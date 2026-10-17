#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include "scene.h"
#include "bvh.h"
#include "intersect.h"

GPU_DEVICE inline bool intersect_all(
    const Ray& ray, const Object* objs, int n_obj,
    const Plane* planes, int n_planes, HitRecord& rec)
{
    bool hit = false;
    float best = ray.t_max;
    for (int i = 0; i < n_obj; ++i) {
        if (objs[i].type != 0) continue;
        HitRecord t;
        if (intersect_sphere(ray, objs[i].param1, objs[i].param2, t) && t.t < best) {
            rec = t;
            rec.material_id = objs[i].material_id;
            rec.object_id = i;
            best = t.t;
            hit = true;
        }
    }
    for (int i = 0; i < n_planes; ++i) {
        HitRecord t;
        if (intersect_plane(ray, planes[i].normal, planes[i].d, t) && t.t < best) {
            rec = t;
            rec.material_id = planes[i].material_id;
            rec.object_id = n_obj + i;
            best = t.t;
            hit = true;
        }
    }
    return hit;
}

GPU_DEVICE inline bool intersect_scene_mode(
    int mode, const Ray& ray, const BVHNode* nodes, const Object* objs, int n_obj,
    const Plane* planes, int n_planes, HitRecord& rec)
{
    if (mode == 0 || mode == 3) return intersect_all(ray, objs, n_obj, planes, n_planes, rec);
    if (intersect_bvh(ray, nodes, objs, n_obj, rec)) return true;
    return intersect_planes(ray, planes, n_planes, rec);
}

template <int MODE>
__global__ void render_mode(
    float4* out, int W, int H, CameraData cam,
    const BVHNode* nodes, const Object* objs, int n_obj,
    const Plane* planes, int n_planes,
    const Material* mats, const Light* lights, int n_lit)
{
    const int AA = (MODE == 0 || MODE == 1) ? 1 : 4;
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    const float2 sub[4] = {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.75f, 0.75f}};
    float3 acc = {0.0f, 0.0f, 0.0f};

    for (int s = 0; s < AA; ++s) {
        float ox = (AA == 1) ? 0.5f : sub[s].x;
        float oy = (AA == 1) ? 0.5f : sub[s].y;
        float u = (float(x) + ox) / float(W) * 2.0f - 1.0f;
        float v = 1.0f - (float(y) + oy) / float(H) * 2.0f;

        Ray ray;
        ray.origin = cam.position;
        ray.direction = normalize(cam.right * u + cam.up * v + cam.forward);
        ray.t_min = 0.001f;
        ray.t_max = 1e10f;

        HitRecord rec;
        bool hit = intersect_scene_mode(MODE, ray, nodes, objs, n_obj, planes, n_planes, rec);

        float3 color;
        if (hit) {
            const Material& mat = mats[rec.material_id];
            float3 res = mat.diffuse_color * mat.ambient;
            for (int i = 0; i < n_lit; ++i) {
                const Light& light = lights[i];
                Ray sr;
                sr.origin = rec.point + rec.normal * 0.002f;
                sr.direction = -light.direction;
                sr.t_min = 0.001f;
                sr.t_max = 1e10f;
                HitRecord srec;
                if (intersect_scene_mode(MODE, sr, nodes, objs, n_obj, planes, n_planes, srec)) continue;
                float diff = fmaxf(dot(rec.normal, -light.direction), 0.0f);
                res += mat.diffuse_color * light.color * light.intensity * diff;
                if (mat.specular > 0.0f && diff > 0.0f) {
                    float3 hv = normalize(-light.direction + (-ray.direction));
                    float spec = powf(fmaxf(dot(rec.normal, hv), 0.0f), mat.shininess);
                    res += light.color * light.intensity * mat.specular * spec;
                }
            }
            color = res;
        } else {
            float t = 0.5f * (ray.direction.y + 1.0f);
            color = {(1.0f - t) + t * 0.5f, (1.0f - t) + t * 0.7f, (1.0f - t) + t * 1.0f};
        }
        acc += color;
    }

    float3 fc = acc * (1.0f / float(AA));
    out[y * W + x] = {fc.x, fc.y, fc.z, 1.0f};
}

template <int MODE>
static float run_mode(float4* dst, const Scene& scene, const BVHNode* d_nodes,
                      const Object* d_obj, const Plane* d_planes,
                      const Material* d_mat, const Light* d_lit, int W, int H, int RUNS)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);
    render_mode<MODE><<<grid, block>>>(dst, W, H, scene.camera, d_nodes, d_obj,
        (int)scene.objects.size(), d_planes, (int)scene.planes.size(),
        d_mat, d_lit, (int)scene.lights.size());
    cudaDeviceSynchronize();
    cudaEvent_t e0, e1;
    cudaEventCreate(&e0);
    cudaEventCreate(&e1);
    cudaEventRecord(e0);
    for (int i = 0; i < RUNS; ++i) {
        render_mode<MODE><<<grid, block>>>(dst, W, H, scene.camera, d_nodes, d_obj,
            (int)scene.objects.size(), d_planes, (int)scene.planes.size(),
            d_mat, d_lit, (int)scene.lights.size());
    }
    cudaEventRecord(e1);
    cudaEventSynchronize(e1);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, e0, e1);
    cudaEventDestroy(e0);
    cudaEventDestroy(e1);
    return ms / RUNS;
}

int main()
{
    const int W = 800, H = 600, RUNS = 5;
    Scene scene;
    scene.camera.position = {0.0f, 2.5f, 6.0f};
    scene.camera.right = {1.0f, 0.0f, 0.0f};
    scene.camera.up = {0.0f, 1.0f, 0.0f};
    scene.camera.forward = {0.0f, 0.0f, -1.0f};
    scene.add_material({1.0f, 0.0f, 0.0f}, 0.3f, 32.0f, 0.1f);
    scene.add_material({0.0f, 1.0f, 0.0f}, 0.5f, 64.0f, 0.1f);
    scene.add_material({0.0f, 0.0f, 1.0f}, 0.2f, 16.0f, 0.1f);
    scene.add_material({0.5f, 0.5f, 0.5f}, 0.1f, 8.0f, 0.2f);
    for (int i = 0; i < 1000; ++i) {
        float x = (i % 40) * 0.5f - 10.0f;
        float z = (i / 40) * 0.5f - 2.0f;
        scene.add_sphere({x, 1.0f, z}, 0.2f, i % 3);
    }
    // Strands: 3 sweeps, 33 points each, radius 0.02..0.08, orange material (id 3)
    // Strands: 3 sweeps, 33 points each, radius 0.02..0.08, orange material (id 3)
    std::vector<SweepPoint> strand1;
    strand1.push_back({{-2.0f, 1.0f, 0.0f}, 0.08f});
    strand1.push_back({{0.0f, 1.0f, 0.0f}, 0.08f});
    strand1.push_back({{2.0f, 1.0f, 0.0f}, 0.08f});
    scene.add_sphere_sweep(strand1, 3);

    std::vector<SweepPoint> strand2;
    strand2.push_back({{-2.0f, 1.0f, 0.0f}, 0.04f});
    strand2.push_back({{-1.0f, 2.0f, 1.0f}, 0.04f});
    strand2.push_back({{0.0f, 1.0f, 2.0f}, 0.04f});
    scene.add_sphere_sweep(strand2, 3);

    std::vector<SweepPoint> strand3;
    strand3.push_back({{0.0f, 1.0f, 2.0f}, 0.02f});
    strand3.push_back({{1.0f, 1.5f, 1.0f}, 0.02f});
    strand3.push_back({{2.0f, 1.0f, 0.0f}, 0.02f});
    scene.add_sphere_sweep(strand3, 3);
    scene.add_plane({0.0f, 1.0f, 0.0f}, 0.0f, 3);
    scene.add_directional_light({-0.5f, -1.0f, 0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);
    scene.build_bvh();

    float4 *d_lin = 0, *d_bvh = 0, *d_aa = 0, *d_lin_aa = 0;
    BVHNode* d_nodes = 0;
    Object* d_obj = 0;
    Plane* d_planes = 0;
    Material* d_mat = 0;
    Light* d_lit = 0;
    const unsigned long long bytes = (unsigned long long)W * H * sizeof(float4);
    cudaMalloc(&d_lin, bytes);
    cudaMalloc(&d_bvh, bytes);
    cudaMalloc(&d_aa, bytes);
    cudaMalloc(&d_lin_aa, bytes);
    cudaMalloc(&d_nodes, scene.bvh_nodes.size() * sizeof(BVHNode));
    cudaMalloc(&d_obj, scene.objects.size() * sizeof(Object));
    cudaMalloc(&d_planes, scene.planes.size() * sizeof(Plane));
    cudaMalloc(&d_mat, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lit, scene.lights.size() * sizeof(Light));
    cudaMemcpy(d_nodes, scene.bvh_nodes.data(), scene.bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    cudaMemcpy(d_obj, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    cudaMemcpy(d_planes, scene.planes.data(), scene.planes.size() * sizeof(Plane), cudaMemcpyHostToDevice);
    cudaMemcpy(d_mat, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lit, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);

    std::printf("objects: %zu spheres | planes: %zu | BVH nodes: %zu | %dx%d | %d runs per mode\n",
                scene.objects.size(), scene.planes.size(), scene.bvh_nodes.size(), W, H, RUNS);

    float t_lin = run_mode<0>(d_lin, scene, d_nodes, d_obj, d_planes, d_mat, d_lit, W, H, RUNS);
    float t_bvh = run_mode<1>(d_bvh, scene, d_nodes, d_obj, d_planes, d_mat, d_lit, W, H, RUNS);
    float t_aa = run_mode<2>(d_aa, scene, d_nodes, d_obj, d_planes, d_mat, d_lit, W, H, RUNS);
    float t_lin_aa = run_mode<3>(d_lin_aa, scene, d_nodes, d_obj, d_planes, d_mat, d_lit, W, H, RUNS);

    std::printf("\n--- results, ms per frame ---\n");
    std::printf("linear scan, no AA:   %.3f\n", t_lin);
    std::printf("linear scan, 4x AA:   %.3f\n", t_lin_aa);
    std::printf("Render time (no AA):  %.3f   (BVH + plane check)\n", t_bvh);
    std::printf("Render time (4x AA):  %.3f   (the setting the renderer ships with)\n", t_aa);
    std::printf("BVH speedup over linear (no AA): %.1fx\n", t_lin / t_bvh);
    std::printf("BVH speedup over linear (4x AA): %.1fx\n", t_lin_aa / t_aa);
    std::printf("price of 4x AA: linear %.1fx, BVH %.1fx\n", t_lin_aa / t_lin, t_aa / t_bvh);
    std::printf("structural sizes: BVHNode %zu B, Object %zu B, Plane %zu B, HitRecord %zu B\n",
                sizeof(BVHNode), sizeof(Object), sizeof(Plane), sizeof(HitRecord));

    std::vector<float4> lin(W * H), bvh(W * H), aa(W * H), lin_aa(W * H);
    cudaMemcpy(lin.data(), d_lin, bytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(bvh.data(), d_bvh, bytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(aa.data(), d_aa, bytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(lin_aa.data(), d_lin_aa, bytes, cudaMemcpyDeviceToHost);

    long long diff_lb = 0, diff_la = 0, diff_aa = 0;
    for (int i = 0; i < W * H; ++i) {
        float d1 = std::fabs(lin[i].x - bvh[i].x) + std::fabs(lin[i].y - bvh[i].y) + std::fabs(lin[i].z - bvh[i].z);
        float d2 = std::fabs(lin[i].x - aa[i].x) + std::fabs(lin[i].y - aa[i].y) + std::fabs(lin[i].z - aa[i].z);
        float d3 = std::fabs(lin_aa[i].x - aa[i].x) + std::fabs(lin_aa[i].y - aa[i].y) + std::fabs(lin_aa[i].z - aa[i].z);
        if (d1 > 1e-5f) diff_lb++;
        if (d2 > 1e-5f) diff_la++;
        if (d3 > 1e-5f) diff_aa++;
    }
    std::printf("\n--- image checks ---\n");
    std::printf("linear vs BVH (both no AA):   differing pixels = %lld of %d\n", diff_lb, W * H);
    std::printf("linear vs BVH (both 4x AA):   differing pixels = %lld of %d\n", diff_aa, W * H);
    std::printf("no AA vs 4x AA (BVH):         differing pixels = %lld of %d\n", diff_la, W * H);

    std::vector<float4> horizon_probe;
    for (int x : {100, 400, 700}) {
        for (int y = 297; y <= 302; ++y) {
            std::printf("  x=%3d y=%d : %.3f %.3f %.3f\n", x, y, lin[y * W + x].x, lin[y * W + x].y, lin[y * W + x].z);
        }
    }
    (void)horizon_probe;

    cudaFree(d_lin); cudaFree(d_bvh); cudaFree(d_aa); cudaFree(d_lin_aa);
    cudaFree(d_nodes); cudaFree(d_obj); cudaFree(d_planes); cudaFree(d_mat); cudaFree(d_lit);
    return 0;
}
