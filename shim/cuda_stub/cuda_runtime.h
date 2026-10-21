#pragma once
// Host-only stub of the CUDA runtime for syntax/size checks with plain g++.
#define __host__
#define __device__
#define __global__
#define __shared__

struct float3 { float x, y, z; };
struct dim3  { unsigned x, y, z; dim3(unsigned a=1,unsigned b=1,unsigned c=1):x(a),y(b),z(c){} };

inline float3 make_float3(float x, float y, float z) { return float3{x,y,z}; }

#include <cmath>
inline float rsqrtf(float x) { return 1.0f / std::sqrt(x); }
inline float sqrtf(float x)  { return std::sqrt(x); }
inline float fabsf(float x)  { return std::fabs(x); }
inline float fminf(float a, float b) { return std::fmin(a,b); }
inline float fmaxf(float a, float b) { return std::fmax(a,b); }

struct float4 { float x, y, z, w; };
inline float4 make_float4(float a, float b, float c, float d) { return float4{a,b,c,d}; }
struct uint3 { unsigned x, y, z; };
static struct { uint3 blockIdx, blockDim, threadIdx; } _blk_dummy;
#define blockIdx (_blk_dummy.blockIdx)
#define blockDim (_blk_dummy.blockDim)
#define threadIdx (_blk_dummy.threadIdx)
