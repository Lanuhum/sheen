#pragma once
#include "types.h"
#include "primitives/sphere.h"
#include "primitives/plane.h"
#include "primitives/cone.h"
#include "primitives/sweep.h"

// ---------------------------------------------------------
// Проверка пересечения ТОЛЬКО с массивом плоскостей
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_planes(
    const Ray& ray, 
    const Plane* planes, 
    int num_planes, 
    HitRecord& rec
) {
    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;
    
    for (int i = 0; i < num_planes; i++) {
        const Plane& p = planes[i];
        if (intersect_plane(ray, p.normal, p.d, temp_rec)) {
            if (temp_rec.t < closest_t) {
                rec = temp_rec;
                rec.material_id = p.material_id;
                rec.object_id = -1 - i;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
    }
    return hit_anything;
}
