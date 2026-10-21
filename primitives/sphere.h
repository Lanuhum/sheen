#pragma once
#include "types.h"

// ---------------------------------------------------------
// Интервальная версия: полные вход/выход луча через сферу.
// Нужна для CSG: даёт отрезок [t_enter, t_exit], а не ближайшую точку.
// ---------------------------------------------------------
GPU_DEVICE inline bool sphere_intervals(const Ray& ray, float3 center, float radius,
                                        float t0, float t1,
                                        float& t_enter, float& t_exit) {
    float3 oc = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0f * dot(oc, ray.direction);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return false;

    float sqrtDisc = sqrtf(discriminant);
    float ta = (-b - sqrtDisc) / (2.0f * a);
    float tb = (-b + sqrtDisc) / (2.0f * a);
    if (tb < t0 || ta > t1) return false;   // целиком вне [t0,t1]
    t_enter = fmaxf(ta, t0);
    t_exit  = fminf(tb, t1);
    return t_enter <= t_exit;
}

// ---------------------------------------------------------
// Пересечение луча со сферой
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_sphere(const Ray& ray, float3 center, float radius, HitRecord& rec) {
    float3 oc = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0f * dot(oc, ray.direction);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    
    if (discriminant < 0.0f) return false;
    
    float sqrtDisc = sqrtf(discriminant);
    float t = (-b - sqrtDisc) / (2.0f * a);
    
    if (t < ray.t_min || t > ray.t_max) {
        t = (-b + sqrtDisc) / (2.0f * a);
        if (t < ray.t_min || t > ray.t_max) return false;
    }
    
    rec.t = t;
    rec.hit = 1;
    rec.point = ray.origin + t * ray.direction;
    rec.normal = normalize(rec.point - center);
    rec.tangent = {0.0f, 0.0f, 0.0f};
    return true;
}

