#pragma once
#include "types.h"

// ---------------------------------------------------------
// Пересечение луча с плоскостью
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_plane(const Ray& ray, float3 normal, float d, HitRecord& rec) {
    float denom = dot(normal, ray.direction);
    if (fabsf(denom) > 1e-6f) {
        float t = -(dot(ray.origin, normal) + d) / denom;
        if (t >= ray.t_min && t <= ray.t_max) {
            rec.t = t;
            rec.hit = 1;
            rec.point = ray.origin + t * ray.direction;
            rec.normal = normal;
            rec.tangent = {0.0f, 0.0f, 0.0f};
            if (dot(rec.normal, ray.direction) > 0.0f) {
                rec.normal = -rec.normal;
            }
            return true;
        }
    }
    return false;
}

