#pragma once
#include "types.h"

// ---------------------------------------------------------
// Затенение поверхности: только BRDF, без обхода сцены.
// Теневые лучи и перебор источников живут в render_kernel.
// Здесь же позже появится анизотропный лепесток для прядей —
// ему нужна касательная в точке попадания.
// ---------------------------------------------------------
struct ShadeInput {
    float3 point;
    float3 normal;
    float3 tangent;    // касательная пряди, {0,0,0} если её нет
    float2 uv;         // координаты текстуры
    float3 view_dir;   // от точки к камере
    float  uv_fp;      // размер пикселя в единицах UV (для выбора мипа)
};

// Уровень мипа: сколько UV приходится на пиксель × ширина текстуры.
GPU_DEVICE inline float tex_lod(const TextureRef& tex, float uv_fp) {
    return log2f(fmaxf(uv_fp * (float)tex.width, 1.0f));
}

// Билинейная выборка с явным уровнем мипа. UV вне [0,1] — повторение.
// Уровни лежат подряд: смещение считается по width/height, как в build_mips.
GPU_DEVICE inline float3 sample_texture_lod(const TextureRef& tex, float2 uv, float lod) {
    int level = (int)floorf(lod + 0.5f);
    if (level < 0) level = 0;
    if (level >= tex.mips) level = tex.mips - 1;

    int w = tex.width, h = tex.height;
    size_t off = 0;
    for (int i = 0; i < level; i++) {
        off += (size_t)w * h * 3;
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
    }
    const unsigned char* base = tex.pixels + off;

    float u = uv.x - floorf(uv.x);
    // V в OBJ растёт вверх, строки PPM — сверху вниз
    float v = 1.0f - (uv.y - floorf(uv.y));
    float fx = u * (float)(w - 1);
    float fy = v * (float)(h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = (x0 + 1) % w;
    int y1 = (y0 + 1) % h;
    float tx = fx - (float)x0, ty = fy - (float)y0;
    auto at = [&](int x, int y) {
        const unsigned char* p = base + (size_t)(y * w + x) * 3;
        return float3{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f};
    };
    float3 c00 = at(x0, y0), c10 = at(x1, y0);
    float3 c01 = at(x0, y1), c11 = at(x1, y1);
    float3 a = c00 * (1.0f - tx) + c10 * tx;
    float3 b = c01 * (1.0f - tx) + c11 * tx;
    return a * (1.0f - ty) + b * ty;
}

GPU_DEVICE inline float3 sample_texture(const TextureRef& tex, float2 uv) {
    return sample_texture_lod(tex, uv, 0.0f);
}

GPU_DEVICE inline float3 albedo(const Material& mat, const ShadeInput& in,
                                const TextureRef* textures, int num_textures) {
    if (mat.texture_id >= 0 && mat.texture_id < num_textures) {
        const TextureRef& tex = textures[mat.texture_id];
        return sample_texture_lod(tex, in.uv, tex_lod(tex, in.uv_fp));
    }
    return mat.diffuse_color;
}

// ---------------------------------------------------------
// Карта свойств материала (mask_id): R — transmission, G — IOR-1, B — roughness.
// Одна карта вместо трёх: жила минерала задаёт сразу прозрачность, показатель
// преломления и глянец, и все три совпадают по построению.
// Нет карты — все функции возвращают нули (нулевая стоимость без маски).
// ---------------------------------------------------------
GPU_DEVICE inline bool has_mask(const Material& mat, int num_textures) {
    return mat.mask_id >= 0 && mat.mask_id < num_textures;
}

// roughness (0..1) -> показатель Блинна-Фонга; та же формула, что в мосте:
// alpha = r², exp = 2/alpha² - 2.
GPU_DEVICE inline float roughness_to_shininess(float r) {
    float rr = fmaxf(r, 0.02f);
    float r2 = rr * rr;
    return fminf(fmaxf(2.0f / (r2 * r2) - 2.0f, 1.0f), 4096.0f);
}

// Пропускание в конкретной точке попадания: скаляр материала × маска.
GPU_DEVICE inline float hit_transmission(const HitRecord& rec, const Material& mat,
                                         const TextureRef* textures, int num_textures) {
    if (mat.transmission <= 0.0f) return 0.0f;
    if (has_mask(mat, num_textures)) {
        return mat.transmission * sample_texture(textures[mat.mask_id], rec.uv).x;
    }
    return mat.transmission;
}

// Показатель преломления в точке: G маски отображается в 1.0..2.0.
GPU_DEVICE inline float hit_ior(const HitRecord& rec, const Material& mat,
                                const TextureRef* textures, int num_textures) {
    if (has_mask(mat, num_textures)) {
        return 1.0f + sample_texture(textures[mat.mask_id], rec.uv).y;
    }
    return mat.ior;
}

// Модель волос: диффузная часть по sin(T,L), блик двумя лепестками
// со сдвинутой вдоль нормали касательной (Kajiya-Kay / TRT).
GPU_DEVICE inline float3 shade_hair(const Material& mat, const ShadeInput& in,
                                    float3 L, float3 radiance) {
    float3 T = in.tangent;
    float3 N = in.normal;
    float3 V = in.view_dir;

    float tl = dot(T, L);
    float sin_tl = sqrtf(fmaxf(1.0f - tl * tl, 0.0f));

    // свет с изнанки усиливает освещённость там, где он идёт против касательной
    float back = fmaxf(-tl, 0.0f);
    float diffuse = sin_tl + mat.hair_backlight * back * back;

    float3 result = mat.diffuse_color * radiance * diffuse * 0.75f;

    // веса лепестков: если не заданы, работаем как один лепесток на mat.specular
    float w_r = mat.hair_spec_r;
    float w_t = mat.hair_spec_t;
    float e_r = mat.hair_exp_r;
    float e_t = mat.hair_exp_t;
    if (w_r <= 0.0f && w_t <= 0.0f) {
        w_r = mat.specular;
        e_r = mat.shininess;
    }

    float3 H = normalize(L + V);

    if (w_r > 0.0f) {
        float3 Tr = normalize(T + mat.hair_shift * N);
        float sr = dot(Tr, H);
        float rr = powf(sqrtf(fmaxf(1.0f - sr * sr, 0.0f)), fmaxf(e_r, 1.0f));
        result += radiance * w_r * rr;
    }
    if (w_t > 0.0f) {
        float3 Tt = normalize(T - mat.hair_shift * N);
        float st = dot(Tt, H);
        float rt = powf(sqrtf(fmaxf(1.0f - st * st, 0.0f)), fmaxf(e_t, 1.0f));
        result += radiance * w_t * rt;
    }
    return result;
}

// Вклад одного источника при известном отсутствии тени.
// Вклад одного источника: L — направление НА источник, radiance — уже
// умноженные цвет, мощность и затухание. Так Sun, Point и Area считаются
// одной формулой, различается только подготовка L/radiance в render_kernel.
GPU_DEVICE inline float3 shade_direct(const Material& mat, const ShadeInput& in,
                                      float3 L, float3 radiance,
                                      const TextureRef* textures, int num_textures,
                                      float shininess) {
    if (mat.anisotropy > 0.0f) {
        return shade_hair(mat, in, L, radiance);
    }
    float3 base = albedo(mat, in, textures, num_textures);
    float diff = fmaxf(dot(in.normal, L), 0.0f);
    float3 result = base * radiance * diff;

    if (mat.specular > 0.0f && diff > 0.0f) {
        float3 half_vec = normalize(L + in.view_dir);
        float spec = powf(fmaxf(dot(in.normal, half_vec), 0.0f), shininess);
        result += radiance * mat.specular * spec;
    }
    return result;
}
