#include <iostream>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "render_kernel.h"
#include "scene.h"

int main() {
    int width = 800;
    int height = 600;
    Scene scene;
    
    // Камера чуть выше и дальше, чтобы видеть и сферы, и пол перед ними
    scene.camera.position = {0.0f, 2.5f, 6.0f}; 
    scene.camera.right = {1.0f, 0.0f, 0.0f};
    scene.camera.up = {0.0f, 1.0f, 0.0f};
    scene.camera.forward = {0.0f, 0.0f, -1.0f};
    
    // Материалы: красный, зелёный, синий, и серый для пола
    scene.add_material({1.0f, 0.0f, 0.0f}, 0.3f, 32.0f, 0.1f);
    scene.add_material({0.0f, 1.0f, 0.0f}, 0.5f, 64.0f, 0.1f);
    scene.add_material({0.0f, 0.0f, 1.0f}, 0.2f, 16.0f, 0.1f);
    scene.add_material({0.5f, 0.5f, 0.5f}, 0.1f, 8.0f, 0.2f); // Пол
    
    // Объекты: 3 сферы. ИСПРАВЛЕНО: y = 1.0f, чтобы они стояли НА полу, а не в нём
    scene.add_sphere({-1.5f, 1.0f, 0.0f}, 1.0f, 0);
    scene.add_sphere({ 0.0f, 1.0f, 0.0f}, 1.0f, 1);
    scene.add_sphere({ 1.5f, 1.0f, 0.0f}, 1.0f, 2);
    
    // Объект: Плоскость (пол). type=1, param1=нормаль(вверх), param2=расстояние(0)
    scene.objects.push_back({1, 3, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f});
    
    // Свет: ИСПРАВЛЕНО: Z = +0.5f. Свет теперь светит СЗАДИ камеры ВПЕРЁД, 
    // отбрасывая тени на пол в сторону зрителя.
    scene.add_directional_light({-0.5f, -1.0f, 0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);
    
    // Выделение и копирование памяти
    float4* d_output;
    Object* d_objects;
    Material* d_materials;
    Light* d_lights;
    
    cudaMalloc(&d_output, width * height * sizeof(float4));
    cudaMalloc(&d_objects, scene.objects.size() * sizeof(Object));
    cudaMalloc(&d_materials, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lights, scene.lights.size() * sizeof(Light));
    
    cudaMemcpy(d_objects, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    cudaMemcpy(d_materials, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lights, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);
    
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    
    render_kernel<<<grid, block>>>(d_output, width, height, scene.camera,
        d_objects, scene.objects.size(), d_materials, scene.materials.size(), d_lights, scene.lights.size());
    
    cudaDeviceSynchronize();
    
    std::vector<float4> h_output(width * height);
    cudaMemcpy(h_output.data(), d_output, width * height * sizeof(float4), cudaMemcpyDeviceToHost);
    
    std::vector<unsigned char> png_data(width * height * 3);
    for (int i = 0; i < width * height; i++) {
        png_data[i * 3 + 0] = static_cast<unsigned char>(fminf(h_output[i].x * 255.0f, 255.0f));
        png_data[i * 3 + 1] = static_cast<unsigned char>(fminf(h_output[i].y * 255.0f, 255.0f));
        png_data[i * 3 + 2] = static_cast<unsigned char>(fminf(h_output[i].z * 255.0f, 255.0f));
    }
    
    stbi_write_png("test_output.png", width, height, 3, png_data.data(), width * 3);
    std::cout << "PNG saved: test_output.png\n";
    
    cudaFree(d_output); cudaFree(d_objects); cudaFree(d_materials); cudaFree(d_lights);
    return 0;
}