#pragma once
#include "types.h"
#include "bvh.h"
#include "primitives/primitive.h"
#include "integrator/csg.h"

// ---------------------------------------------------------
// Обход сцены: BVH сфер -> BVH прядей -> плоскости.
// Порядок сохранён как был в render_kernel до выноса: первая
// категория, давшая попадание, выигрывает (не поиск ближайшего).
// Аргументы передаются явно: структура-контекст заставляла
// компилятор держать её в локальной памяти и роняла занятость SM.
// ---------------------------------------------------------
GPU_DEVICE __forceinline__ bool scene_intersect(
    const Ray& ray,
    const BVHNode* bvh_nodes, const Object* objects, int num_objects,
    const BVHNode* strand_bvh_nodes, const SweepStrand* strands,
    const SweepPoint* sweep_points, int num_strands,
    const BVHNode* mesh_bvh_nodes, const Triangle* triangles, int num_triangles,
    const BVHNode* hf_bvh_nodes, const HeightField* heightfields, int num_heightfields,
    const BVHNode* se_bvh_nodes, const Superellipsoid* superellipsoids, int num_superellipsoids,
    const BVHNode* lathe_bvh_nodes, const Lathe* lathes, const LathePoint* lathe_points, int num_lathes,
    const BVHNode* prism_bvh_nodes, const Prism* prisms, const PrismPoint* prism_points, int num_prisms,
    const CSGNode* csg_nodes, int csg_root,
    const Plane* planes, int num_planes,
    HitRecord& rec
) {
    // Ближайшее попадание по всем категориям, а не первая сработавшая.
    float best = ray.t_max;
    bool hit = false;

    HitRecord tmp;
    if (intersect_bvh(ray, bvh_nodes, objects, num_objects, tmp) && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (intersect_strand_bvh(ray, strand_bvh_nodes, strands, sweep_points, num_strands, tmp)
        && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (intersect_mesh_bvh(ray, mesh_bvh_nodes, triangles, num_triangles, tmp)
        && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (intersect_hf_bvh(ray, hf_bvh_nodes, heightfields, num_heightfields, tmp)
        && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (intersect_se_bvh(ray, se_bvh_nodes, superellipsoids, num_superellipsoids, tmp)
        && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (intersect_lathe_bvh(ray, lathe_bvh_nodes, lathes, lathe_points, num_lathes, tmp)
        && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (intersect_prism_bvh(ray, prism_bvh_nodes, prisms, prism_points, num_prisms, tmp)
        && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    if (csg_root >= 0) {
        if (intersect_csg(ray, csg_nodes, csg_root, objects, superellipsoids, heightfields,
                          ray.t_min, ray.t_max, tmp) && tmp.t < best) {
            best = tmp.t; rec = tmp; hit = true;
        }
    }
    if (intersect_planes(ray, planes, num_planes, tmp) && tmp.t < best) {
        best = tmp.t; rec = tmp; hit = true;
    }
    return hit;
}

// Обход только непрозрачной геометрии (сферы, меш, HF, SE, CSG, плоскости).
// Пряди сюда не входят: они считаются полупрозрачной средой.
GPU_DEVICE __forceinline__ bool scene_intersect_opaque(
    const Ray& ray,
    const BVHNode* bvh_nodes, const Object* objects, int num_objects,
    const BVHNode* mesh_bvh_nodes, const Triangle* triangles, int num_triangles,
    const BVHNode* hf_bvh_nodes, const HeightField* heightfields, int num_heightfields,
    const BVHNode* se_bvh_nodes, const Superellipsoid* superellipsoids, int num_superellipsoids,
    const BVHNode* lathe_bvh_nodes, const Lathe* lathes, const LathePoint* lathe_points, int num_lathes,
    const BVHNode* prism_bvh_nodes, const Prism* prisms, const PrismPoint* prism_points, int num_prisms,
    const CSGNode* csg_nodes, int csg_root,
    const Plane* planes, int num_planes,
    HitRecord& rec
) {
    if (intersect_bvh(ray, bvh_nodes, objects, num_objects, rec)) return true;
    if (intersect_mesh_bvh(ray, mesh_bvh_nodes, triangles, num_triangles, rec)) return true;
    if (intersect_hf_bvh(ray, hf_bvh_nodes, heightfields, num_heightfields, rec)) return true;
    if (intersect_se_bvh(ray, se_bvh_nodes, superellipsoids, num_superellipsoids, rec)) return true;
    if (intersect_lathe_bvh(ray, lathe_bvh_nodes, lathes, lathe_points, num_lathes, rec)) return true;
    if (intersect_prism_bvh(ray, prism_bvh_nodes, prisms, prism_points, num_prisms, rec)) return true;
    if (csg_root >= 0) {
        if (intersect_csg(ray, csg_nodes, csg_root, objects, superellipsoids, heightfields,
                          ray.t_min, ray.t_max, rec)) return true;
    }
    return intersect_planes(ray, planes, num_planes, rec);
}
