#pragma once
#include "types.h"

// ---------------------------------------------------------
// HeightField: процедурный ландшафт, геометрия как функция y = h(x, z).
// Никаких треугольников: высота считается детерминированным шумом (fBm),
// пересечение луча — спейсинг-марчинг по AABB примитива с бинарным
// уточнением. Генератор шума одинаков на хосте (Blender-превью) и на GPU.
// ---------------------------------------------------------

// Псевдослучайное значение ячейки сетки шума. Детерминированное:
// одинаковые (xi, zi, seed) дают одно и то же число на любой платформе.
GPU_HOST_DEVICE inline float hf_cell_hash(int xi, int zi, int seed) {
    unsigned h = (unsigned)(xi * 374761393 + zi * 668265263 + seed * 974634097);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return float(h & 0xffffffu) / 16777216.0f;   // 0..1
}

// Билинейная интерполяция ячеек вокруг точки (fx, fz).
GPU_HOST_DEVICE inline float hf_value_noise(float fx, float fz, int seed) {
    int x0 = (int)floorf(fx), z0 = (int)floorf(fz);
    float tx = fx - (float)x0, tz = fz - (float)z0;
    // сглаживание smoothstep
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);

    float v00 = hf_cell_hash(x0, z0, seed);
    float v10 = hf_cell_hash(x0 + 1, z0, seed);
    float v01 = hf_cell_hash(x0, z0 + 1, seed);
    float v11 = hf_cell_hash(x0 + 1, z0 + 1, seed);

    float a = v00 + (v10 - v00) * tx;
    float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * tz;                      // 0..1
}

// fBm: сумма октав value-шума. freq — базовая частота (ед. мира на ячейку),
// octaves — число октав, lacunarity=2, gain=0.5. Амплитуда нормирована к 0..1.
GPU_HOST_DEVICE inline float hf_fbm(float x, float z, float freq,
                                    int octaves, int seed) {
    float fx = x * freq, fz = z * freq;
    float amp = 0.5f, sum = 0.0f, norm = 0.0f;
    for (int o = 0; o < octaves; o++) {
        sum += amp * hf_value_noise(fx, fz, seed + o * 131);
        norm += amp;
        amp *= 0.5f;
        fx *= 2.0f; fz *= 2.0f;
    }
    return sum / norm;                            // 0..1
}

// Высота над базовой плоскостью (0..height) открытого ландшафта.
// Режим из карты (image != nullptr): высота = яркость пикселя (bilinear),
// нормированная на [0, height]. Локальные координаты [0,1]x[0,1] отображаются
// на всю карту; за пределами тайла высота 0 (как и в процедурном режиме).
GPU_DEVICE inline float hf_image_height(const HeightField& hf, float x, float z) {
    float lx = (x - hf.center.x) / hf.size_x + 0.5f;
    float lz = (z - hf.center.z) / hf.size_z + 0.5f;
    if (lx < 0.0f || lx > 1.0f || lz < 0.0f || lz > 1.0f) return 0.0f;
    if (hf.img_w < 1 || hf.img_h < 1) return 0.0f;

    float fu = lx * (float)(hf.img_w - 1);
    float fv = lz * (float)(hf.img_h - 1);
    int u0 = (int)fu, v0 = (int)fv;
    int u1 = (u0 < hf.img_w - 1) ? u0 + 1 : u0;
    int v1 = (v0 < hf.img_h - 1) ? v0 + 1 : v0;
    float tx = fu - (float)u0, ty = fv - (float)v0;

    auto pixel = [&](int u, int v) -> float {
        return (float)hf.image[v * hf.img_w + u] / 255.0f;
    };
    float h00 = pixel(u0, v0), h10 = pixel(u1, v0);
    float h01 = pixel(u0, v1), h11 = pixel(u1, v1);
    float h = h00 * (1.0f - tx) * (1.0f - ty)
            + h10 * tx       * (1.0f - ty)
            + h01 * (1.0f - tx) * ty
            + h11 * tx       * ty;
    return h * hf.height;
}

// Высота над базовой плоскостью (0..height): либо из карты высот, либо
// процедурный fBm-шум, либо за пределами тайла 0.
GPU_DEVICE inline float hf_eval(const HeightField& hf, float x, float z) {
    // локальные координаты: центр тайла в (0,0), размер 1x1
    float lx = (x - hf.center.x) / hf.size_x + 0.5f;
    float lz = (z - hf.center.z) / hf.size_z + 0.5f;
    // за пределами тайла высота 0 (не мешает соседним примитивам)
    if (lx < 0.0f || lx > 1.0f || lz < 0.0f || lz > 1.0f) return 0.0f;

    if (hf.image) return hf_image_height(hf, x, z);

    float t = hf_fbm(lx, lz, hf.freq, hf.octaves, hf.seed);
    // наслаиваем случайный мелкий шум сверху (детализация, без новой октавы)
    float detail = hf_value_noise(lx * 7.0f, lz * 7.0f, hf.seed + 7777) * 0.06f;
    t = fminf(t + detail - 0.03f, 1.0f);
    return t * hf.height;
}

// Пересечение луча с тайлом высот.
// Луч идёт сверху вниз или снизу вверх — марчим внутри AABB по секциям,
// находим первый переход пересечения, затем бинарно уточняем.
GPU_DEVICE inline bool intersect_heightfield(const Ray& ray, const HeightField& hf,
                                             HitRecord& rec) {
    // AABB примитива
    float3 bmin = {hf.center.x - 0.5f * hf.size_x, hf.center.y,
                   hf.center.z - 0.5f * hf.size_z};
    float3 bmax = {hf.center.x + 0.5f * hf.size_x, hf.center.y + hf.height,
                   hf.center.z + 0.5f * hf.size_z};

    // Вход в AABB (слабая версия — для марчинга достаточно)
    float t0, t1;
    {
        float invDx = 1.0f / ray.direction.x, invDy = 1.0f / ray.direction.y,
              invDz = 1.0f / ray.direction.z;
        float tx0 = (bmin.x - ray.origin.x) * invDx, tx1 = (bmax.x - ray.origin.x) * invDx;
        float ty0 = (bmin.y - ray.origin.y) * invDy, ty1 = (bmax.y - ray.origin.y) * invDy;
        float tz0 = (bmin.z - ray.origin.z) * invDz, tz1 = (bmax.z - ray.origin.z) * invDz;
        if (tx0 > tx1) { float t = tx0; tx0 = tx1; tx1 = t; }
        if (ty0 > ty1) { float t = ty0; ty0 = ty1; ty1 = t; }
        if (tz0 > tz1) { float t = tz0; tz0 = tz1; tz1 = t; }
        t0 = fmaxf(fmaxf(tx0, ty0), tz0);
        t1 = fminf(fminf(tx1, ty1), tz1);
        if (t1 < ray.t_min || t0 > ray.t_max || t1 < t0) return false;
        t0 = fmaxf(t0, ray.t_min);
        t1 = fminf(t1, ray.t_max);
    }

    // Шаг марчинга: диагональ тайла / N. N фиксируем по размеру тайла.
    float diag = sqrtf(hf.size_x * hf.size_x + hf.size_z * hf.size_z);
    float step = fmaxf(diag / 64.0f, 1e-4f);

    float t = t0;
    float y_prev = 0.0f;
    float y_at_t = 0.0f;
    bool inside = false;
    const int MAX_STEPS = 256;
    for (int i = 0; i < MAX_STEPS; i++) {
        float3 p = ray.origin + t * ray.direction;
        float hgt = hf_eval(hf, p.x, p.z) + hf.center.y;
        bool iy = (p.y <= hgt);
        if (iy != inside) {
            // перешли поверхность — бинарное уточнение между t-step и t
            float lo = t - step, hi = t;
            if (lo < t0) lo = t0;
            for (int b = 0; b < 10; b++) {
                float mid = 0.5f * (lo + hi);
                float3 pm = ray.origin + mid * ray.direction;
                float hm = hf_eval(hf, pm.x, pm.z) + hf.center.y;
                if ((pm.y <= hm) == iy) hi = mid; else lo = mid;
            }
            float tt = 0.5f * (lo + hi);
            // точка попадания
            float3 P = ray.origin + tt * ray.direction;
            float H = hf_eval(hf, P.x, P.z) + hf.center.y;
            if (tt >= ray.t_min && tt <= ray.t_max) {
                rec.t = tt;
                rec.hit = 1;
                rec.point = P;
                // нормаль через конечные разности
                float e = fmaxf(hf.size_x, hf.size_z) / 256.0f;
                float hxp = hf_eval(hf, P.x + e, P.z);
                float hxm = hf_eval(hf, P.x - e, P.z);
                float hzp = hf_eval(hf, P.x, P.z + e);
                float hzm = hf_eval(hf, P.x, P.z - e);
                rec.normal = normalize({-(hxp - hxm) / (2.0f * e), 1.0f,
                                        -(hzp - hzm) / (2.0f * e)});
                rec.tangent = {1.0f, 0.0f, 0.0f};
                rec.uv = {P.x, P.z};
                if (dot(rec.normal, ray.direction) > 0.0f)
                    rec.normal = -rec.normal;
                return true;
            }
            break; // за пределами — не нашли корректного времени
        }
        inside = iy;
        y_prev = hgt;
        (void)y_prev;
        (void)y_at_t;
        t += step;
        if (t > t1) break;
    }
    return false;
}