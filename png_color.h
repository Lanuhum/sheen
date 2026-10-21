#pragma once
#include <cmath>

// ---------------------------------------------------------
// Перевод линейного цвета ядра в sRGB для записи в PNG.
// Одна реализация на все выходные пути: и C-API (Blender),
// и CLI-стенд должны давать одинаковую картинку.
// ---------------------------------------------------------
namespace sheen {

inline float srgb_encode(float c) {
    if (!(c > 0.0f)) return 0.0f;
    if (c > 1.0f) c = 1.0f;
    return (c <= 0.0031308f) ? c * 12.92f
                             : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline unsigned char linear_to_srgb_byte(float c) {
    float v = srgb_encode(c) * 255.0f + 0.5f;
    return (unsigned char)(v > 255.0f ? 255.0f : v);
}

} // namespace sheen
