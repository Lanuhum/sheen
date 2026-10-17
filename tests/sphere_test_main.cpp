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
    
    // Камера
    scene.camera.position = {0.0f, 2.5f, 6.0f};
    scene.camera.right = {1.0f, 0.0f, 0.0f};
    scene.camera.up = {0.0f, 1.0f, 0.0f};
    scene.camera.forward = {0.0f, 0.0f, -1.0f};
    
    // Материалы
    scene.add_material({1.0f, 0.0f, 0.0f}, 0.3f, 32.0f, 0.1f); // красный
    scene.add_material({0.0f, 1.0f, 0.0f}, 0.5f, 64.0f, 0.1f); // зелёный
    scene.add_material({0.0f, 0.0f, 1.0f}, 0.2f, 16.0f, 0.1f); // синий
    scene.add_material({0.5f, 0.5f, 0.5f}, 0.1f, 8.0f, 0.2f);  // серый (пол)
    scene.add_material({0.8f, 0.4f, 0.1f}, 0.6f, 128.0f, 0.1f); // оранжевый (пряди)
    
    // 1000 сфер для бенчмарка BVH
    for (int i = 0; i < 1000; i++) {
        float x = (i % 40) * 0.5f - 10.0f;
        float z = (i / 40) * 0.5f - 2.0f;
        scene.add_sphere({x, 1.0f, z}, 0.2f, i % 3);
    }
    
    // Плоскость (пол)
    scene.add_plane({0.0f, 1.0f, 0.0f}, 0.0f, 3);
    
    // Несколько тестовых прядей (sphere_sweep)
    // Прядь 1: изогнутая дуга слева
    {
        std::vector<SweepPoint> pts;
        for (int i = 0; i <= 8; i++) {
            float t = (float)i / 8.0f;
            SweepPoint p;
            p.position = {-2.0f + t * 1.0f, 1.5f + sinf(t * 3.14159f) * 0.5f, 0.0f};
            p.radius = 0.05f * (1.0f - t * 0.5f);  // сужается к концу
            pts.push_back(p);
        }
        scene.add_sphere_sweep(pts, 4);
    }
    
    // Прядь 2: вертикальная прядь справа
    {
        std::vector<SweepPoint> pts;
        for (int i = 0; i <= 6; i++) {
            float t = (float)i / 6.0f;
            SweepPoint p;
            p.position = {2.0f, 1.0f + t * 2.0f, 0.0f};
            p.radius = 0.08f * (1.0f - t * 0.7f);
            pts.push_back(p);
        }
        scene.add_sphere_sweep(pts, 4);
    }
    
    // Прядь 3: спираль в центре
    {
        std::vector<SweepPoint> pts;
        for (int i = 0; i <= 16; i++) {
            float t = (float)i / 16.0f;
            SweepPoint p;
            float angle = t * 6.28318f * 2.0f;
            p.position = {sinf(angle) * 0.3f, 1.0f + t * 1.5f, cosf(angle) * 0.3f};
            p.radius = 0.04f;
            pts.push_back(p);
        }
        scene.add_sphere_sweep(pts, 4);
    }
    
    // Свет
    scene.add_directional_light({-0.5f, -1.0f, 0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);
    
    // Строим BVH
    scene.build_bvh();
    std::cout << "BVH nodes (spheres): " << scene.bvh_nodes.size() << "\n";
    std::cout << "BVH nodes (strands): " << scene.strand_bvh_nodes.size() << "\n";
    std::cout << "Objects (spheres): " << scene.objects.size() << "\n";
    std::cout << "Strands: " << scene.sweep_strands.size() << "\n";
    std::cout << "Sweep points: " << scene.sweep_points.size() << "\n";
    std::cout << "Planes: " << scene.planes.size() << "\n";
    
    // Выделение памяти на GPU
    float4* d_output;
    BVHNode* d_bvh;
    BVHNode* d_strand_bvh;
    Object* d_objects;
    SweepStrand* d_strands;
    SweepPoint* d_sweep_points;
    Plane* d_planes;
    Material* d_materials;
    Light* d_lights;
    
    cudaMalloc(&d_output, width * height * sizeof(float4));
    cudaMalloc(&d_bvh, scene.bvh_nodes.size() * sizeof(BVHNode));
    cudaMalloc(&d_strand_bvh, scene.strand_bvh_nodes.size() * sizeof(BVHNode));
    cudaMalloc(&d_objects, scene.objects.size() * sizeof(Object));
    cudaMalloc(&d_strands, scene.sweep_strands.size() * sizeof(SweepStrand));
    cudaMalloc(&d_sweep_points, scene.sweep_points.size() * sizeof(SweepPoint));
    cudaMalloc(&d_planes, scene.planes.size() * sizeof(Plane));
    cudaMalloc(&d_materials, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lights, scene.lights.size() * sizeof(Light));
    
    cudaMemcpy(d_bvh, scene.bvh_nodes.data(), scene.bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    cudaMemcpy(d_strand_bvh, scene.strand_bvh_nodes.data(), scene.strand_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    cudaMemcpy(d_objects, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    cudaMemcpy(d_strands, scene.sweep_strands.data(), scene.sweep_strands.size() * sizeof(SweepStrand), cudaMemcpyHostToDevice);
    cudaMemcpy(d_sweep_points, scene.sweep_points.data(), scene.sweep_points.size() * sizeof(SweepPoint), cudaMemcpyHostToDevice);
    cudaMemcpy(d_planes, scene.planes.data(), scene.planes.size() * sizeof(Plane), cudaMemcpyHostToDevice);
    cudaMemcpy(d_materials, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lights, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);
    
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    
    cudaDeviceSynchronize();
    auto start = std::chrono::high_resolution_clock::now();
    
    render_kernel<<<grid, block>>>(
        d_output, width, height, scene.camera,
        d_bvh, d_objects, scene.objects.size(),
        d_strand_bvh, d_strands, d_sweep_points, scene.sweep_strands.size(),
        d_planes, scene.planes.size(),
        d_materials, scene.materials.size(),
        d_lights, scene.lights.size()
    );
    
    cudaDeviceSynchronize();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Render time (4x AA): " << elapsed.count() << " ms\n";
    
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
    
    cudaFree(d_output);
    cudaFree(d_bvh);
    cudaFree(d_strand_bvh);
    cudaFree(d_objects);
    cudaFree(d_strands);
    cudaFree(d_sweep_points);
    cudaFree(d_planes);
    cudaFree(d_materials);
    cudaFree(d_lights);
    
    return 0;
}