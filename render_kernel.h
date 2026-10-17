#pragma once
#include "types.h"
#include "bvh.h"

__global__ void render_kernel(
    float4* output,
    int width,
    int height,
    CameraData camera,
    const BVHNode* bvh_nodes,
    const Object* objects,
    int num_objects,
    const BVHNode* strand_bvh_nodes,
    const SweepStrand* strands,
    const SweepPoint* sweep_points,
    int num_strands,
    const Plane* planes,
    int num_planes,
    const Material* materials,
    int num_materials,
    const Light* lights,
    int num_lights
);