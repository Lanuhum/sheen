#pragma once
#include "types.h"

__global__ void render_kernel(
    float4* output,
    int width,
    int height,
    CameraData camera,
    const Object* objects,
    int num_objects,
    const Material* materials,
    const Light* lights,
    int num_lights
);