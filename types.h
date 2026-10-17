#pragma once
#include "gpu_compat.h"

// ---------------------------------------------------------
// 1. Transform
// ---------------------------------------------------------
struct alignas(16) Transform {
    float m[12]; 
    GPU_HOST_DEVICE inline float3 transform_point(float3 p) const {
        return {m[0]*p.x + m[1]*p.y + m[2]*p.z + m[3],
                m[4]*p.x + m[5]*p.y + m[6]*p.z + m[7],
                m[8]*p.x + m[9]*p.y + m[10]*p.z + m[11]};
    }
    GPU_HOST_DEVICE inline float3 transform_vector(float3 v) const {
        return {m[0]*v.x + m[1]*v.y + m[2]*v.z,
                m[4]*v.x + m[5]*v.y + m[6]*v.z,
                m[8]*v.x + m[9]*v.y + m[10]*v.z};
    }
};

// ---------------------------------------------------------
// 2. Ray
// ---------------------------------------------------------
struct alignas(16) Ray {
    float3 origin;
    float3 direction;
    float  t_min;
    float  t_max;
};

// ---------------------------------------------------------
// 3. HitRecord
// ---------------------------------------------------------
struct alignas(16) HitRecord {
    float  t;
    int    material_id;
    int    object_id;
    int    hit;
    float3 point;
    float3 normal;
    float  _pad0;
    float  _pad1;
};

// ---------------------------------------------------------
// 4. CameraData
// ---------------------------------------------------------
struct alignas(16) CameraData {
    float3 position;
    float3 right;
    float3 up;
    float3 forward;
    float  focal_length;
    float  sensor_width;
    float  shift_x;
    float  shift_y;
};

// ---------------------------------------------------------
// 5. Material
// ---------------------------------------------------------
struct alignas(16) Material {
    float3 diffuse_color;
    float  specular;
    float  shininess;
    float  ambient;
    int    _pad0;
};

// ---------------------------------------------------------
// 6. Object (сферы, type == 0)
// ---------------------------------------------------------
struct alignas(16) Object {
    int    type;
    int    material_id;
    float3 param1;
    float  param2;
    float  _pad0, _pad1, _pad2;
};

// ---------------------------------------------------------
// 7. Light
// ---------------------------------------------------------
struct alignas(16) Light {
    float3 direction;
    float3 color;
    float  intensity;
    int    _pad0;
};

// ---------------------------------------------------------
// 8. Plane (отдельно от BVH)
// ---------------------------------------------------------
struct alignas(16) Plane {
    float3 normal;
    float  d;
    int    material_id;
    int    _pad0, _pad1, _pad2;
};

// ---------------------------------------------------------
// 9. SweepPoint (одна контрольная точка пряди, 16 байт)
// ---------------------------------------------------------
struct alignas(16) SweepPoint {
    float3 position;  // 12 bytes
    float  radius;    // 4 bytes
};

// ---------------------------------------------------------
// 10. SweepStrand (описание одной пряди, 32 байта)
// ---------------------------------------------------------
struct alignas(16) SweepStrand {
    int point_offset;   // индекс начала в массиве sweep_points[]
    int point_count;    // количество точек в этой пряди
    int material_id;
    int _pad0;
};

// ---------------------------------------------------------
// 11. SceneData
// ---------------------------------------------------------
struct SceneData {
    int num_objects;
    int num_materials;
    int num_lights;
};

// ---------------------------------------------------------
// Вспомогательные функции
// ---------------------------------------------------------
GPU_HOST_DEVICE inline float dot(float3 a, float3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

GPU_HOST_DEVICE inline float3 normalize(float3 v) {
    float invLen = rsqrtf(dot(v, v));
    return {v.x * invLen, v.y * invLen, v.z * invLen};
}

GPU_HOST_DEVICE inline float3 operator+(float3 a, float3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
GPU_HOST_DEVICE inline float3 operator-(float3 a, float3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
GPU_HOST_DEVICE inline float3 operator-(float3 a) { return {-a.x, -a.y, -a.z}; }
GPU_HOST_DEVICE inline float3 operator*(float3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
GPU_HOST_DEVICE inline float3 operator*(float b, float3 a) { return {a.x*b, a.y*b, a.z*b}; }
GPU_HOST_DEVICE inline float3 operator*(float3 a, float3 b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
GPU_HOST_DEVICE inline float3 operator/(float3 a, float b) { return {a.x/b, a.y/b, a.z/b}; }
GPU_HOST_DEVICE inline float3 operator/(float3 a, float3 b) { return {a.x/b.x, a.y/b.y, a.z/b.z}; }
GPU_HOST_DEVICE inline float3& operator+=(float3& a, float3 b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
GPU_HOST_DEVICE inline float3& operator-=(float3& a, float3 b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; return a; }
GPU_HOST_DEVICE inline float3& operator*=(float3& a, float b) { a.x*=b; a.y*=b; a.z*=b; return a; }