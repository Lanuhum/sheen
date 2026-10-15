#pragma once
#include "types.h"
#include <vector>

// Структура для хранения сцены на CPU
struct Scene {
    std::vector<Object> objects;
    std::vector<Material> materials;
    std::vector<Light> lights;
    CameraData camera;
    
    // Методы для добавления объектов
    void add_sphere(float3 center, float radius, int material_id) {
        Object obj;
        obj.type = 0; // сфера
        obj.material_id = material_id;
        obj.param1 = center;
        obj.param2 = radius;
        objects.push_back(obj);
    }
    
    void add_material(float3 color, float specular, float shininess, float ambient) {
        Material mat;
        mat.diffuse_color = color;
        mat.specular = specular;
        mat.shininess = shininess;
        mat.ambient = ambient;
        materials.push_back(mat);
    }
    
    void add_directional_light(float3 direction, float3 color, float intensity) {
        Light light;
        light.direction = normalize(direction);
        light.color = color;
        light.intensity = intensity;
        lights.push_back(light);
    }
};