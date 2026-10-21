#pragma once
#include "types.h"
#include "primitives/cone.h"

// ---------------------------------------------------------
// Lathe: тело вращения профиля вокруг оси Y.
// Профиль — цепочка LathePoint (y, radius); между соседними точками —
// конический сегмент на оси Y. Пересечение луча считаем по всем сегментам,
// ближайшее попадание выигрывает. Касательная каждой точки — ось Y
// (анизотропные материалы, например волосы/струны, работают).
// ---------------------------------------------------------

// Пересечение одного конического сегмента (p0.y -> p1.y, радиусы r0->r1).
GPU_DEVICE inline bool intersect_lathe_segment(
    const Ray& ray, const LathePoint& p0, const LathePoint& p1,
    HitRecord& rec)
{
    SweepPoint s0, s1;
    s0.position = {0.0f, p0.y, 0.0f};
    s0.radius = p0.radius;
    s1.position = {0.0f, p1.y, 0.0f};
    s1.radius = p1.radius;
    return intersect_cone_segment(ray, s0, s1, rec);
}

// Пересечение луча со всем латехом (все конические сегменты профиля).
GPU_DEVICE inline bool intersect_lathe(
    const Ray& ray, const Lathe& l, const LathePoint* points, HitRecord& rec)
{
    if (l.point_count < 2) return false;

    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;

    int base = l.point_offset;
    for (int i = 0; i < l.point_count - 1; i++) {
        const LathePoint& p0 = points[base + i];
        const LathePoint& p1 = points[base + i + 1];
        if (p0.radius <= 0.0f && p1.radius <= 0.0f) continue;

        if (intersect_lathe_segment(ray, p0, p1, temp_rec)) {
            if (temp_rec.t < closest_t) {
                // сдвигаем точку/нормаль на реальный центр латеха
                temp_rec.point = ray.origin + temp_rec.t * ray.direction;
                rec = temp_rec;
                rec.material_id = l.material_id;
                rec.object_id = -1;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
    }
    return hit_anything;
}

// Интервальная версия (для CSG): полный вход/выход луча через латех.
// Проходим все сегменты; собираем минимальный вход и максимальный выход.
GPU_DEVICE inline bool lathe_intervals(
    const Ray& ray, const Lathe& l, const LathePoint* points,
    float t0, float t1, float& t_enter, float& t_exit)
{
    t_enter = -1e30f;
    t_exit = -1e30f;
    bool found = false;
    int base = l.point_offset;
    for (int i = 0; i < l.point_count - 1; i++) {
        const LathePoint& p0 = points[base + i];
        const LathePoint& p1 = points[base + i + 1];
        if (p0.radius <= 0.0f && p1.radius <= 0.0f) continue;

        // переиспользуем конус: считаем пересечения (два корня)
        SweepPoint s0, s1;
        s0.position = {0.0f, p0.y, 0.0f};
        s0.radius = p0.radius;
        s1.position = {0.0f, p1.y, 0.0f};
        s1.radius = p1.radius;
        // упрощённо: берём ближайшее и дальнее попадание через два вызова
        Ray r = ray;
        r.t_min = t0; r.t_max = t1;
        HitRecord h1, h2;
        // local копии с ограничением t_max -> получаем вход через обрезанный луч
        if (intersect_cone_segment(r, s0, s1, h1) && h1.t < t1) {
            if (!found || h1.t < t_enter) t_enter = h1.t;
            found = true;
            // дальний корень: режем луч от t_enter+eps
            Ray r2 = ray;
            r2.t_min = h1.t + 0.001f; r2.t_max = t1;
            if (intersect_cone_segment(r2, s0, s1, h2) && h2.t <= t1) {
                if (h2.t > t_exit) t_exit = h2.t;
            }
        }
    }
    return found && t_enter <= t_exit;
}