#pragma once
#include "types.h"

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
    return true;
}

GPU_DEVICE inline bool intersect_plane(const Ray& ray, float3 normal, float d, HitRecord& rec) {
    float denom = dot(normal, ray.direction);
    if (fabsf(denom) > 1e-6f) {
        float t = -(dot(ray.origin, normal) + d) / denom;
        if (t >= ray.t_min && t <= ray.t_max) {
            rec.t = t;
            rec.hit = 1;
            rec.point = ray.origin + t * ray.direction;
            rec.normal = normal;
            // Убедимся, что нормаль смотрит на луч
            if (dot(rec.normal, ray.direction) > 0.0f) {
                rec.normal = -rec.normal;
            }
            return true;
        }
    }
    return false;
}

GPU_DEVICE inline bool intersect_scene(const Ray& ray, const Object* objects, int num_objects, HitRecord& rec) {
    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;
    
    for (int i = 0; i < num_objects; i++) {
        const Object& obj = objects[i];
        bool hit = false;
        
        if (obj.type == 0) { // Сфера
            hit = intersect_sphere(ray, obj.param1, obj.param2, temp_rec);
        } else if (obj.type == 1) { // Плоскость
            hit = intersect_plane(ray, obj.param1, obj.param2, temp_rec);
        }
        
        if (hit && temp_rec.t < closest_t) {
            rec = temp_rec;
            rec.material_id = obj.material_id;
            rec.object_id = i;
            closest_t = temp_rec.t;
            hit_anything = true;
        }
    }
    return hit_anything;
}

// Функция проверки тени (возвращает true, если свет ЗАБЛОКИРОВАН)
GPU_DEVICE inline bool is_in_shadow(const Ray& shadow_ray, const Object* objects, int num_objects) {
    HitRecord shadow_rec;
    // Нам не нужно знать детали попадания, только факт пересечения
    return intersect_scene(shadow_ray, objects, num_objects, shadow_rec);
}