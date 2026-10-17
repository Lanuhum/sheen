#pragma once
#include "types.h"
#include "bvh.h"
#include <vector>

struct Scene {
    std::vector<Object> objects;
    std::vector<Plane> planes;
    std::vector<SweepPoint> sweep_points;
    std::vector<SweepStrand> sweep_strands;
    std::vector<Material> materials;
    std::vector<Light> lights;
    CameraData camera;
    std::vector<BVHNode> bvh_nodes;
    std::vector<BVHNode> strand_bvh_nodes;

    void add_sphere(float3 center, float radius, int material_id) {
        Object obj;
        obj.type = 0;
        obj.material_id = material_id;
        obj.param1 = center;
        obj.param2 = radius;
        objects.push_back(obj);
    }
    
    void add_plane(float3 normal, float d, int material_id) {
        Plane p;
        p.normal = normal;
        p.d = d;
        p.material_id = material_id;
        planes.push_back(p);
    }
    
    // Добавляет прядь: принимает вектор точек (position + radius)
    void add_sphere_sweep(const std::vector<SweepPoint>& pts, int material_id) {
        SweepStrand strand;
        strand.point_offset = (int)sweep_points.size();
        strand.point_count = (int)pts.size();
        strand.material_id = material_id;
        
        for (const auto& p : pts) {
            sweep_points.push_back(p);
        }
        sweep_strands.push_back(strand);
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

    void build_bvh() {
        // BVH для сфер
        std::vector<int> sphere_indices;
        for (size_t i = 0; i < objects.size(); i++) {
            if (objects[i].type == 0) {
                sphere_indices.push_back(i);
            }
        }
        bvh_nodes.clear();
        if (!sphere_indices.empty()) {
            build_bvh_recursive(bvh_nodes, objects, sphere_indices, 0, sphere_indices.size());
        }
        
        // BVH для прядей
        std::vector<int> strand_indices;
        for (size_t i = 0; i < sweep_strands.size(); i++) {
            strand_indices.push_back(i);
        }
        strand_bvh_nodes.clear();
        if (!strand_indices.empty()) {
            build_strand_bvh_recursive(
                strand_bvh_nodes, sweep_strands, sweep_points.data(),
                strand_indices, 0, strand_indices.size()
            );
        }
    }
};