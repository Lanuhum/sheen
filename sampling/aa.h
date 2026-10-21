#pragma once
#include "types.h"

// ---------------------------------------------------------
// Сэмплирование пикселя.
// Число сэмплов задаётся рендерером (render_kernel) и приходит
// параметром; здесь только раскладка сэмплов внутри пикселя.
// Пока это сетка NxN со стратифицированным джиттером; позже сюда
// добавятся более умные схемы, не трогая ядро.
// ---------------------------------------------------------
const int SHEEN_AA_SAMPLES = 4;   // значение по умолчанию для CLI и тестов
const int SHEEN_AA_MAX = 64;

// Фиксированная сетка 2x2 — историческая раскладка для 4 сэмплов.
GPU_DEVICE inline float2 aa_sample_offset(int s) {
    switch (s) {
        case 0:  return {0.25f, 0.25f};
        case 1:  return {0.75f, 0.25f};
        case 2:  return {0.25f, 0.75f};
        default: return {0.75f, 0.75f};
    }
}

// Экранные координаты пикселя + смещение выборки -> координаты в плоскости кадра.
GPU_DEVICE inline void aa_uv(int x, int y, int width, int height, float2 offset,
                             float& u, float& v) {
    u = (float(x) + offset.x) / float(width) * 2.0f - 1.0f;
    v = 1.0f - (float(y) + offset.y) / float(height) * 2.0f;
}

// Детерминированный хеш: один и тот же кадр воспроизводится побитово.
GPU_DEVICE inline unsigned int aa_hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

GPU_DEVICE inline float aa_rand(unsigned int seed) {
    return float(aa_hash(seed) & 0xffffffu) / 16777216.0f;
}

// Смещение выборки внутри её ячейки: геометрия тоньше пикселя
// перестаёт пропадать целиком, покрытие становится дробным.
GPU_DEVICE inline float2 aa_sample_offset_jitter(int s, int x, int y) {
    float2 cell = aa_sample_offset(s);
    unsigned int seed = (unsigned int)(x * 73856093) ^ (unsigned int)(y * 19349663)
                      ^ (unsigned int)(s * 83492791);
    float jx = aa_rand(seed) - 0.5f;
    float jy = aa_rand(seed ^ 0x9e3779b9u) - 0.5f;
    return {cell.x + jx * 0.5f, cell.y + jy * 0.5f};
}

// Раскладка произвольного числа сэмплов n: сетка side x side внутри
// пикселя, каждый сэмпл джиттерится в своей ячейке. n <= 4 — прежняя
// сетка 2x2, чтобы кадры со старыми настройками не менялись.
GPU_DEVICE inline float2 aa_sample_offset_n(int s, int n, int x, int y) {
    if (n <= SHEEN_AA_SAMPLES) return aa_sample_offset_jitter(s, x, y);

    int side = 2;
    while (side * side < n) side++;
    int cx = s % side;
    int cy = s / side;
    float inv = 1.0f / (float)side;

    unsigned int seed = (unsigned int)(x * 73856093) ^ (unsigned int)(y * 19349663)
                      ^ (unsigned int)(s * 83492791);
    float jx = aa_rand(seed);
    float jy = aa_rand(seed ^ 0x9e3779b9u);
    return {(float(cx) + jx) * inv, (float(cy) + jy) * inv};
}
