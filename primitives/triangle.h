#pragma once
#include "types.h"

// ---------------------------------------------------------
// Пересечение луча с треугольником (Moller-Trumbore).
// Нормаль плоская, касательная — вдоль первого ребра.
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_triangle(const Ray& ray, const Triangle& tri, HitRecord& rec) {
    float3 e1 = tri.v1 - tri.v0;
    float3 e2 = tri.v2 - tri.v0;

    float3 pvec = cross(ray.direction, e2);
    float det = dot(e1, pvec);
    if (fabsf(det) < 1e-8f) return false;

    float inv_det = 1.0f / det;
    float3 tvec = ray.origin - tri.v0;
    float u = dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;

    float3 qvec = cross(tvec, e1);
    float v = dot(ray.direction, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = dot(e2, qvec) * inv_det;
    if (t < ray.t_min || t > ray.t_max) return false;

    rec.t = t;
    rec.hit = 1;
    rec.point = ray.origin + t * ray.direction;

    // Плавная нормаль: интерполяция нормалей вершин по барицентрике.
    // Если нормали не заданы (нули) — плоская по грани.
    float w = 1.0f - u - v;
    float3 nv = tri.n0 * w + tri.n1 * u + tri.n2 * v;
    float len2 = dot(nv, nv);
    if (len2 > 1e-12f) {
        rec.normal = normalize(nv);
    } else {
        rec.normal = normalize(cross(e1, e2));
    }
    rec.tangent = normalize(e1);
    rec.uv.x = tri.uv0.x * w + tri.uv1.x * u + tri.uv2.x * v;
    rec.uv.y = tri.uv0.y * w + tri.uv1.y * u + tri.uv2.y * v;
    if (dot(rec.normal, ray.direction) > 0.0f) rec.normal = -rec.normal;
    return true;
}
