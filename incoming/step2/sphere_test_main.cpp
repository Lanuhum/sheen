#include <iostream>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "render_kernel.h"
#include "scene.h"

int main() {
    int width = 800;
    int height = 600;
    
    // Создаём сцену
    Scene scene;
    
    // Камера
    scene.camera.position = make_float3(0.0f, 0.0f, 5.0f);
    scene.camera.right = make_float3(1.0f, 0.0f, 0.0f);
    scene.camera.up = make_float3(0.0f, 1.0f, 0.0f);
    scene.camera.forward = make_float3(0.0f, 0.0f, -1.0f);
    
    // Материалы
    scene.add_material(make_float3(1.0f, 0.0f, 0.0f), 0.3f, 32.0f, 0.1f); // Красный
    scene.add_material(make_float3(0.0f, 1.0f, 0.0f), 0.5f, 64.0f, 0.1f); // Зелёный
    scene.add_material(make_float3(0.0f, 0.0f, 1.0f), 0.2f, 16.0f, 0.1f); // Синий
    
    // Объекты (3 сферы)
    scene.add_sphere(make_float3(-1.5f, 0.0f, 0.0f), 1.0f, 0); // Красная слева
    scene.add_sphere(make_float3(0.0f, 0.0f, 0.0f), 1.0f, 1);  // Зелёная в центре
    scene.add_sphere(make_float3(1.5f, 0.0f, 0.0f), 1.0f, 2);  // Синяя справа
    
    // Свет (направленный, сверху-справа)
    scene.add_directional_light(
        make_float3(-0.5f, -1.0f, -0.5f),
        make_float3(1.0f, 1.0f, 1.0f),
        0.8f
    );
    
    // Выделяем память на GPU
    float4* d_output;
    Object* d_objects;
    Material* d_materials;
    Light* d_lights;
    
    cudaMalloc(&d_output, width * height * sizeof(float4));
    cudaMalloc(&d_objects, scene.objects.size() * sizeof(Object));
    cudaMalloc(&d_materials, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lights, scene.lights.size() * sizeof(Light));
    
    // Копируем данные на GPU
    cudaMemcpy(d_objects, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    cudaMemcpy(d_materials, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lights, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);
    
    // Запускаем ядро
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    
    render_kernel<<<grid, block>>>(
        d_output, width, height,
        scene.camera,
        d_objects, scene.objects.size(),
        d_materials, scene.materials.size(),
        d_lights, scene.lights.size()
    );
    
    cudaDeviceSynchronize();
    
    // Копируем результат обратно
    std::vector<float4> h_output(width * height);
    cudaMemcpy(h_output.data(), d_output, width * height * sizeof(float4), cudaMemcpyDeviceToHost);
    
    // Сохраняем PNG
    std::vector<unsigned char> png_data(width * height * 3);
    for (int i = 0; i < width * height; i++) {
        png_data[i * 3 + 0] = static_cast<unsigned char>(h_output[i].x * 255.0f);
        png_data[i * 3 + 1] = static_cast<unsigned char>(h_output[i].y * 255.0f);
        png_data[i * 3 + 2] = static_cast<unsigned char>(h_output[i].z * 255.0f);
    }
    
    stbi_write_png("test_output.png", width, height, 3, png_data.data(), width * 3);
    std::cout << "PNG saved: test_output.png\n";
    
    // Освобождаем память
    cudaFree(d_output);
    cudaFree(d_objects);
    cudaFree(d_materials);
    cudaFree(d_lights);
    
    return 0;
}