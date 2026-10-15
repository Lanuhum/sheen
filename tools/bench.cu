#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include "scene.h"
#include "bvh.h"

template <bool USE_BVH>
__global__ void render_bench(
    float4* out, int W, int H, CameraData cam,
    const BVHNode* nodes, const Object* objs, int n_obj,
    const Material* mats, const Light* lights, int n_lit)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    float u = (float(x) + 0.5f) / float(W) * 2.0f - 1.0f;
    float v = 1.0f - (float(y) + 0.5f) / float(H) * 2.0f;

    Ray ray;
    ray.origin = cam.position;
    ray.direction = normalize(cam.right * u + cam.up * v + cam.forward);
    ray.t_min = 0.001f;
    ray.t_max = 1e10f;

    HitRecord rec;
    bool hit = USE_BVH ? intersect_bvh(ray, nodes, objs, n_obj, rec)
                       : intersect_scene(ray, objs, n_obj, rec);

    float4 color;
    if (hit) {
        const Material& mat = mats[rec.material_id];
        float3 result = mat.diffuse_color * mat.ambient;
        for (int i = 0; i < n_lit; i++) {
            const Light& light = lights[i];
            Ray sr;
            sr.origin = rec.point + rec.normal * 0.002f;
            sr.direction = -light.direction;
            sr.t_min = 0.001f;
            sr.t_max = 1e10f;
            HitRecord srec;
            bool shadowed = USE_BVH ? intersect_bvh(sr, nodes, objs, n_obj, srec)
                                    : intersect_scene(sr, objs, n_obj, srec);
            if (shadowed) continue;
            float diff = fmaxf(dot(rec.normal, -light.direction), 0.0f);
            result += mat.diffuse_color * light.color * light.intensity * diff;
            if (mat.specular > 0.0f && diff > 0.0f) {
                float3 hv = normalize(-light.direction + (-ray.direction));
                float spec = powf(fmaxf(dot(rec.normal, hv), 0.0f), mat.shininess);
                result += light.color * light.intensity * mat.specular * spec;
            }
        }
        color = make_float4(result.x, result.y, result.z, 1.0f);
    } else {
        float t = 0.5f * (ray.direction.y + 1.0f);
        color = make_float4((1.0f - t) + t * 0.5f, (1.0f - t) + t * 0.7f, (1.0f - t) + t * 1.0f, 1.0f);
    }
    out[y * W + x] = color;
}

template <bool USE_BVH>
static float run_mode(float4* dst, const Scene& scene, const BVHNode* d_bvh,
                      const Object* d_obj, const Material* d_mat, const Light* d_lit,
                      int W, int H, int RUNS)
{
    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);
    render_bench<USE_BVH><<<grid, block>>>(dst, W, H, scene.camera, d_bvh,
        d_obj, (int)scene.objects.size(), d_mat, d_lit, (int)scene.lights.size());
    cudaDeviceSynchronize();
    cudaEvent_t e0, e1;
    cudaEventCreate(&e0);
    cudaEventCreate(&e1);
    cudaEventRecord(e0);
    for (int i = 0; i < RUNS; i++) {
        render_bench<USE_BVH><<<grid, block>>>(dst, W, H, scene.camera, d_bvh,
            d_obj, (int)scene.objects.size(), d_mat, d_lit, (int)scene.lights.size());
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
    scene.add_material({0.5f, 0.5f, 0.5f}, 0.1f, 8.0f, 0.2f);
    for (int i = 0; i < 1000; i++) {
        float x = (i % 40) * 0.5f - 10.0f;
        float z = (i / 40) * 0.5f - 2.0f;
        scene.add_sphere({x, 1.0f, z}, 0.2f, 0);
    }
    scene.objects.push_back({1, 1, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f});
    scene.add_directional_light({-0.5f, -1.0f, 0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);
    scene.build_bvh();

    float4 *d_a = 0, *d_b = 0;
    BVHNode* d_bvh = 0;
    Object* d_obj = 0;
    Material* d_mat = 0;
    Light* d_lit = 0;
    unsigned long long bytes = (unsigned long long)W * H * sizeof(float4);
    cudaMalloc(&d_a, bytes);
    cudaMalloc(&d_b, bytes);
    cudaMalloc(&d_bvh, scene.bvh_nodes.size() * sizeof(BVHNode));
    cudaMalloc(&d_obj, scene.objects.size() * sizeof(Object));
    cudaMalloc(&d_mat, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lit, scene.lights.size() * sizeof(Light));
    cudaMemcpy(d_bvh, scene.bvh_nodes.data(), scene.bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    cudaMemcpy(d_obj, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    cudaMemcpy(d_mat, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lit, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);

    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    std::printf("objects: %zu | BVH nodes: %zu | %dx%d | runs per mode: %d\n",
                scene.objects.size(), scene.bvh_nodes.size(), W, H, RUNS);

    std::printf("\n--- LINEAR (intersect_scene, per-pixel scan of all objects) ---\n");
    float lin = run_mode<false>(d_b, scene, d_bvh, d_obj, d_mat, d_lit, W, H, RUNS);
    std::printf("linear: %.3f ms/frame\n", lin);

    std::printf("\n--- BVH (intersect_bvh) ---\n");
    float bvh = run_mode<true>(d_a, scene, d_bvh, d_obj, d_mat, d_lit, W, H, RUNS);
    std::printf("bvh:    %.3f ms/frame\n", bvh);
    std::printf("speedup: %.1fx | linear/bvh = %.1f\n", lin / bvh, lin / bvh);

    std::vector<float4> a(W * H), b(W * H);
    cudaMemcpy(a.data(), d_a, bytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(b.data(), d_b, bytes, cudaMemcpyDeviceToHost);
    double maxdiff = 0.0;
    long long neq = 0;
    std::vector<int> rows(H, 0);
    int minrow = H, maxrow = -1;
    for (int i = 0; i < W * H; i++) {
        double dx = std::fabs(double(a[i].x) - double(b[i].x));
        double dy = std::fabs(double(a[i].y) - double(b[i].y));
        double dz = std::fabs(double(a[i].z) - double(b[i].z));
        double m = dx > dy ? dx : dy;
        if (dz > m) m = dz;
        if (m > maxdiff) maxdiff = m;
        if (m > 0.0) {
            neq++;
            int r = i / W;
            rows[r]++;
            if (r < minrow) minrow = r;
            if (r > maxrow) maxrow = r;
        }
    }
    std::printf("\nimage comparison (BVH vs linear): max per-channel diff = %.6f | differing pixels = %lld of %d\n",
                maxdiff, neq, W * H);
    if (neq > 0) {
        std::printf("differing rows: %d..%d | distributed over %d rows\n", minrow, maxrow,
                    (int)std::count_if(rows.begin(), rows.end(), [](int v) { return v > 0; }));
        std::printf("worst rows (row: pixels): ");
        std::vector<std::pair<int, int>> v;
        for (int r = 0; r < H; r++) if (rows[r]) v.push_back({r, rows[r]});
        std::sort(v.begin(), v.end(), [](auto p, auto q) { return p.second > q.second; });
        for (size_t k = 0; k < v.size() && k < 5; k++) std::printf("%d:%d ", v[k].first, v[k].second);
        std::printf("\n");
    }

    cudaFree(d_a); cudaFree(d_b); cudaFree(d_bvh); cudaFree(d_obj); cudaFree(d_mat); cudaFree(d_lit);
    return 0;
}
