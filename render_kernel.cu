#include "render_kernel.h"
#include "intersect.h"
#include "bvh.h"

GPU_DEVICE inline float3 compute_lighting(
    const HitRecord& rec,
    const Material* materials,
    const Light* lights,
    int num_lights,
    const BVHNode* bvh_nodes,
    const Object* objects,
    int num_objects,
    const BVHNode* strand_bvh_nodes,
    const SweepStrand* strands,
    const SweepPoint* sweep_points,
    int num_strands,
    const Plane* planes,
    int num_planes,
    float3 view_dir
) {
    const Material& mat = materials[rec.material_id];
    float3 result = mat.diffuse_color * mat.ambient;
    
    for (int i = 0; i < num_lights; i++) {
        const Light& light = lights[i];
        
        Ray shadow_ray;
        shadow_ray.origin = rec.point + rec.normal * 0.002f;
        shadow_ray.direction = -light.direction;
        shadow_ray.t_min = 0.001f;
        shadow_ray.t_max = 1e10f;
        
        HitRecord shadow_rec;
        bool in_shadow = intersect_bvh(shadow_ray, bvh_nodes, objects, num_objects, shadow_rec);
        if (!in_shadow) {
            in_shadow = intersect_strand_bvh(shadow_ray, strand_bvh_nodes, strands, sweep_points, num_strands, shadow_rec);
        }
        if (!in_shadow) {
            in_shadow = intersect_planes(shadow_ray, planes, num_planes, shadow_rec);
        }
        
        if (in_shadow) continue;
        
        float diff = fmaxf(dot(rec.normal, -light.direction), 0.0f);
        result += mat.diffuse_color * light.color * light.intensity * diff;
        
        if (mat.specular > 0.0f && diff > 0.0f) {
            float3 half_vec = normalize(-light.direction + view_dir);
            float spec = powf(fmaxf(dot(rec.normal, half_vec), 0.0f), mat.shininess);
            result += light.color * light.intensity * mat.specular * spec;
        }
    }
    return result;
}

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
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    
    const int AA_SAMPLES = 4;
    const float2 sample_offsets[AA_SAMPLES] = {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.75f, 0.75f}};
    float3 accumulated_color = {0.0f, 0.0f, 0.0f};
    
    for (int s = 0; s < AA_SAMPLES; s++) {
        float u = (float(x) + sample_offsets[s].x) / float(width) * 2.0f - 1.0f;
        float v = 1.0f - (float(y) + sample_offsets[s].y) / float(height) * 2.0f;
        
        Ray ray;
        ray.origin = camera.position;
        ray.direction = normalize(camera.right * u + camera.up * v + camera.forward);
        ray.t_min = 0.001f;
        ray.t_max = 1e10f;
        
        HitRecord rec;
        // Трёхэтапная проверка: BVH сфер → BVH прядей → плоскости
        bool hit = intersect_bvh(ray, bvh_nodes, objects, num_objects, rec);
        if (!hit) {
            hit = intersect_strand_bvh(ray, strand_bvh_nodes, strands, sweep_points, num_strands, rec);
        }
        if (!hit) {
            hit = intersect_planes(ray, planes, num_planes, rec);
        }
        
        float3 color;
        if (hit) {
            color = compute_lighting(
                rec, materials, lights, num_lights,
                bvh_nodes, objects, num_objects,
                strand_bvh_nodes, strands, sweep_points, num_strands,
                planes, num_planes,
                -ray.direction
            );
        } else {
            float t = 0.5f * (ray.direction.y + 1.0f);
            color = {(1.0f - t) * 1.0f + t * 0.5f,
                     (1.0f - t) * 1.0f + t * 0.7f,
                     (1.0f - t) * 1.0f + t * 1.0f};
        }
        accumulated_color += color;
    }
    
    float3 final_color = accumulated_color * (1.0f / float(AA_SAMPLES));
    output[y * width + x] = {final_color.x, final_color.y, final_color.z, 1.0f};
}