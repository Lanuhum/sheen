#pragma once
#include "types.h"

// ---------------------------------------------------------
// Superellipsoid: |x/a|^n + |y/b|^n + |z/c|^n = 1.
// Полностью аналитический примитив: n=2 — эллипсоид, n≈8..16 — куб
// с гладкими фасками. Пересечение — сэмплинг F по лучу + бисекция
// пересечения F=1 (выпуклое тело). Нормаль — аналитический градиент.
// ---------------------------------------------------------

// F(p) = |x/a|^n + |y/b|^n + |z/c|^n  (поверхность при F=1)
GPU_HOST_DEVICE inline float se_f(const Superellipsoid& s, float3 p) {
    float qx = (p.x - s.center.x) / s.a;
    float qy = (p.y - s.center.y) / s.b;
    float qz = (p.z - s.center.z) / s.c;
    return powf(fabsf(qx), s.n) + powf(fabsf(qy), s.n) + powf(fabsf(qz), s.n);
}

// Градиент F в точке (нормаль наружу)
GPU_HOST_DEVICE inline float3 se_grad(const Superellipsoid& s, float3 p) {
    float qx = (p.x - s.center.x) / s.a;
    float qy = (p.y - s.center.y) / s.b;
    float qz = (p.z - s.center.z) / s.c;
    float gx = s.n * powf(fabsf(qx), s.n - 1.0f) * (qx >= 0.0f ? 1.0f : -1.0f) / s.a;
    float gy = s.n * powf(fabsf(qy), s.n - 1.0f) * (qy >= 0.0f ? 1.0f : -1.0f) / s.b;
    float gz = s.n * powf(fabsf(qz), s.n - 1.0f) * (qz >= 0.0f ? 1.0f : -1.0f) / s.c;
    return {gx, gy, gz};
}

// AABB суперэллипсоида (мнемоника: |x|<=a всегда)
GPU_HOST_DEVICE inline void superellipsoid_bounds(
    const Superellipsoid& s, float3& bmin, float3& bmax
) {
    bmin = {s.center.x - s.a, s.center.y - s.b, s.center.z - s.c};
    bmax = {s.center.x + s.a, s.center.y + s.b, s.center.z + s.c};
}

// Интервальная версия: полные вход/выход луча через суперэллипсоид.
// Нужна для CSG. Выпуклое тело: сэмплируем F вдоль [t0,t1], ищем переходы
// F=1. Если луч начинается внутри (F(t0)<=1), интервал = [t0, t_exit].
GPU_DEVICE inline bool se_intervals(const Ray& ray, const Superellipsoid& se,
                                    float t0, float t1,
                                    float& t_enter, float& t_exit) {
    // Сужаем диапазон сэмплинга до AABB тела: t1 может быть 1e10 (дальний
    // луч), а на 32 сэмпла по всему пути тело не попадает вовсе.
    float3 bmin, bmax;
    superellipsoid_bounds(se, bmin, bmax);
    {
        float invDx = 1.0f / ray.direction.x;
        float tx0 = (bmin.x - ray.origin.x) * invDx, tx1 = (bmax.x - ray.origin.x) * invDx;
        if (tx0 > tx1) { float t = tx0; tx0 = tx1; tx1 = t; }
        float invDy = 1.0f / ray.direction.y;
        float ty0 = (bmin.y - ray.origin.y) * invDy, ty1 = (bmax.y - ray.origin.y) * invDy;
        if (ty0 > ty1) { float t = ty0; ty0 = ty1; ty1 = t; }
        float invDz = 1.0f / ray.direction.z;
        float tz0 = (bmin.z - ray.origin.z) * invDz, tz1 = (bmax.z - ray.origin.z) * invDz;
        if (tz0 > tz1) { float t = tz0; tz0 = tz1; tz1 = t; }
        float tA = fmaxf(fmaxf(tx0, ty0), tz0);
        float tB = fminf(fminf(tx1, ty1), tz1);
        if (tB < t0 || tA > t1 || tB < tA) return false;
        t0 = fmaxf(t0, tA);
        t1 = fminf(t1, tB);
    }

    const float EPS = 1.0f;              // порог поверхности
    const int NS = 32;
    const float STEP = (t1 - t0) / (float)NS;
    float t_prev = t0;
    float F_prev = se_f(se, ray.origin + t_prev * ray.direction);
    bool inside = (F_prev <= EPS);
    t_enter = -1e30f;
    t_exit = -1e30f;
    bool found_enter = false, found_exit = false;

    if (inside) {
        // старт внутри тела: вход на t0
        t_enter = t0;
        found_enter = true;
    }

    // поиск выхода (переход внутрь->наружу)
    for (int i = 1; i <= NS && !found_exit; i++) {
        float t = fminf(t0 + STEP * (float)i, t1);
        float F = se_f(se, ray.origin + t * ray.direction);
        if (inside && F > EPS) {
            // выход: бисекция между t_prev и t
            float lo = t_prev, hi = t;
            for (int b = 0; b < 20; b++) {
                float mid = 0.5f * (lo + hi);
                if (se_f(se, ray.origin + mid * ray.direction) > EPS) hi = mid;
                else lo = mid;
            }
            t_exit = 0.5f * (lo + hi);
            if (t_exit > t1) t_exit = t1;
            found_exit = true;
            break;
        }
        t_prev = t;
        F_prev = F;
        if (!inside && F <= EPS) {
            // вход: бисекция  (для луча, начинающегося снаружи)
            float lo = t_prev, hi = t;
            for (int b = 0; b < 20; b++) {
                float mid = 0.5f * (lo + hi);
                if (se_f(se, ray.origin + mid * ray.direction) <= EPS) hi = mid;
                else lo = mid;
            }
            t_enter = 0.5f * (lo + hi);
            if (t_enter < t0) t_enter = t0;
            found_enter = true;
            inside = true;
        }
    }
    if (!found_enter) return false;
    if (!found_exit) t_exit = t1;         // так и не вышел (не должно быть)
    return t_enter <= t_exit;
}

// ---------------------------------------------------------
// Пересечение луча с суперэллипсоидом (ближайшая точка, для BVH/ядра).
// Сэмплируем F вдоль [t0, t1] (AABB); ищем первое пересечение F=1.
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_superellipsoid(
    const Ray& ray, const Superellipsoid& se, HitRecord& rec
) {
    float3 bmin, bmax;
    superellipsoid_bounds(se, bmin, bmax);

    // вход/выход AABB
    float t0, t1;
    {
        float invDx = 1.0f / ray.direction.x;
        float tx0 = (bmin.x - ray.origin.x) * invDx, tx1 = (bmax.x - ray.origin.x) * invDx;
        if (tx0 > tx1) { float t = tx0; tx0 = tx1; tx1 = t; }
        float invDy = 1.0f / ray.direction.y;
        float ty0 = (bmin.y - ray.origin.y) * invDy, ty1 = (bmax.y - ray.origin.y) * invDy;
        if (ty0 > ty1) { float t = ty0; ty0 = ty1; ty1 = t; }
        float invDz = 1.0f / ray.direction.z;
        float tz0 = (bmin.z - ray.origin.z) * invDz, tz1 = (bmax.z - ray.origin.z) * invDz;
        if (tz0 > tz1) { float t = tz0; tz0 = tz1; tz1 = t; }
        t0 = fmaxf(fmaxf(tx0, ty0), tz0);
        t1 = fminf(fminf(tx1, ty1), tz1);
        if (t1 < ray.t_min || t0 > ray.t_max || t1 < t0) return false;
    }
    t0 = fmaxf(t0, ray.t_min);
    t1 = fminf(t1, ray.t_max);

    // переиспользуем интервальную логику: нужен целиком первый интервал
    float t_enter, t_exit;
    if (!se_intervals(ray, se, t0, t1, t_enter, t_exit)) return false;

    float t = t_enter;
    float3 P = ray.origin + t * ray.direction;
    rec.t = t;
    rec.hit = 1;
    rec.point = P;
    rec.normal = normalize(se_grad(se, P));
    rec.tangent = {1.0f, 0.0f, 0.0f};
    rec.uv = {P.x, P.y};
    if (dot(rec.normal, ray.direction) > 0.0f) rec.normal = -rec.normal;
    return true;
}