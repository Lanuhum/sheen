#include <cstdio>
#include <vector>
#include <string>
#include <cstdlib>
#include <chrono>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "render_kernel.h"
#include "scene.h"
#include "png_color.h"
#include "api/scene_io.h"
#include "sampling/aa.h"

int main(int argc, char** argv) {
    const char* path = nullptr;
    const char* out_override = nullptr;
    int w_override = 0, h_override = 0, aa_override = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc)          out_override = argv[++i];
        else if (a == "--width" && i + 1 < argc)   w_override = atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc)  h_override = atoi(argv[++i]);
        else if (a == "--aa" && i + 1 < argc)      aa_override = atoi(argv[++i]);
        else if (a == "-h" || a == "--help") {
            std::printf("usage: scene_render [scene.sheen] [--out file.png] [--width N] [--height N] [--aa N]\n");
            return 0;
        }
        else if (a[0] != '-') path = argv[i];
    }
    if (!path) path = "scenes/demo.sheen";

    // CSG-обход рекурсивный: поднимаем лимит стека потока (дефолт ~1 КБ мал)
    cudaSetDevice(0);
    cudaDeviceSetLimit(cudaLimitStackSize, 16384);

    Scene scene;
    SceneSettings st;
    if (!load_scene_file(path, scene, st)) return 1;

    if (out_override) st.output = out_override;
    if (w_override > 0) st.width = w_override;
    if (h_override > 0) st.height = h_override;
    if (aa_override > 0) st.aa = aa_override;

    const int W = st.width, H = st.height;
    std::printf("scene: %s\n", path);
    std::printf("resolution: %dx%d, AA %d (SHEEN_AA_SAMPLES=%d)\n", W, H, st.aa, SHEEN_AA_SAMPLES);
    std::printf("objects %zu | triangles %zu | strands %zu | points %zu | planes %zu | lights %zu\n",
                scene.objects.size(), scene.triangles.size(), scene.sweep_strands.size(),
                scene.sweep_points.size(), scene.planes.size(), scene.lights.size());
    std::printf("BVH: spheres %zu, strands %zu, mesh %zu\n",
                scene.bvh_nodes.size(), scene.strand_bvh_nodes.size(), scene.mesh_bvh_nodes.size());

    float4* d_output = nullptr;
    BVHNode *d_bvh = nullptr, *d_strand_bvh = nullptr, *d_mesh_bvh = nullptr,
            *d_hf_bvh = nullptr, *d_se_bvh = nullptr, *d_lathe_bvh = nullptr, *d_prism_bvh = nullptr;
    Object* d_objects = nullptr;
    SweepStrand* d_strands = nullptr;
    SweepPoint* d_points = nullptr;
    Triangle* d_triangles = nullptr;
    HeightField* d_heightfields = nullptr;
    Superellipsoid* d_superellipsoids = nullptr;
    LathePoint* d_lathe_points = nullptr;
    Lathe* d_lathes = nullptr;
    PrismPoint* d_prism_points = nullptr;
    Prism* d_prisms = nullptr;
    CSGNode* d_csg_nodes = nullptr;
    Plane* d_planes = nullptr;
    Material* d_materials = nullptr;
    Light* d_lights = nullptr;
    Medium* d_media = nullptr;
    TextureRef* d_textures = nullptr;
    std::vector<TextureRef> tex_refs(scene.textures.size());
    std::vector<unsigned char*> tex_ptrs(scene.textures.size());

    auto alloc = [](size_t bytes) {
        void* p = nullptr;
        cudaMalloc(&p, bytes ? bytes : 4);
        return p;
    };

    cudaMalloc(&d_output, (size_t)W * H * sizeof(float4));
    d_bvh = (BVHNode*)alloc(scene.bvh_nodes.size() * sizeof(BVHNode));
    d_strand_bvh = (BVHNode*)alloc(scene.strand_bvh_nodes.size() * sizeof(BVHNode));
    d_mesh_bvh = (BVHNode*)alloc(scene.mesh_bvh_nodes.size() * sizeof(BVHNode));
    d_hf_bvh = (BVHNode*)alloc(scene.hf_bvh_nodes.size() * sizeof(BVHNode));
    d_se_bvh = (BVHNode*)alloc(scene.se_bvh_nodes.size() * sizeof(BVHNode));
    d_lathe_bvh = (BVHNode*)alloc(scene.lathe_bvh_nodes.size() * sizeof(BVHNode));
    d_prism_bvh = (BVHNode*)alloc(scene.prism_bvh_nodes.size() * sizeof(BVHNode));
    d_objects = (Object*)alloc(scene.objects.size() * sizeof(Object));
    d_strands = (SweepStrand*)alloc(scene.sweep_strands.size() * sizeof(SweepStrand));
    d_points = (SweepPoint*)alloc(scene.sweep_points.size() * sizeof(SweepPoint));
    d_triangles = (Triangle*)alloc(scene.triangles.size() * sizeof(Triangle));
    d_heightfields = (HeightField*)alloc(scene.heightfields.size() * sizeof(HeightField));
    d_superellipsoids = (Superellipsoid*)alloc(scene.superellipsoids.size() * sizeof(Superellipsoid));
    d_lathe_points = (LathePoint*)alloc(scene.lathe_points.size() * sizeof(LathePoint));
    d_lathes = (Lathe*)alloc(scene.lathes.size() * sizeof(Lathe));
    d_prism_points = (PrismPoint*)alloc(scene.prism_points.size() * sizeof(PrismPoint));
    d_prisms = (Prism*)alloc(scene.prisms.size() * sizeof(Prism));
    d_csg_nodes = (CSGNode*)alloc(scene.csg_nodes.size() * sizeof(CSGNode));
    d_planes = (Plane*)alloc(scene.planes.size() * sizeof(Plane));
    d_materials = (Material*)alloc(scene.materials.size() * sizeof(Material));
    d_lights = (Light*)alloc(scene.lights.size() * sizeof(Light));
    d_media = (Medium*)alloc(scene.media.size() * sizeof(Medium));

    for (size_t i = 0; i < scene.textures.size(); i++) {
        const auto& t = scene.textures[i];
        unsigned char* dp = (unsigned char*)alloc(t.pixels.size());
        cudaMemcpy(dp, t.pixels.data(), t.pixels.size(), cudaMemcpyHostToDevice);
        tex_ptrs[i] = dp;
        tex_refs[i].pixels = dp;
        tex_refs[i].width = t.w;
        tex_refs[i].height = t.h;
        tex_refs[i].mips = t.mips;
        // хостовая копия больше не нужна — освобождаем сразу
        scene.textures[i].pixels.clear();
        scene.textures[i].pixels.shrink_to_fit();
    }
    d_textures = (TextureRef*)alloc(tex_refs.size() * sizeof(TextureRef));
    if (!tex_refs.empty())
        cudaMemcpy(d_textures, tex_refs.data(), tex_refs.size() * sizeof(TextureRef), cudaMemcpyHostToDevice);

    if (!scene.bvh_nodes.empty())
        cudaMemcpy(d_bvh, scene.bvh_nodes.data(), scene.bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.strand_bvh_nodes.empty())
        cudaMemcpy(d_strand_bvh, scene.strand_bvh_nodes.data(), scene.strand_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.mesh_bvh_nodes.empty())
        cudaMemcpy(d_mesh_bvh, scene.mesh_bvh_nodes.data(), scene.mesh_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.hf_bvh_nodes.empty())
        cudaMemcpy(d_hf_bvh, scene.hf_bvh_nodes.data(), scene.hf_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.se_bvh_nodes.empty())
        cudaMemcpy(d_se_bvh, scene.se_bvh_nodes.data(), scene.se_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.lathe_bvh_nodes.empty())
        cudaMemcpy(d_lathe_bvh, scene.lathe_bvh_nodes.data(), scene.lathe_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.prism_bvh_nodes.empty())
        cudaMemcpy(d_prism_bvh, scene.prism_bvh_nodes.data(), scene.prism_bvh_nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice);
    if (!scene.objects.empty())
        cudaMemcpy(d_objects, scene.objects.data(), scene.objects.size() * sizeof(Object), cudaMemcpyHostToDevice);
    if (!scene.sweep_strands.empty())
        cudaMemcpy(d_strands, scene.sweep_strands.data(), scene.sweep_strands.size() * sizeof(SweepStrand), cudaMemcpyHostToDevice);
    if (!scene.sweep_points.empty())
        cudaMemcpy(d_points, scene.sweep_points.data(), scene.sweep_points.size() * sizeof(SweepPoint), cudaMemcpyHostToDevice);
    if (!scene.triangles.empty())
        cudaMemcpy(d_triangles, scene.triangles.data(), scene.triangles.size() * sizeof(Triangle), cudaMemcpyHostToDevice);
    if (!scene.heightfields.empty()) {
        cudaMemcpy(d_heightfields, scene.heightfields.data(), scene.heightfields.size() * sizeof(HeightField), cudaMemcpyHostToDevice);
        // карты высот HF: копируем на GPU и переправляем image в устройственной копии
        for (size_t i = 0; i < scene.heightfields.size(); i++) {
            const HeightField& hf = scene.heightfields[i];
            if (!hf.image || hf.img_w < 1 || hf.img_h < 1) continue;
            size_t n = (size_t)hf.img_w * hf.img_h;
            unsigned char* d_img = (unsigned char*)alloc(n);
            cudaMemcpy(d_img, hf.image, n, cudaMemcpyHostToDevice);
            cudaMemcpy(&d_heightfields[i].image, &d_img, sizeof(void*), cudaMemcpyHostToDevice);
        }
    }
    if (!scene.superellipsoids.empty())
        cudaMemcpy(d_superellipsoids, scene.superellipsoids.data(), scene.superellipsoids.size() * sizeof(Superellipsoid), cudaMemcpyHostToDevice);
    if (!scene.lathe_points.empty())
        cudaMemcpy(d_lathe_points, scene.lathe_points.data(), scene.lathe_points.size() * sizeof(LathePoint), cudaMemcpyHostToDevice);
    if (!scene.lathes.empty())
        cudaMemcpy(d_lathes, scene.lathes.data(), scene.lathes.size() * sizeof(Lathe), cudaMemcpyHostToDevice);
    if (!scene.prism_points.empty())
        cudaMemcpy(d_prism_points, scene.prism_points.data(), scene.prism_points.size() * sizeof(PrismPoint), cudaMemcpyHostToDevice);
    if (!scene.prisms.empty())
        cudaMemcpy(d_prisms, scene.prisms.data(), scene.prisms.size() * sizeof(Prism), cudaMemcpyHostToDevice);
    if (!scene.lathe_points.empty())
        cudaMemcpy(d_lathe_points, scene.lathe_points.data(), scene.lathe_points.size() * sizeof(LathePoint), cudaMemcpyHostToDevice);
    if (!scene.lathes.empty())
        cudaMemcpy(d_lathes, scene.lathes.data(), scene.lathes.size() * sizeof(Lathe), cudaMemcpyHostToDevice);
    if (!scene.prism_points.empty())
        cudaMemcpy(d_prism_points, scene.prism_points.data(), scene.prism_points.size() * sizeof(PrismPoint), cudaMemcpyHostToDevice);
    if (!scene.prisms.empty())
        cudaMemcpy(d_prisms, scene.prisms.data(), scene.prisms.size() * sizeof(Prism), cudaMemcpyHostToDevice);
    if (!scene.csg_nodes.empty())
        cudaMemcpy(d_csg_nodes, scene.csg_nodes.data(), scene.csg_nodes.size() * sizeof(CSGNode), cudaMemcpyHostToDevice);
    if (!scene.planes.empty())
        cudaMemcpy(d_planes, scene.planes.data(), scene.planes.size() * sizeof(Plane), cudaMemcpyHostToDevice);
    if (!scene.materials.empty())
        cudaMemcpy(d_materials, scene.materials.data(), scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice);
    if (!scene.lights.empty())
        cudaMemcpy(d_lights, scene.lights.data(), scene.lights.size() * sizeof(Light), cudaMemcpyHostToDevice);
    if (!scene.media.empty())
        cudaMemcpy(d_media, scene.media.data(), scene.media.size() * sizeof(Medium), cudaMemcpyHostToDevice);

    PhotonMapBuffers pm;
    if (st.photons > 0) {
        const int max_photons = 2000000;
        Photon* d_photons = nullptr;
        unsigned int* d_pcount = nullptr;
        cudaMalloc(&d_photons, (size_t)max_photons * sizeof(Photon));
        cudaMalloc(&d_pcount, sizeof(unsigned int));
        PhotonScene ps;
        ps.bvh_nodes = d_bvh; ps.objects = d_objects; ps.num_objects = (int)scene.objects.size();
        ps.strand_bvh_nodes = d_strand_bvh; ps.strands = d_strands;
        ps.sweep_points = d_points; ps.num_strands = (int)scene.sweep_strands.size();
        ps.mesh_bvh_nodes = d_mesh_bvh; ps.triangles = d_triangles;
        ps.num_triangles = (int)scene.triangles.size();
        ps.hf_bvh_nodes = d_hf_bvh; ps.heightfields = d_heightfields;
        ps.num_heightfields = (int)scene.heightfields.size();
        ps.se_bvh_nodes = d_se_bvh; ps.superellipsoids = d_superellipsoids;
        ps.num_superellipsoids = (int)scene.superellipsoids.size();
        ps.lathe_bvh_nodes = d_lathe_bvh; ps.lathes = d_lathes;
        ps.lathe_points = d_lathe_points; ps.num_lathes = (int)scene.lathes.size();
        ps.prism_bvh_nodes = d_prism_bvh; ps.prisms = d_prisms;
        ps.prism_points = d_prism_points; ps.num_prisms = (int)scene.prisms.size();
        ps.csg_nodes = d_csg_nodes; ps.csg_root = scene.csg_root;
        ps.planes = d_planes; ps.num_planes = (int)scene.planes.size();
        ps.materials = d_materials; ps.num_materials = (int)scene.materials.size();
        ps.lights = d_lights; ps.num_lights = (int)scene.lights.size();
        PhotonStats pst = launch_photons(ps, st.photons, d_photons, max_photons, d_pcount);
        std::printf("photons: emitted %d, stored %d, %.2f ms\n",
                    pst.emitted, pst.stored, pst.ms);

        PhotonMapStats pms = build_photon_map(d_photons, pst.stored,
                                              st.photon_cell, pm);
        std::printf("photon map: cells %d (used %d, max %d), cell %.2f, %.2f ms\n",
                    pm.num_cells, pms.used_cells, pms.max_cell,
                    st.photon_cell, pms.build_ms);
        int pmin = 0, pavg = 0, pmax = 0;
        probe_photon_map(pm, 97, pmin, pavg, pmax);
        int bmin = 0, bavg = 0, bmax = 0;
        probe_bruteforce(pm.photons, d_photons, pst.stored, 97, bmin, bavg, bmax);
        std::printf("photon probe: map %d/%d/%d, brute %d/%d/%d\n",
                    pmin, pavg, pmax, bmin, bavg, bmax);
        std::printf("caustics: scale %.3f, map photons %d, cells %d\n",
                    st.caustics, pm.num_photons, pm.num_cells);
        {
            std::vector<Photon> hp(pst.stored);
            cudaMemcpy(hp.data(), d_photons, (size_t)pst.stored * sizeof(Photon),
                       cudaMemcpyDeviceToHost);
            float3 lo = hp[0].position, hi = hp[0].position;
            for (const auto& q : hp) {
                lo.x = std::min(lo.x, q.position.x); lo.y = std::min(lo.y, q.position.y); lo.z = std::min(lo.z, q.position.z);
                hi.x = std::max(hi.x, q.position.x); hi.y = std::max(hi.y, q.position.y); hi.z = std::max(hi.z, q.position.z);
            }
            std::printf("photon bbox: (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n",
                        lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
        }
        cudaFree(d_photons);
        cudaFree(d_pcount);
    }


    dim3 block(16, 16);
    dim3 grid((W + 15) / 16, (H + 15) / 16);

    auto launch = [&]() {
        render_kernel<<<grid, block>>>(
            d_output, W, H, scene.camera,
            d_bvh, d_objects, (int)scene.objects.size(),
            d_strand_bvh, d_strands, d_points, (int)scene.sweep_strands.size(),
            d_mesh_bvh, d_triangles, (int)scene.triangles.size(),
            d_hf_bvh, d_heightfields, (int)scene.heightfields.size(),
            d_se_bvh, d_superellipsoids, (int)scene.superellipsoids.size(),
            d_lathe_bvh, d_lathes, d_lathe_points, (int)scene.lathes.size(),
            d_prism_bvh, d_prisms, d_prism_points, (int)scene.prisms.size(),
            d_csg_nodes, scene.csg_root,
            d_planes, (int)scene.planes.size(),
            d_materials, (int)scene.materials.size(),
            d_textures, (int)tex_refs.size(),
            d_lights, (int)scene.lights.size(),
            d_media, (int)scene.media.size(),
            st.aa,
            photon_map_view(pm), st.caustics);
    };

    launch();
    cudaError_t err0 = cudaDeviceSynchronize();
    if (err0 != cudaSuccess) {
        std::printf("CUDA error after warmup: %s\n", cudaGetErrorString(err0));
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    launch();
    {
        cudaError_t err2 = cudaDeviceSynchronize();
        if (err2 != cudaSuccess) {
            std::printf("CUDA error after render: %s\n", cudaGetErrorString(err2));
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    std::printf("render: %.2f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());

    std::vector<float4> host((size_t)W * H);
    cudaMemcpy(host.data(), d_output, (size_t)W * H * sizeof(float4), cudaMemcpyDeviceToHost);

    std::vector<unsigned char> png((size_t)W * H * 3);
    for (size_t i = 0; i < host.size(); i++) {
        png[i * 3 + 0] = sheen::linear_to_srgb_byte(host[i].x);
        png[i * 3 + 1] = sheen::linear_to_srgb_byte(host[i].y);
        png[i * 3 + 2] = sheen::linear_to_srgb_byte(host[i].z);
    }
    stbi_write_png(st.output.c_str(), W, H, 3, png.data(), W * 3);
    std::printf("saved: %s\n", st.output.c_str());

    free_photon_map(pm);
    cudaFree(d_output); cudaFree(d_bvh); cudaFree(d_strand_bvh); cudaFree(d_mesh_bvh);
    cudaFree(d_hf_bvh); cudaFree(d_objects); cudaFree(d_strands); cudaFree(d_points);
    cudaFree(d_triangles); cudaFree(d_heightfields); cudaFree(d_superellipsoids);
    cudaFree(d_csg_nodes); cudaFree(d_lathe_points); cudaFree(d_lathes); cudaFree(d_lathe_bvh);
    cudaFree(d_prism_points); cudaFree(d_prisms); cudaFree(d_prism_bvh); cudaFree(d_planes);
    cudaFree(d_materials); cudaFree(d_lights); cudaFree(d_media);
    for (auto* p : tex_ptrs) if (p) cudaFree(p);
    if (d_textures) cudaFree(d_textures);
    return 0;
}
