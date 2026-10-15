#include "render_kernel.h"
#include "intersect.h"

GPU_DEVICE inline float3 compute_lighting(
    const HitRecord& rec,
    const Material* materials,
    const Light* lights,
    int num_lights,
    const Object* objects,
    int num_objects,
    float3 view_dir
) {
    const Material& mat = materials[rec.material_id];
    float3 result = mat.diffuse_color * mat.ambient;
    
    for (int i = 0; i < num_lights; i++) {
        const Light& light = lights[i];
        
        // 1. Строим теневой луч
        Ray shadow_ray;
        // Сдвиг на 0.002 вдоль нормали, чтобы избежать self-intersection (shadow acne)
        shadow_ray.origin = rec.point + rec.normal * 0.002f;
        shadow_ray.direction = -light.direction; // Для направленного света
        shadow_ray.t_min = 0.001f;
        shadow_ray.t_max = 1e10f; // Для направленного света дистанция бесконечна
        
        // 2. Проверяем тень
        if (is_in_shadow(shadow_ray, objects, num_objects)) {
            continue; // Свет заблокирован, пропускаем diffuse и specular
        }
        
        // 3. Diffuse
        float diff = fmaxf(dot(rec.normal, -light.direction), 0.0f);
        result += mat.diffuse_color * light.color * light.intensity * diff;
        
        // 4. Specular
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
    const Object* objects,
    int num_objects,
    const Material* materials,
    int num_materials,
    const Light* lights,
    int num_lights
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= width || y >= height) return;
    
    float u = (float(x) + 0.5f) / float(width) * 2.0f - 1.0f;
    float v = 1.0f - (float(y) + 0.5f) / float(height) * 2.0f;
    
    Ray ray;
    ray.origin = camera.position;
    ray.direction = normalize(camera.right * u + camera.up * v + camera.forward);
    ray.t_min = 0.001f;
    ray.t_max = 1e10f;
    
    HitRecord rec;
    bool hit = intersect_scene(ray, objects, num_objects, rec);
    
    float4 color;
    if (hit) {
        float3 lit_color = compute_lighting(rec, materials, lights, num_lights, objects, num_objects, -ray.direction);
        color = make_float4(lit_color.x, lit_color.y, lit_color.z, 1.0f);
    } else {
        float t = 0.5f * (ray.direction.y + 1.0f);
        color = make_float4((1.0f - t) * 1.0f + t * 0.5f,
                            (1.0f - t) * 1.0f + t * 0.7f,
                            (1.0f - t) * 1.0f + t * 1.0f, 1.0f);
    }
    output[y * width + x] = color;
}