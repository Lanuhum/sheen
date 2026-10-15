#include "render_kernel.h"
#include "intersect.h"

// ---------------------------------------------------------
// Простое освещение (Blinn-Phong упрощённый)
// ---------------------------------------------------------
GPU_DEVICE inline float3 compute_lighting(
    const HitRecord& rec,
    const Material* materials,
    const Light* lights,
    int num_lights,
    float3 view_dir
) {
    const Material& mat = materials[rec.material_id];
    
    // Ambient (окружающий свет)
    float3 result = mat.diffuse_color * mat.ambient;
    
    // Для каждого источника света
    for (int i = 0; i < num_lights; i++) {
        const Light& light = lights[i];
        
        // Diffuse (рассеянный свет)
        float diff = fmaxf(dot(rec.normal, -light.direction), 0.0f);
        result += mat.diffuse_color * light.color * light.intensity * diff;
        
        // Specular (блик)
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
    const Light* lights,
    int num_lights
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= width || y >= height) return;
    
    // Генерируем луч
    float u = (float(x) + 0.5f) / float(width) * 2.0f - 1.0f;
    float v = 1.0f - (float(y) + 0.5f) / float(height) * 2.0f;
    
    Ray ray;
    ray.origin = camera.position;
    ray.direction = normalize(make_float3(
        camera.right.x * u + camera.up.x * v + camera.forward.x,
        camera.right.y * u + camera.up.y * v + camera.forward.y,
        camera.right.z * u + camera.up.z * v + camera.forward.z
    ));
    ray.t_min = 0.001f;
    ray.t_max = 1e10f;
    
    HitRecord rec;
    bool hit = intersect_scene(ray, objects, num_objects, rec);
    
    float4 color;
    if (hit) {
        // Вычисляем освещение
        float3 lit_color = compute_lighting(rec, materials, lights, num_lights, -ray.direction);
        color = make_float4(lit_color.x, lit_color.y, lit_color.z, 1.0f);
    } else {
        // Градиент неба
        float t = 0.5f * (ray.direction.y + 1.0f);
        color = make_float4(
            (1.0f - t) * 1.0f + t * 0.5f,
            (1.0f - t) * 1.0f + t * 0.7f,
            (1.0f - t) * 1.0f + t * 1.0f,
            1.0f
        );
    }
    
    output[y * width + x] = color;
}