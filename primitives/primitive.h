#pragma once
#include "types.h"
#include "primitives/sphere.h"
#include "primitives/plane.h"
#include "primitives/cone.h"
#include "primitives/sweep.h"

// ---------------------------------------------------------
// Общий интерфейс примитивов.
// Примитив — это (kind, index) в массивах сцены. Логика обхода
// (BVH, порядок проверки) живёт выше, в integrator/, а здесь — только
// «дай пересечение с этим примитивом».
// ---------------------------------------------------------
enum PrimKind {
    PRIM_SPHERE = 0,
    PRIM_PLANE  = 1,
    PRIM_SWEEP  = 2
};

struct PrimRef {
    int kind;
    int index;
};

// Данные сцены, нужные примитивам. Никакой логики обхода внутри.
struct SceneView {
    const Object*      objects;
    const Plane*       planes;
    const SweepStrand* strands;
    const SweepPoint*  sweep_points;
};

GPU_DEVICE inline float3 prim_hit_point(const PrimRef& prim, const Ray& ray,
                                        const SceneView& view, HitRecord& rec) {
    (void)prim; (void)view;
    return ray.origin + rec.t * ray.direction;
}

GPU_DEVICE inline bool intersect_primitive(
    const PrimRef& prim, const Ray& ray, const SceneView& view, HitRecord& rec
) {
    switch (prim.kind) {
        case PRIM_SPHERE: {
            const Object& o = view.objects[prim.index];
            if (!intersect_sphere(ray, o.param1, o.param2, rec)) return false;
            rec.material_id = o.material_id;
            rec.object_id   = prim.index;
            return true;
        }
        case PRIM_PLANE: {
            const Plane& p = view.planes[prim.index];
            if (!intersect_plane(ray, p.normal, p.d, rec)) return false;
            rec.material_id = p.material_id;
            rec.object_id   = -1 - prim.index;
            return true;
        }
        case PRIM_SWEEP: {
            const SweepStrand& s = view.strands[prim.index];
            if (!intersect_sphere_sweep(ray, s, view.sweep_points, rec)) return false;
            rec.material_id = s.material_id;
            rec.object_id   = -(prim.index + 1);
            return true;
        }
        default:
            return false;
    }
}
