#pragma once
#include "types.h"

GPU_DEVICE inline bool intersect_hemisphere(
    const Ray& ray, float3 center, float radius, float3 axis_direction, HitRecord& rec);

#include "primitives/cone.h"

// ---------------------------------------------------------
// Пересечение луча со всей прядью (sphere_sweep).
// Боковая поверхность — конические сегменты, торцы — полусферы.
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_sphere_sweep(
    const Ray& ray,
    const SweepStrand& strand,
    const SweepPoint* points,
    HitRecord& rec
) {
    if (strand.point_count < 2) return false;
    
    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;
    
    int base = strand.point_offset;
    int count = strand.point_count;
    
    // Полусфера на первой точке (смотрит назад, против первого сегмента)
    {
        const SweepPoint& p0 = points[base];
        const SweepPoint& p1 = points[base + 1];
        float3 axis_dir = normalize(p0.position - p1.position);
        
        if (intersect_hemisphere(ray, p0.position, p0.radius, axis_dir, temp_rec)) {
            if (temp_rec.t < closest_t) {
                rec = temp_rec;
                rec.material_id = strand.material_id;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
    }
    
    // Конические сегменты + полусферы на промежуточных точках
    for (int i = 0; i < count - 1; i++) {
        const SweepPoint& p0 = points[base + i];
        const SweepPoint& p1 = points[base + i + 1];
        
        if (intersect_cone_segment(ray, p0, p1, temp_rec)) {
            if (temp_rec.t < closest_t) {
                rec = temp_rec;
                rec.material_id = strand.material_id;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
        
        // Полусфера на p1 (смотрит вперёд, если есть следующий сегмент)
        if (i < count - 2) {
            const SweepPoint& p2 = points[base + i + 2];
            float3 axis_dir = normalize(p2.position - p1.position);
            
            if (intersect_hemisphere(ray, p1.position, p1.radius, axis_dir, temp_rec)) {
                if (temp_rec.t < closest_t) {
                    rec = temp_rec;
                    rec.material_id = strand.material_id;
                    closest_t = temp_rec.t;
                    hit_anything = true;
                }
            }
        }
    }
    
    // Полусфера на последней точке (смотрит вперёд, по последнему сегменту)
    {
        const SweepPoint& p_last = points[base + count - 1];
        const SweepPoint& p_prev = points[base + count - 2];
        float3 axis_dir = normalize(p_last.position - p_prev.position);
        
        if (intersect_hemisphere(ray, p_last.position, p_last.radius, axis_dir, temp_rec)) {
            if (temp_rec.t < closest_t) {
                rec = temp_rec;
                rec.material_id = strand.material_id;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
    }
    
    return hit_anything;
}

// ---------------------------------------------------------
// Мягкое затенение прядью: расстояние от луча до оси сегмента
// даёт длину хорды через трубку, из неё — оптическая плотность.
// Возвращает приращение tau для одного сегмента.
// ---------------------------------------------------------
GPU_DEVICE inline float strand_segment_tau(
    const Ray& ray, const SweepPoint& p0, const SweepPoint& p1, float sigma
) {
    float3 u = p1.position - p0.position;
    float A = dot(u, u);
    if (A < 1e-12f) return 0.0f;

    float3 v = ray.direction;
    float3 w = ray.origin - p0.position;

    float B = dot(u, v);
    float C = dot(v, v);
    float D = dot(u, w);
    float E = dot(v, w);

    float denom = A * C - B * B;
    float s = (denom > 1e-9f) ? (B * E - C * D) / denom : 0.0f;   // вдоль сегмента
    s = fminf(fmaxf(s, 0.0f), 1.0f);

    float t = (B * s + E) / C;                                    // вдоль луча
    if (t < 0.0f) {
        t = 0.0f;
        s = fminf(fmaxf(-D / A, 0.0f), 1.0f);
    }
    if (t > ray.t_max) return 0.0f;

    float3 closest = w + u * s - v * t;
    float d2 = dot(closest, closest);

    float r = p0.radius + (p1.radius - p0.radius) * s;
    float r2 = r * r;
    if (d2 >= r2) return 0.0f;

    // длина хорды через цилиндр радиуса r на расстоянии d от оси
    float chord = 2.0f * sqrtf(fmaxf(r2 - d2, 0.0f));
    return sigma * chord;
}

// Оптическая плотность всей пряди
GPU_DEVICE inline float strand_tau(
    const Ray& ray, const SweepStrand& strand, const SweepPoint* points, float sigma
) {
    float tau = 0.0f;
    int base = strand.point_offset;
    int count = strand.point_count;
    for (int i = 0; i < count - 1; i++) {
        tau += strand_segment_tau(ray, points[base + i], points[base + i + 1], sigma);
        if (tau > 6.0f) break;  // дальше всё равно непрозрачно
    }
    return tau;
}
