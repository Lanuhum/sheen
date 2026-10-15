#pragma once
#include "types.h"

// ... (intersect_sphere остаётся без изменений) ...

// ---------------------------------------------------------
// Проверка пересечения со всеми объектами сцены
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_scene(
    const Ray& ray,
    const Object* objects,
    int num_objects,
    HitRecord& rec
) {
    bool hit_anything = false;
    float closest_t = ray.t_max;
    
    for (int i = 0; i < num_objects; i++) {
        const Object& obj = objects[i];
        
        if (obj.type == 0) { // Сфера
            HitRecord temp_rec;
            if (intersect_sphere(ray, obj.param1, obj.param2, temp_rec)) {
                if (temp_rec.t < closest_t) {
                    rec = temp_rec;
                    rec.material_id = obj.material_id;
                    rec.object_id = i;
                    closest_t = temp_rec.t;
                    hit_anything = true;
                }
            }
        }
        // Потом добавим другие типы (плоскость, тор и т.д.)
    }
    
    return hit_anything;
}