#include <iostream>
#include <vector>
#include <chrono>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "render_kernel.h"
#include "scene.h"

int main() {
    int width = 800;
    int height = 600;
    Scene scene;
    
    scene.camera.position = {0.0f, 2.5f, 6.0f};
    scene.camera.right = {1.0f, 0.0f, 0.0f};
    scene.camera.up = {0.0f, 1.0f, 0.0f};
    scene.camera.forward = {0.0f, 0.0f, -1.0f};
    
    scene.add_material({1.0f, 0.0f, 0.0f}, 0.3f, 32.0f, 0.1f);
    scene.add_material({0.5f, 0.5f, 0.5f}, 0.1f, 8.0f, 0.2f); // Пол
    
    // Генерируем 1000 сфер для честного бенчмарка BVH
    for (int i = 0; i < 1000; i++) {
        float x = (i % 40) * 0.5f - 10.0f;
        float z = (i / 40) * 0.5f - 2.0f;
        scene.add_sphere({x, 1.0f, z}, 0.2f, 0); // Маленькие сферы
    }
    
    scene.objects.push_back({1, 1, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f}); // Пол
    
    scene.add_directional_light({-0.5f, -1.0f, 0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);
    
    // СТРОИМ BVH
    scene.build_bvh();
    std::cout << "BVH nodes built: " << scene.bvh_nodes.size() << "\n";
    
    float4* d_output;
    BVHNode* d_bvh;
    Object* d_objects;
    Material* d_materials;
    Light* d_lights;
    
    cudaMalloc(&d_output, width * height * sizeof(float4));
    cudaMalloc(&d_bvh, scene.bvh_nodes.size() * sizeof(BVHNode));
    cudaMalloc(&d_objects, scene.objects.size() * sizeof(Object));
    cudaMalloc(&d_materials, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lights, scene.lights.size() * sizeof(Light));
    
    cudaMemcpy(d_bvh, scene.bvh_nodes.data(), scene.bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    cudaMemcpy(d_objects, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    cudaMemcpy(d_materials, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lights, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);
    
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    
    // БЕНЧМАРК
    cudaDeviceSynchronize();
    auto start = std::chrono::high_resolution_clock::now();
    
    render_kernel<<<grid, block>>>(d_output, width, height, scene.camera,
        d_bvh, d_objects, scene.objects.size(), d_materials, scene.materials.size(), d_lights, scene.lights.size());
    
    cudaDeviceSynchronize();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Render time (with BVH, 1000 objects): " << elapsed.count() << " ms\n";
    
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
    
    cudaFree(d_output); cudaFree(d_bvh); cudaFree(d_objects); cudaFree(d_materials); cudaFree(d_lights);
    return 0;
}