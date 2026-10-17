#pragma once
#include "types.h"
#include "intersect.h"
#include <vector>
#include <algorithm>
#include <utility>

struct alignas(16) BVHNode {
    float3 bbox_min;
    float3 bbox_max;
    int left_child;
    int right_child;
};

// ---------------------------------------------------------
// Границы объекта (для сфер и прядей)
// ---------------------------------------------------------
inline void get_object_bounds(const Object& obj, float3& bmin, float3& bmax) {
    if (obj.type == 0) { // Сфера
        bmin = {obj.param1.x - obj.param2, obj.param1.y - obj.param2, obj.param1.z - obj.param2};
        bmax = {obj.param1.x + obj.param2, obj.param1.y + obj.param2, obj.param1.z + obj.param2};
    } else if (obj.type == 2) { // sphere_sweep
        // obj.param1 содержит индекс пряди (как float, переинтерпретируем как int)
        // Но это неудобно. Используем отдельный механизм — см. ниже
        // Пока заглушка — для сфер
        bmin = {0, 0, 0};
        bmax = {0, 0, 0};
    }
}

// ---------------------------------------------------------
// Границы пряди (отдельная функция, т.к. нужны sweep_points)
// ---------------------------------------------------------
inline void get_strand_bounds(
    const SweepStrand& strand,
    const SweepPoint* points,
    float3& bmin,
    float3& bmax
) {
    bmin = {1e10f, 1e10f, 1e10f};
    bmax = {-1e10f, -1e10f, -1e10f};
    
    int base = strand.point_offset;
    int count = strand.point_count;
    
    for (int i = 0; i < count; i++) {
        const SweepPoint& p = points[base + i];
        float r = p.radius;
        bmin = {fminf(bmin.x, p.position.x - r),
                fminf(bmin.y, p.position.y - r),
                fminf(bmin.z, p.position.z - r)};
        bmax = {fmaxf(bmax.x, p.position.x + r),
                fmaxf(bmax.y, p.position.y + r),
                fmaxf(bmax.z, p.position.z + r)};
    }
}

inline float3 get_centroid(const float3& bmin, const float3& bmax) {
    return {(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
}

// ---------------------------------------------------------
// Построение BVH для сфер
// ---------------------------------------------------------
inline int build_bvh_recursive(
    std::vector<BVHNode>& nodes,
    const std::vector<Object>& objects,
    std::vector<int>& object_indices,
    int start, int end
) {
    int node_idx = nodes.size();
    nodes.push_back(BVHNode());

    float3 bmin = {1e10f, 1e10f, 1e10f};
    float3 bmax = {-1e10f, -1e10f, -1e10f};
    float3 cmin = {1e10f, 1e10f, 1e10f};
    float3 cmax = {-1e10f, -1e10f, -1e10f};

    for (int i = start; i < end; i++) {
        float3 obj_bmin, obj_bmax;
        get_object_bounds(objects[object_indices[i]], obj_bmin, obj_bmax);
        bmin = {fminf(bmin.x, obj_bmin.x), fminf(bmin.y, obj_bmin.y), fminf(bmin.z, obj_bmin.z)};
        bmax = {fmaxf(bmax.x, obj_bmax.x), fmaxf(bmax.y, obj_bmax.y), fmaxf(bmax.z, obj_bmax.z)};
        
        float3 c = get_centroid(obj_bmin, obj_bmax);
        cmin = {fminf(cmin.x, c.x), fminf(cmin.y, c.y), fminf(cmin.z, c.z)};
        cmax = {fmaxf(cmax.x, c.x), fmaxf(cmax.y, c.y), fmaxf(cmax.z, c.z)};
    }

    nodes[node_idx].bbox_min = bmin;
    nodes[node_idx].bbox_max = bmax;

    int count = end - start;
    if (count == 1) {
        nodes[node_idx].left_child = ~object_indices[start];
        nodes[node_idx].right_child = ~object_indices[start];
        return node_idx;
    }

    float3 extent = {cmax.x - cmin.x, cmax.y - cmin.y, cmax.z - cmin.z};
    int axis = 0;
    if (extent.y > extent.x && extent.y > extent.z) axis = 1;
    if (extent.z > extent.x && extent.z > extent.y) axis = 2;

    int mid = start + count / 2;
    if (axis == 0) {
        std::nth_element(object_indices.begin() + start, object_indices.begin() + mid, object_indices.begin() + end,
            [&](int a, int b) {
                float3 obj_bmin_a, obj_bmax_a, obj_bmin_b, obj_bmax_b;
                get_object_bounds(objects[a], obj_bmin_a, obj_bmax_a);
                get_object_bounds(objects[b], obj_bmin_b, obj_bmax_b);
                return get_centroid(obj_bmin_a, obj_bmax_a).x < get_centroid(obj_bmin_b, obj_bmax_b).x;
            });
    } else if (axis == 1) {
        std::nth_element(object_indices.begin() + start, object_indices.begin() + mid, object_indices.begin() + end,
            [&](int a, int b) {
                float3 obj_bmin_a, obj_bmax_a, obj_bmin_b, obj_bmax_b;
                get_object_bounds(objects[a], obj_bmin_a, obj_bmax_a);
                get_object_bounds(objects[b], obj_bmin_b, obj_bmax_b);
                return get_centroid(obj_bmin_a, obj_bmax_a).y < get_centroid(obj_bmin_b, obj_bmax_b).y;
            });
    } else {
        std::nth_element(object_indices.begin() + start, object_indices.begin() + mid, object_indices.begin() + end,
            [&](int a, int b) {
                float3 obj_bmin_a, obj_bmax_a, obj_bmin_b, obj_bmax_b;
                get_object_bounds(objects[a], obj_bmin_a, obj_bmax_a);
                get_object_bounds(objects[b], obj_bmin_b, obj_bmax_b);
                return get_centroid(obj_bmin_a, obj_bmax_a).z < get_centroid(obj_bmin_b, obj_bmax_b).z;
            });
    }

    nodes[node_idx].left_child = build_bvh_recursive(nodes, objects, object_indices, start, mid);
    nodes[node_idx].right_child = build_bvh_recursive(nodes, objects, object_indices, mid, end);

    return node_idx;
}

// ---------------------------------------------------------
// Построение отдельного BVH для прядей
// ---------------------------------------------------------
inline int build_strand_bvh_recursive(
    std::vector<BVHNode>& nodes,
    const std::vector<SweepStrand>& strands,
    const SweepPoint* points,
    std::vector<int>& strand_indices,
    int start, int end
) {
    int node_idx = nodes.size();
    nodes.push_back(BVHNode());

    float3 bmin = {1e10f, 1e10f, 1e10f};
    float3 bmax = {-1e10f, -1e10f, -1e10f};
    float3 cmin = {1e10f, 1e10f, 1e10f};
    float3 cmax = {-1e10f, -1e10f, -1e10f};

    for (int i = start; i < end; i++) {
        float3 s_bmin, s_bmax;
        get_strand_bounds(strands[strand_indices[i]], points, s_bmin, s_bmax);
        bmin = {fminf(bmin.x, s_bmin.x), fminf(bmin.y, s_bmin.y), fminf(bmin.z, s_bmin.z)};
        bmax = {fmaxf(bmax.x, s_bmax.x), fmaxf(bmax.y, s_bmax.y), fmaxf(bmax.z, s_bmax.z)};
        
        float3 c = get_centroid(s_bmin, s_bmax);
        cmin = {fminf(cmin.x, c.x), fminf(cmin.y, c.y), fminf(cmin.z, c.z)};
        cmax = {fmaxf(cmax.x, c.x), fmaxf(cmax.y, c.y), fmaxf(cmax.z, c.z)};
    }

    nodes[node_idx].bbox_min = bmin;
    nodes[node_idx].bbox_max = bmax;

    int count = end - start;
    if (count == 1) {
        // Лист: кодируем как ~strand_index, но со сдвигом, чтобы не путать с объектами
        // Используем отрицательное значение с флагом: -(strand_index + 1)
        // Но это конфликтует с ~object_index. Используем другой трюк:
        // left_child = -(strand_index + 1) для прядей
        // Для объектов: left_child = ~object_index = -(object_index + 1)
        // Конфликт! Нужен отдельный BVH для прядей.
        // Решаем: strand_bvh_nodes — отдельный массив, и left_child = ~strand_index
        nodes[node_idx].left_child = ~strand_indices[start];
        nodes[node_idx].right_child = ~strand_indices[start];
        return node_idx;
    }

    float3 extent = {cmax.x - cmin.x, cmax.y - cmin.y, cmax.z - cmin.z};
    int axis = 0;
    if (extent.y > extent.x && extent.y > extent.z) axis = 1;
    if (extent.z > extent.x && extent.z > extent.y) axis = 2;

    int mid = start + count / 2;
    if (axis == 0) {
        std::nth_element(strand_indices.begin() + start, strand_indices.begin() + mid, strand_indices.begin() + end,
            [&](int a, int b) {
                float3 bmin_a, bmax_a, bmin_b, bmax_b;
                get_strand_bounds(strands[a], points, bmin_a, bmax_a);
                get_strand_bounds(strands[b], points, bmin_b, bmax_b);
                return get_centroid(bmin_a, bmax_a).x < get_centroid(bmin_b, bmax_b).x;
            });
    } else if (axis == 1) {
        std::nth_element(strand_indices.begin() + start, strand_indices.begin() + mid, strand_indices.begin() + end,
            [&](int a, int b) {
                float3 bmin_a, bmax_a, bmin_b, bmax_b;
                get_strand_bounds(strands[a], points, bmin_a, bmax_a);
                get_strand_bounds(strands[b], points, bmin_b, bmax_b);
                return get_centroid(bmin_a, bmax_a).y < get_centroid(bmin_b, bmax_b).y;
            });
    } else {
        std::nth_element(strand_indices.begin() + start, strand_indices.begin() + mid, strand_indices.begin() + end,
            [&](int a, int b) {
                float3 bmin_a, bmax_a, bmin_b, bmax_b;
                get_strand_bounds(strands[a], points, bmin_a, bmax_a);
                get_strand_bounds(strands[b], points, bmin_b, bmax_b);
                return get_centroid(bmin_a, bmax_a).z < get_centroid(bmin_b, bmax_b).z;
            });
    }

    nodes[node_idx].left_child = build_strand_bvh_recursive(nodes, strands, points, strand_indices, start, mid);
    nodes[node_idx].right_child = build_strand_bvh_recursive(nodes, strands, points, strand_indices, mid, end);

    return node_idx;
}

// ---------------------------------------------------------
// Пересечение с AABB
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_aabb(const Ray& ray, float3 bbox_min, float3 bbox_max) {
    float invDirX = 1.0f / ray.direction.x;
    float tminX = (bbox_min.x - ray.origin.x) * invDirX;
    float tmaxX = (bbox_max.x - ray.origin.x) * invDirX;
    if (tminX > tmaxX) { float tmp = tminX; tminX = tmaxX; tmaxX = tmp; }

    float invDirY = 1.0f / ray.direction.y;
    float tminY = (bbox_min.y - ray.origin.y) * invDirY;
    float tmaxY = (bbox_max.y - ray.origin.y) * invDirY;
    if (tminY > tmaxY) { float tmp = tminY; tminY = tmaxY; tmaxY = tmp; }

    if (tminX > tmaxY || tminY > tmaxX) return false;
    if (tminY > tminX) tminX = tminY;
    if (tmaxY < tmaxX) tmaxX = tmaxY;

    float invDirZ = 1.0f / ray.direction.z;
    float tminZ = (bbox_min.z - ray.origin.z) * invDirZ;
    float tmaxZ = (bbox_max.z - ray.origin.z) * invDirZ;
    if (tminZ > tmaxZ) { float tmp = tminZ; tminZ = tmaxZ; tmaxZ = tmp; }

    if (tminX > tmaxZ || tminZ > tmaxX) return false;
    return (tmaxX >= ray.t_min) && (tminX <= ray.t_max);
}

// ---------------------------------------------------------
// Обход BVH для сфер
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_bvh(
    const Ray& ray,
    const BVHNode* bvh_nodes,
    const Object* objects,
    int num_objects,
    HitRecord& rec
) {
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const BVHNode& node = bvh_nodes[node_idx];

        if (intersect_aabb(ray, node.bbox_min, node.bbox_max)) {
            if (node.left_child < 0) {
                int obj_idx = ~node.left_child;
                const Object& obj = objects[obj_idx];
                bool hit = false;
                
                if (obj.type == 0) {
                    hit = intersect_sphere(ray, obj.param1, obj.param2, temp_rec);
                }

                if (hit && temp_rec.t < closest_t) {
                    rec = temp_rec;
                    rec.material_id = obj.material_id;
                    rec.object_id = obj_idx;
                    closest_t = temp_rec.t;
                    hit_anything = true;
                }
            } else {
                stack[stack_ptr++] = node.left_child;
                stack[stack_ptr++] = node.right_child;
            }
        }
    }
    return hit_anything;
}

// ---------------------------------------------------------
// Обход BVH для прядей
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_strand_bvh(
    const Ray& ray,
    const BVHNode* bvh_nodes,
    const SweepStrand* strands,
    const SweepPoint* points,
    int num_strands,
    HitRecord& rec
) {
    if (num_strands == 0) return false;
    
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const BVHNode& node = bvh_nodes[node_idx];

        if (intersect_aabb(ray, node.bbox_min, node.bbox_max)) {
            if (node.left_child < 0) {
                int strand_idx = ~node.left_child;
                const SweepStrand& strand = strands[strand_idx];
                
                if (intersect_sphere_sweep(ray, strand, points, temp_rec)) {
                    if (temp_rec.t < closest_t) {
                        rec = temp_rec;
                        rec.object_id = -(strand_idx + 1);  // отрицательный ID для прядей
                        closest_t = temp_rec.t;
                        hit_anything = true;
                    }
                }
            } else {
                stack[stack_ptr++] = node.left_child;
                stack[stack_ptr++] = node.right_child;
            }
        }
    }
    return hit_anything;
}