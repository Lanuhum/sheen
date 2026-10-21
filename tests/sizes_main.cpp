#include <cstdio>
#include <cstddef>
#include "types.h"

static_assert(sizeof(Transform) == 48, "Transform must be 48 bytes");
static_assert(sizeof(Ray) == 32, "Ray must be 32 bytes");
static_assert(sizeof(HitRecord) == 80, "HitRecord must be 80 bytes");
static_assert(sizeof(CameraData) == 64, "CameraData must be 64 bytes");
static_assert(sizeof(Material) == 112, "Material must be 112 bytes");
static_assert(offsetof(Material, emission_color) == 72, "Material emission_color offset");
static_assert(offsetof(Material, emission) == 84, "Material emission offset");
static_assert(offsetof(Material, mask_id) == 88, "Material mask_id offset");
static_assert(offsetof(Material, normal_id) == 92, "Material normal_id offset");
static_assert(offsetof(Material, normal_strength) == 96, "Material normal_strength offset");
static_assert(sizeof(HeightField) == 64, "HeightField must be 64 bytes");
static_assert(alignof(HeightField) == 16, "HeightField must be 16-byte aligned");

static_assert(alignof(Transform) == 16, "Transform must be 16-byte aligned");
static_assert(alignof(Ray) == 16, "Ray must be 16-byte aligned");
static_assert(alignof(HitRecord) == 16, "HitRecord must be 16-byte aligned");
static_assert(alignof(CameraData) == 16, "CameraData must be 16-byte aligned");

#define SHOW_SIZE(T) printf("sizeof(%-11s) = %3zu   alignof = %3zu\n", #T, sizeof(T), alignof(T))
#define SHOW_OFF(T, F) printf("    offsetof(%-11s, %-12s) = %3zu\n", #T, #F, offsetof(T, F))

int main()
{
    printf("=== sizes ===\n");
    SHOW_SIZE(Transform);
    SHOW_SIZE(Ray);
    SHOW_SIZE(HitRecord);
    SHOW_SIZE(CameraData);
    SHOW_SIZE(SceneData);
    SHOW_SIZE(float3);

    printf("=== layout ===\n");
    SHOW_OFF(Transform, m);
    SHOW_OFF(Ray, origin);
    SHOW_OFF(Ray, direction);
    SHOW_OFF(Ray, t_min);
    SHOW_OFF(Ray, t_max);
    SHOW_OFF(HitRecord, t);
    SHOW_OFF(HitRecord, material_id);
    SHOW_OFF(HitRecord, object_id);
    SHOW_OFF(HitRecord, hit);
    SHOW_OFF(HitRecord, point);
    SHOW_OFF(HitRecord, normal);
    SHOW_OFF(HitRecord, tangent);
    SHOW_OFF(HitRecord, _pad0);
    SHOW_OFF(CameraData, position);
    SHOW_OFF(CameraData, right);
    SHOW_OFF(CameraData, up);
    SHOW_OFF(CameraData, forward);
    SHOW_OFF(CameraData, focal_length);
    SHOW_OFF(CameraData, sensor_width);
    SHOW_OFF(CameraData, shift_x);
    SHOW_OFF(CameraData, shift_y);

    printf("=== padding ===\n");
    printf("HitRecord tail           = %zu\n",
           sizeof(HitRecord) - (offsetof(HitRecord, _pad0) + sizeof(float)));
    printf("Ray tail                 = %zu\n",
           sizeof(Ray) - (offsetof(Ray, t_max) + sizeof(float)));
    printf("Transform tail           = %zu\n",
           sizeof(Transform) - 12 * sizeof(float));
    printf("CameraData tail          = %zu\n",
           sizeof(CameraData) - (offsetof(CameraData, shift_y) + sizeof(float)));
    printf("HitRecord %% 16           = %zu\n", sizeof(HitRecord) % 16);
    printf("Ray %% 16                 = %zu\n", sizeof(Ray) % 16);
    printf("Transform %% 16           = %zu\n", sizeof(Transform) % 16);

    printf("=== device helper ===\n");
    printf("types.h helpers are device-only, checked in a kernel, not here\n");

    printf("ALL STATIC_ASSERTS PASSED\n");
    return 0;
}
