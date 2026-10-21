#include <iostream>
#include <vector>
#include <chrono>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "render_kernel.h"
#include "scene.h"
#include "sampling/aa.h"

int main() {
    int width = 800;
    int height = 600;
    Scene scene;

    scene.camera.position = {0.0f, 2.5f, 6.0f};
    scene.camera.right = {1.0f, 0.0f, 0.0f};
    scene.camera.up = {0.0f, 1.0f, 0.0f};
    scene.camera.forward = {0.0f, 0.0f, -1.0f};

    scene.add_material({0.8f, 0.2f, 0.2f}, 0.4f, 32.0f, 0.1f); // куб
    scene.add_material({0.5f, 0.5f, 0.5f}, 0.1f, 8.0f, 0.2f);  // пол

    scene.add_cube({0.0f, 1.0f, 0.0f}, 0.6f, 0);
    scene.add_plane({0.0f, 1.0f, 0.0f}, 0.0f, 1);
    scene.add_directional_light({-0.5f, -1.0f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);

    scene.build_bvh();

    std::cout << "Triangles: " << scene.triangles.size() << "\n";
    std::cout << "BVH nodes (mesh): " << scene.mesh_bvh_nodes.size() << "\n";

    float4* d_output;
    BVHNode* d_bvh;
    BVHNode* d_strand_bvh;
    BVHNode* d_mesh_bvh;
    Object* d_objects;
    SweepStrand* d_strands;
    SweepPoint* d_sweep_points;
    Triangle* d_triangles;
    Plane* d_planes;
    Material* d_materials;
    Light* d_lights;

    cudaMalloc(&d_output, width * height * sizeof(float4));
    cudaMalloc(&d_bvh, 1);
    cudaMalloc(&d_strand_bvh, 1);
    cudaMalloc(&d_mesh_bvh, scene.mesh_bvh_nodes.size() * sizeof(BVHNode));
    cudaMalloc(&d_objects, 1);
    cudaMalloc(&d_strands, 1);
    cudaMalloc(&d_sweep_points, 1);
    cudaMalloc(&d_triangles, scene.triangles.size() * sizeof(Triangle));
    cudaMalloc(&d_planes, scene.planes.size() * sizeof(Plane));
    cudaMalloc(&d_materials, scene.materials.size() * sizeof(Material));
    cudaMalloc(&d_lights, scene.lights.size() * sizeof(Light));

    cudaMemcpy(d_mesh_bvh, scene.mesh_bvh_nodes.data(), scene.mesh_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    cudaMemcpy(d_triangles, scene.triangles.data(), scene.triangles.size() * sizeof(Triangle), cudaMemcpyHostToDevice);
    cudaMemcpy(d_planes, scene.planes.data(), scene.planes.size() * sizeof(Plane), cudaMemcpyHostToDevice);
    cudaMemcpy(d_materials, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    cudaMemcpy(d_lights, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);

    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);

    render_kernel<<<grid, block>>>(
        d_output, width, height, scene.camera,
        d_bvh, d_objects, 0,
        d_strand_bvh, d_strands, d_sweep_points, 0,
        d_mesh_bvh, d_triangles, scene.triangles.size(),
        nullptr, nullptr, 0,
        nullptr, nullptr, 0,
        nullptr, nullptr, nullptr, 0,
        nullptr, nullptr, nullptr, 0,
        nullptr, -1,
        d_planes, scene.planes.size(),
        d_materials, scene.materials.size(),
        nullptr, 0,
        d_lights, scene.lights.size(),
        nullptr, 0,
        SHEEN_AA_SAMPLES, PhotonMap{}, 0.0f
    );
    cudaDeviceSynchronize();

    auto start = std::chrono::high_resolution_clock::now();
    render_kernel<<<grid, block>>>(
        d_output, width, height, scene.camera,
        d_bvh, d_objects, 0,
        d_strand_bvh, d_strands, d_sweep_points, 0,
        d_mesh_bvh, d_triangles, scene.triangles.size(),
        nullptr, nullptr, 0,
        nullptr, nullptr, 0,
        nullptr, nullptr, nullptr, 0,
        nullptr, nullptr, nullptr, 0,
        nullptr, -1,
        d_planes, scene.planes.size(),
        d_materials, scene.materials.size(),
        nullptr, 0,
        d_lights, scene.lights.size(),
        nullptr, 0,
        SHEEN_AA_SAMPLES, PhotonMap{}, 0.0f
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

    stbi_write_png("mesh_output.png", width, height, 3, png_data.data(), width * 3);
    std::cout << "PNG saved: mesh_output.png\n";

    cudaFree(d_output); cudaFree(d_bvh); cudaFree(d_strand_bvh); cudaFree(d_mesh_bvh);
    cudaFree(d_objects); cudaFree(d_strands); cudaFree(d_sweep_points); cudaFree(d_triangles);
    cudaFree(d_planes); cudaFree(d_materials); cudaFree(d_lights);
    return 0;
}
