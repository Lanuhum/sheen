#pragma once
#include "types.h"

// ---------------------------------------------------------
// Prism (семантика POV-Ray linear_spline / linear_sweep):
// 2D-профиль в XZ, экструзия по Y.
//  - Под-контуры закрыты повтором первой точки (или автозакрыты).
//  - Дырка там, где накладывается чётное число под-контуров.
//  - Крышки: Y = y_min / y_max, если точка (x,z) внутри профиля.
//  - Стены: вертикальные грани по рёбрам под-контуров.
// ---------------------------------------------------------

// Число под-контуров в профиле (по повторам первой точки).
// (Не используется напрямую — контуры ищутся в prism_point_inside.)

// Точка внутри одного замкнутого контура (points[base..base+count])?
// Контур может быть замкнут (последняя точка == первой) или автозакрыт.
GPU_DEVICE inline bool prism_in_contour(float x, float z, const PrismPoint* pts,
                                        int base, int count) {
    bool inside = false;
    int j = count - 1;
    for (int i = 0; i < count; i++) {
        const PrismPoint& pi = pts[base + i];
        const PrismPoint& pj = pts[base + j];
        // автозакрытие последнего ребра: если последняя точка == первой, пропускаем
        bool skip_last = (i == 0);   // ребро [base+count-1, base]
        if (i == 0) {
            // если последняя точка повторяет первую — ребро нулевой длины
            if (pts[base + count - 1].x == pi.x && pts[base + count - 1].z == pi.z) skip_last = true;
        }
        if ((pi.z > z) != (pj.z > z)) {
            float xint = (pj.x - pi.x) * (z - pi.z) / (pj.z - pi.z) + pi.x;
            if (x < xint) inside = !inside;
        }
        j = i;
    }
    return inside;
}

// Точка (x,z) внутри профиля: нечётное число контуров содержит её.
GPU_DEVICE inline bool prism_point_inside(float x, float z, const Prism& p,
                                          const PrismPoint* all) {
    // Разбиваем точки на контуры: старт контура — повтор первой точки.
    int base = p.point_offset;
    int cnt = p.point_count;
    int i = 0;
    bool inside = false;
    while (i < cnt) {
        int start = i;
        // контур идёт до повтора стартовой точки или до конца
        int j = i + 1;
        while (j < cnt) {
            if (all[base + j].x == all[base + start].x &&
                all[base + j].z == all[base + start].z) break;
            j++;
        }
        // j — первый повтор стартовой (или cnt): контур = [start, j)
        int clen = j - start;
        if (clen >= 3) {
            if (prism_in_contour(x, z, all, base + start, clen)) inside = !inside;
        }
        i = (j < cnt) ? j + 1 : j;   // пропускаем повтор
    }
    return inside;
}

// Пересечение луча с призмой.
GPU_DEVICE inline bool intersect_prism(const Ray& ray, const Prism& p, const PrismPoint* pts,
                                       HitRecord& rec) {
    float y0 = p.center.y + p.y_min;
    float y1 = p.center.y + p.y_max;

    // Отсек: луч должен пересекать [y0,y1]
    float ty0 = (y0 - ray.origin.y) / ray.direction.y;
    float ty1 = (y1 - ray.origin.y) / ray.direction.y;
    if (ty0 > ty1) { float t = ty0; ty0 = ty1; ty1 = t; }
    float tmin = fmaxf(ty0, ray.t_min);
    float tmax = fminf(ty1, ray.t_max);
    if (tmin > tmax) return false;

    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp;

    // 1. Крышки (Y-плоскости): точка в профиле
    for (int face = 0; face < 2; face++) {
        float yf = (face == 0) ? y0 : y1;
        float t = (yf - ray.origin.y) / ray.direction.y;
        if (t < ray.t_min || t > ray.t_max) continue;
        float3 P = ray.origin + t * ray.direction;
        if (!prism_point_inside(P.x - p.center.x, P.z - p.center.z, p, pts)) continue;
        temp.t = t; temp.hit = 1; temp.point = P;
        temp.normal = {0.0f, (face == 0) ? -1.0f : 1.0f, 0.0f};
        if (dot(temp.normal, ray.direction) > 0.0f) temp.normal = -temp.normal;
        temp.tangent = {1.0f, 0.0f, 0.0f};
        if (t < closest_t) { rec = temp; rec.material_id = p.material_id; closest_t = t; hit_anything = true; }
    }

    // 2. Стены: рёбра всех под-контуров; пересечение проекции луча на XZ с ребром
    {
        float dx = ray.direction.x, dy = ray.direction.y, dz = ray.direction.z;
        float ox = ray.origin.x, oz = ray.origin.z;
        int base = p.point_offset, cnt = p.point_count;
        for (int i = 0; i < cnt; i++) {
            // ребро [i, i+1]: skip переход между контурами (повтор старта)
            int j = (i + 1) % cnt;
            const PrismPoint& pi = pts[base + i];
            const PrismPoint& pj2 = pts[base + j];
            // пропуск закрывающего ребра (повтор точки) — длина 0
            if (pi.x == pj2.x && pi.z == pj2.z) continue;
            float ax = pi.x + p.center.x, az = pi.z + p.center.z;
            float bx = pj2.x + p.center.x, bz = pj2.z + p.center.z;
            float ex = bx - ax, ez = bz - az;
            float denom = ex * dz - ez * dx;
            if (fabsf(denom) < 1e-9f) continue;
            float s = (dx * (az - oz) - dz * (ax - ox)) / denom;
            if (s < -0.001f || s > 1.001f) continue;
            float t;
            if (fabsf(dx) > 1e-9f) t = (ax + s * ex - ox) / dx;
            else if (fabsf(dz) > 1e-9f) t = (az + s * ez - oz) / dz;
            else continue;
            if (t < ray.t_min || t > ray.t_max) continue;
            float yy = ray.origin.y + dy * t;
            if (yy < y0 - 0.001f || yy > y1 + 0.001f) continue;
            temp.t = t; temp.hit = 1; temp.point = ray.origin + t * ray.direction;
            float3 n = {ez, 0.0f, -ex};
            temp.normal = normalize(n);
            temp.tangent = {0.0f, 1.0f, 0.0f};
            if (dot(temp.normal, ray.direction) > 0.0f) temp.normal = -temp.normal;
            if (t < closest_t) { rec = temp; rec.material_id = p.material_id; closest_t = t; hit_anything = true; }
        }
    }
    return hit_anything;
}