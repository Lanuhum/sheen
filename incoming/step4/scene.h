#pragma once
#include "types.h"
#include "bvh.h"
#include <vector>

struct Scene {
    std::vector<Object> objects;
    std::vector<Material> materials;
    std::vector<Light> lights;
    CameraData camera;
    std::vector<BVHNode> bvh_nodes; // Новое поле

    void add_sphere(float3 center, float radius, int material_id) {
        Object obj;
        obj.type = 0;
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

    // Построить BVH после добавления всех объектов
    void build_bvh() {
        std::vector<int> indices(objects.size());
        for (size_t i = 0; i < objects.size(); i++) indices[i] = i;
        
        bvh_nodes.clear();
        if (!objects.empty()) {
            build_bvh_recursive(bvh_nodes, objects, indices, 0, objects.size());
        }
    }
};