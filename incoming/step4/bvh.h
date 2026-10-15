#pragma once
#include "types.h"
#include <vector>
#include <algorithm>

// ---------------------------------------------------------
// Структура узла BVH (ровно 32 байта, идеально для GPU)
// ---------------------------------------------------------
struct alignas(16) BVHNode {
    float3 bbox_min;
    float3 bbox_max;
    int left_child;  // Если < 0, это лист. Индекс объекта = ~left_child
    int right_child;
};

// ---------------------------------------------------------
// Вспомогательные функции для CPU (построение BVH)
// ---------------------------------------------------------
inline void get_object_bounds(const Object& obj, float3& bmin, float3& bmax) {
    if (obj.type == 0) { // Сфера
        bmin = {obj.param1.x - obj.param2, obj.param1.y - obj.param2, obj.param1.z - obj.param2};
        bmax = {obj.param1.x + obj.param2, obj.param1.y + obj.param2, obj.param1.z + obj.param2};
    } else if (obj.type == 1) { // Плоскость (ограничиваем большим боксом для теста)
        bmin = {-100.0f, obj.param2 - 0.001f, -100.0f};
        bmax = { 100.0f, obj.param2 + 0.001f,  100.0f};
    }
}

inline float3 get_centroid(const float3& bmin, const float3& bmax) {
    return {(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
}

// Рекурсивный построитель BVH (простое разбиение по самой длинной оси)
inline int build_bvh_recursive(
    std::vector<BVHNode>& nodes,
    const std::vector<Object>& objects,
    std::vector<int>& object_indices,
    int start, int end
) {
    int node_idx = nodes.size();
    nodes.push_back(BVHNode()); // Резервируем место

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
        // Лист
        nodes[node_idx].left_child = ~object_indices[start];
        nodes[node_idx].right_child = ~object_indices[start];
        return node_idx;
    }

    // Находим самую длинную ось центроидов
    float3 extent = {cmax.x - cmin.x, cmax.y - cmin.y, cmax.z - cmin.z};
    int axis = 0;
    if (extent.y > extent.x && extent.y > extent.z) axis = 1;
    if (extent.z > extent.x && extent.z > extent.y) axis = 2;

    int mid = start + count / 2;
    if (axis == 0) {
        std::nth_element(object_indices.begin() + start, object_indices.begin() + mid, object_indices.begin() + end,
            [&](int a, int b) {
                float3 ca = get_centroid(get_object_bounds_temp(objects[a]).first, get_object_bounds_temp(objects[a]).second);
                float3 cb = get_centroid(get_object_bounds_temp(objects[b]).first, get_object_bounds_temp(objects[b]).second);
                return ca.x < cb.x;
            });
    } else if (axis == 1) {
        std::nth_element(object_indices.begin() + start, object_indices.begin() + mid, object_indices.begin() + end,
            [&](int a, int b) {
                float3 ca = get_centroid(get_object_bounds_temp(objects[a]).first, get_object_bounds_temp(objects[a]).second);
                float3 cb = get_centroid(get_object_bounds_temp(objects[b]).first, get_object_bounds_temp(objects[b]).second);
                return ca.y < cb.y;
            });
    } else {
        std::nth_element(object_indices.begin() + start, object_indices.begin() + mid, object_indices.begin() + end,
            [&](int a, int b) {
                float3 ca = get_centroid(get_object_bounds_temp(objects[a]).first, get_object_bounds_temp(objects[a]).second);
                float3 cb = get_centroid(get_object_bounds_temp(objects[b]).first, get_object_bounds_temp(objects[b]).second);
                return ca.z < cb.z;
            });
    }

    // Упрощённый лямбда-хелпер для nth_element (чтобы не дублировать код)
    // На самом деле, чтобы избежать ошибок компиляции с лямбдами в старом C++, сделаем проще:
    // Просто разделим массив пополам без сортировки для этого теста, 
    // так как главная цель - проверить обход, а не идеальный SAH.
    // (Для production нужен нормальный SAH, но для бенчмарка O(N) vs O(log N) хватит и mid-split)
    
    nodes[node_idx].left_child = build_bvh_recursive(nodes, objects, object_indices, start, mid);
    nodes[node_idx].right_child = build_bvh_recursive(nodes, objects, object_indices, mid, end);

    return node_idx;
}

// Хелпер для лямбды выше (выносим, чтобы компилятор не ругался)
inline std::pair<float3, float3> get_object_bounds_temp(const Object& obj) {
    float3 bmin, bmax;
    get_object_bounds(obj, bmin, bmax);
    return {bmin, bmax};
}

// ---------------------------------------------------------
// GPU Функции обхода BVH
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

GPU_DEVICE inline bool intersect_bvh(
    const Ray& ray,
    const BVHNode* bvh_nodes,
    const Object* objects,
    int num_objects,
    HitRecord& rec
) {
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0; // Начинаем с корня

    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const BVHNode& node = bvh_nodes[node_idx];

        if (intersect_aabb(ray, node.bbox_min, node.bbox_max)) {
            if (node.left_child < 0) { // Это лист
                int obj_idx = ~node.left_child;
                const Object& obj = objects[obj_idx];
                bool hit = false;
                
                if (obj.type == 0) hit = intersect_sphere(ray, obj.param1, obj.param2, temp_rec);
                else if (obj.type == 1) hit = intersect_plane(ray, obj.param1, obj.param2, temp_rec);

                if (hit && temp_rec.t < closest_t) {
                    rec = temp_rec;
                    rec.material_id = obj.material_id;
                    rec.object_id = obj_idx;
                    closest_t = temp_rec.t;
                    hit_anything = true;
                }
            } else { // Внутренний узел
                stack[stack_ptr++] = node.left_child;
                stack[stack_ptr++] = node.right_child;
            }
        }
    }
    return hit_anything;
}