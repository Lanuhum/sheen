// Sheen FLAT-контракт: POD-структуры без зависимости от CUDA.
// Бит-в-бит совпадают с types.h (проверено: Object=48, Material=96,
// CameraData=64, Triangle=112, Plane=32, Light=96, SweepPoint=16,
// SweepStrand=16, Medium=48, HeightField=64).
// Нужен там, где нельзя (или не хочется) тащить cuda_runtime.h:
// shell_py_module.cpp, ctypes-контракт — единственный источник истины.
#ifndef SHEEN_FLAT_H
#define SHEEN_FLAT_H

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

struct SFloat2 { float u, v; };
struct SFloat3 { float x, y, z; };

struct alignas(16) SCameraData {
    SFloat3 position;
    SFloat3 right;
    SFloat3 up;
    SFloat3 forward;
    float  focal_length;
    float  sensor_width;
    float  shift_x;
    float  shift_y;
};

struct alignas(16) SObject {
    int    type;
    int    material_id;
    SFloat3 param1;
    float  param2;
    float  _pad0, _pad1, _pad2;
};

struct alignas(16) SSweepStrand {
    int point_offset;
    int point_count;
    int material_id;
    int _pad0;
};

struct alignas(16) SSweepPoint {
    SFloat3 position;
    float  radius;
};

struct alignas(16) STriangle {
    SFloat3 v0, v1, v2;
    SFloat3 n0, n1, n2;
    SFloat2 uv0, uv1, uv2;
    int material_id;
    int _pad0, _pad1;
};

struct alignas(16) SPlane {
    SFloat3 normal;
    float  d;
    int    material_id;
    float  _pad0, _pad1, _pad2;
};

struct alignas(16) SMaterial {
    SFloat3 diffuse_color;
    float  specular;
    float  shininess;
    float  ambient;
    int    texture_id;
    int    _pad_tex;
    float  anisotropy;
    float  hair_shift;
    float  hair_spec_r;
    float  hair_exp_r;
    float  hair_spec_t;
    float  hair_exp_t;
    float  hair_backlight;
    float  reflect;
    float  transmission;
    float  ior;
    SFloat3 emission_color;
    float  emission;
    int    mask_id;
    int    normal_id;
    float  normal_strength;
};

struct alignas(16) SLight {
    SFloat3 direction;
    SFloat3 position;
    SFloat3 u_axis;
    SFloat3 v_axis;
    SFloat3 color;
    float  intensity;
    float  radius;
    float  angle;
    float  blend;
    float  beam;
    int    type;
    int    flags;
    int    texture_id;
    int    _pad1;
};

struct alignas(16) SMedium {
    SFloat3 center;
    SFloat3 half;
    SFloat3 sigma_s;
    float  sigma_t;
    int    samples;
    int    _pad0;
};

struct alignas(16) SHeightField {
    SFloat3 center;
    float  size_x;
    float  size_z;
    float  height;
    float  freq;
    int    octaves;
    int    seed;
    int    material_id;
    const unsigned char* image;   // карта высот (1 байт/пиксель) или nullptr
    int    img_w;
    int    img_h;
    int    _pad0;
};

struct alignas(16) SSuperellipsoid {
    SFloat3 center;
    float  a;
    float  b;
    float  c;
    float  n;
    int    material_id;
    int    _pad0, _pad1;
};

struct alignas(16) SCSGNode {
    int op;
    int leaf_kind;
    int leaf_index;
    int left;
    int right;
};

struct alignas(16) SLathePoint {
    float y;
    float radius;
    int   _pad0, _pad1;
};

struct alignas(16) SLathe {
    SFloat3 center;
    int    point_offset;
    int    point_count;
    int    material_id;
    int    _pad0;
};

struct alignas(16) SPrismPoint {
    float x;
    float z;
    int   _pad0, _pad1;
};

struct alignas(16) SPrism {
    SFloat3 center;
    float  y_min;
    float  y_max;
    float  sweep;
    int    point_offset;
    int    point_count;
    int    material_id;
    int    _pad0;
};

struct alignas(16) SSheenScene {
    SCameraData camera;
    const SObject* objects;       int num_objects;
    const SSweepPoint* sweep_points; int num_sweep_points;
    const SSweepStrand* sweep_strands; int num_sweep_strands;
    const STriangle* triangles;   int num_triangles;
    const SHeightField* heightfields; int num_heightfields;
    const SSuperellipsoid* superellipsoids; int num_superellipsoids;
    const SLathePoint* lathe_points; int num_lathe_points;
    const SLathe* lathes;         int num_lathes;
    const SPrismPoint* prism_points; int num_prism_points;
    const SPrism* prisms;         int num_prisms;
    const SCSGNode* csg_nodes;    int csg_root; int num_csg_nodes;
    const SPlane* planes;         int num_planes;
    const SMaterial* materials;   int num_materials;
    const unsigned char* const* texture_pixels; int* texture_widths;
    int* texture_heights;         int num_textures;
    const SLight* lights;         int num_lights;
    const SMedium* media;         int num_media;
};

#ifdef __cplusplus
}
#endif

#endif // SHEEN_FLAT_H