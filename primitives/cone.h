#pragma once
#include "types.h"

// ---------------------------------------------------------
// Пересечение луча с коническим сегментом пряди.
// Нормаль — радиальная от оси сегмента к точке (огибающая сфер),
// БЕЗ осевого слагаемого -k*d_axis. Осевое слагаемое отклоняло
// нормаль на arctg(k) и давало излом на стыках, где соседние
// сегменты имеют общий центр и радиус, но разные k и оси.
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_cone_segment(
    const Ray& ray,
    const SweepPoint& p0,
    const SweepPoint& p1,
    HitRecord& rec
) {
    float3 axis = p1.position - p0.position;
    float L2 = dot(axis, axis);
    if (L2 < 1e-12f) return false;
    
    float L = sqrtf(L2);
    float3 d_axis = axis / L;
    
    float r0 = p0.radius;
    float r1 = p1.radius;
    float dr = r1 - r0;
    float k = dr / L;
    
    float3 Q = ray.origin - p0.position;
    float u = dot(ray.direction, d_axis);
    float v = dot(Q, d_axis);
    
    float3 D_perp = ray.direction - u * d_axis;
    float3 Q_perp = Q - v * d_axis;
    
    float A = dot(D_perp, D_perp) - k * k * u * u;
    float B = 2.0f * (dot(D_perp, Q_perp) - k * r0 * u - k * k * v * u);
    float C = dot(Q_perp, Q_perp) - k * k * v * v - 2.0f * k * r0 * v - r0 * r0;
    
    float discriminant = B * B - 4.0f * A * C;
    if (discriminant < 0.0f) return false;
    
    float sqrtDisc = sqrtf(discriminant);
    float inv2A = 1.0f / (2.0f * A);
    float s1 = (-B - sqrtDisc) * inv2A;
    float s2 = (-B + sqrtDisc) * inv2A;
    
    for (int i = 0; i < 2; i++) {
        float s = (i == 0) ? s1 : s2;
        if (s < ray.t_min || s > ray.t_max) continue;
        
        float t_param = v + s * u;
        if (t_param < 0.0f || t_param > L) continue;
        
        rec.t = s;
        rec.hit = 1;
        rec.point = ray.origin + s * ray.direction;
        
        // Радиальная нормаль огибающей сфер: от оси сегмента к точке.
        float3 radial = Q_perp + s * D_perp;
        rec.normal = normalize(radial);
        rec.tangent = d_axis;
        
        // Нормаль должна смотреть против луча.
        if (dot(rec.normal, ray.direction) > 0.0f) {
            rec.normal = -rec.normal;
        }
        
        return true;
    }
    
    return false;
}

// ---------------------------------------------------------
// Пересечение луча с ПОЛУСФЕРОЙ (только с нужной стороны)
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_hemisphere(
    const Ray& ray,
    float3 center,
    float radius,
    float3 axis_direction,
    HitRecord& rec
) {
    float3 oc = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0f * dot(oc, ray.direction);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    
    if (discriminant < 0.0f) return false;
    
    float sqrtDisc = sqrtf(discriminant);
    float inv2a = 1.0f / (2.0f * a);
    float t1 = (-b - sqrtDisc) * inv2a;
    float t2 = (-b + sqrtDisc) * inv2a;
    
    for (int i = 0; i < 2; i++) {
        float t = (i == 0) ? t1 : t2;
        if (t < ray.t_min || t > ray.t_max) continue;
        
        float3 point = ray.origin + t * ray.direction;
        float3 normal = normalize(point - center);
        
        // Точка должна лежать на нужной стороне полусферы.
        if (dot(normal, axis_direction) < 0.0f) continue;
        
        rec.t = t;
        rec.hit = 1;
        rec.point = point;
        rec.normal = normal;
        
        if (dot(rec.normal, ray.direction) > 0.0f) {
            rec.normal = -rec.normal;
        }
        
        return true;
    }
    
    return false;
}

