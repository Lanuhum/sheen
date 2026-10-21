// Sheen C API — реализация. Принимает бинарный состав сцены (SheenScene),
// строит BVH на хосте (Scene::build_bvh), копирует всё на GPU и запускает
// render_kernel. Запись PNG — через stb_image_write.

#include "sheen_c_api.h"
#include "render_kernel.h"
#include "scene.h"
#include "png_color.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

namespace {

char g_error[512] = "no error";

void set_error(const char* msg) {
    std::strncpy(g_error, msg, sizeof(g_error) - 1);
    g_error[sizeof(g_error) - 1] = '\0';
}

struct DeviceBuffers {
    float4* output = nullptr;
    BVHNode* bvh = nullptr;
    BVHNode* strand_bvh = nullptr;
    BVHNode* mesh_bvh = nullptr;
    BVHNode* hf_bvh = nullptr;
    BVHNode* se_bvh = nullptr;
    BVHNode* lathe_bvh = nullptr;
    BVHNode* prism_bvh = nullptr;
    Object* objects = nullptr;
    SweepStrand* strands = nullptr;
    SweepPoint* points = nullptr;
    Triangle* triangles = nullptr;
    HeightField* heightfields = nullptr;
    Superellipsoid* superellipsoids = nullptr;
    LathePoint* lathe_points = nullptr;
    Lathe* lathes = nullptr;
    PrismPoint* prism_points = nullptr;
    Prism* prisms = nullptr;
    CSGNode* csg_nodes = nullptr;
    Plane* planes = nullptr;
    Material* materials = nullptr;
    Light* lights = nullptr;
    Medium* media = nullptr;
    TextureRef* textures = nullptr;
    std::vector<unsigned char*> tex_pixels;
    std::vector<unsigned char*> heightmap_pixels;   // карты высот HF

    ~DeviceBuffers() {
        cudaFree(output);
        cudaFree(bvh);
        cudaFree(strand_bvh);
        cudaFree(mesh_bvh);
        cudaFree(hf_bvh);
        cudaFree(se_bvh);
        cudaFree(lathe_bvh);
        cudaFree(prism_bvh);
        cudaFree(objects);
        cudaFree(strands);
        cudaFree(points);
        cudaFree(triangles);
        cudaFree(heightfields);
        cudaFree(superellipsoids);
        cudaFree(lathe_points);
        cudaFree(lathes);
        cudaFree(prism_points);
        cudaFree(prisms);
        cudaFree(csg_nodes);
        cudaFree(planes);
        cudaFree(materials);
        cudaFree(lights);
        cudaFree(media);
        cudaFree(textures);
        for (auto* p : tex_pixels) cudaFree(p);
        for (auto* p : heightmap_pixels) cudaFree(p);
    }

    // Никаких копий и перемещений
    DeviceBuffers() = default;
    DeviceBuffers(const DeviceBuffers&) = delete;
    DeviceBuffers& operator=(const DeviceBuffers&) = delete;
};

// Хост-сцена: из плоских массивов в Scene + BVH.
bool build_host_scene(const SheenScene& in, Scene& scene) {
    if (in.objects)
        scene.objects.assign(in.objects, in.objects + in.num_objects);
    if (in.sweep_points)
        scene.sweep_points.assign(in.sweep_points, in.sweep_points + in.num_sweep_points);
    if (in.sweep_strands)
        scene.sweep_strands.assign(in.sweep_strands, in.sweep_strands + in.num_sweep_strands);
    if (in.triangles)
        scene.triangles.assign(in.triangles, in.triangles + in.num_triangles);
    if (in.heightfields)
        scene.heightfields.assign(in.heightfields, in.heightfields + in.num_heightfields);
    if (in.superellipsoids)
        scene.superellipsoids.assign(in.superellipsoids, in.superellipsoids + in.num_superellipsoids);
    if (in.lathe_points)
        scene.lathe_points.assign(in.lathe_points, in.lathe_points + in.num_lathe_points);
    if (in.lathes)
        scene.lathes.assign(in.lathes, in.lathes + in.num_lathes);
    if (in.prism_points)
        scene.prism_points.assign(in.prism_points, in.prism_points + in.num_prism_points);
    if (in.prisms)
        scene.prisms.assign(in.prisms, in.prisms + in.num_prisms);
    if (in.csg_nodes)
        scene.csg_nodes.assign(in.csg_nodes, in.csg_nodes + in.num_csg_nodes);
    scene.csg_root = in.csg_root;
    if (in.planes)
        scene.planes.assign(in.planes, in.planes + in.num_planes);
    if (in.materials)
        scene.materials.assign(in.materials, in.materials + in.num_materials);
    if (in.lights)
        scene.lights.assign(in.lights, in.lights + in.num_lights);
    if (in.media)
        scene.media.assign(in.media, in.media + in.num_media);

    for (int i = 0; i < in.num_textures; i++) {
        Scene::TextureData td;
        td.w = in.texture_widths ? in.texture_widths[i] : 0;
        td.h = in.texture_heights ? in.texture_heights[i] : 0;
        if (in.texture_pixels && in.texture_pixels[i] && td.w > 0 && td.h > 0) {
            td.pixels.assign(in.texture_pixels[i],
                             in.texture_pixels[i] + (size_t)td.w * td.h * 3);
            Scene::build_mips(td);
        }
        scene.textures.push_back(std::move(td));
    }

    scene.camera = in.camera;
    scene.build_bvh();
    return true;
}

// Копирование типизированного вектора на GPU. Возвращает устройствный
// указатель или nullptr (при пустом векторе — nullptr, но это валидно).
template <typename T>
T* copy_to_device(const std::vector<T>& src) {
    if (src.empty()) return nullptr;
    T* d = nullptr;
    if (cudaMalloc(&d, src.size() * sizeof(T)) != cudaSuccess) return nullptr;
    if (cudaMemcpy(d, src.data(), src.size() * sizeof(T), cudaMemcpyHostToDevice) != cudaSuccess) {
        cudaFree(d);
        return nullptr;
    }
    return d;
}

} // namespace

extern "C" {

int sheen_init(int device_id, int cuda_arch) {
    (void)cuda_arch; // архитектура фиксируется сборкой (sm_61)
    cudaError_t e = cudaSetDevice(device_id);
    if (e != cudaSuccess) {
        set_error(cudaGetErrorString(e));
        return -1;
    }
    // CSG-обход рекурсивный и держит локальные массивы интервалов:
    // поднимаем лимит стека потока (дефолт ~1 КБ слишком мал).
    e = cudaDeviceSetLimit(cudaLimitStackSize, 16384);
    if (e != cudaSuccess) {
        set_error(cudaGetErrorString(e));
        return -1;
    }
    e = cudaFree(0); // прогрев контекста
    if (e != cudaSuccess) {
        set_error(cudaGetErrorString(e));
        return -1;
    }
    return 0;
}

void sheen_cleanup(void) {
    cudaDeviceSynchronize();
    cudaDeviceReset();
}

int sheen_render_scene(const SheenScene* in, int width, int height,
                       int aa_samples, const char* output_path) {
    if (!in) { set_error("null scene"); return -1; }
    if (width <= 0 || height <= 0 || aa_samples < 1 || aa_samples > 64 || !output_path) {
        set_error("bad render params"); return -1;
    }
    if (!in->num_materials) { set_error("no materials"); return -1; }

    // 1. Хост-сцена + BVH
    Scene scene;
    if (!build_host_scene(*in, scene)) { set_error("scene build failed"); return -1; }

    // 2. Устройство
    DeviceBuffers db;
    auto alloc = [](size_t n) {
        void* p = nullptr;
        return (cudaMalloc(&p, n ? n : 4) == cudaSuccess) ? p : nullptr;
    };

    db.output = (float4*)alloc((size_t)width * height * sizeof(float4));
    if (!db.output) { set_error("cudaMalloc output failed"); return -1; }

    db.bvh        = copy_to_device(scene.bvh_nodes);
    db.strand_bvh = copy_to_device(scene.strand_bvh_nodes);
    db.mesh_bvh   = copy_to_device(scene.mesh_bvh_nodes);
    db.hf_bvh     = copy_to_device(scene.hf_bvh_nodes);
    db.se_bvh     = copy_to_device(scene.se_bvh_nodes);
    db.lathe_bvh  = copy_to_device(scene.lathe_bvh_nodes);
    db.prism_bvh  = copy_to_device(scene.prism_bvh_nodes);
    db.objects    = copy_to_device(scene.objects);
    db.strands    = copy_to_device(scene.sweep_strands);
    db.points     = copy_to_device(scene.sweep_points);
    db.triangles  = copy_to_device(scene.triangles);
    // HeightFields: копируем структуры, затем карты высот — на GPU.
    // image в HOST-копии указывает на хост-память; в устройственной копии
    // должен указывать на GPU-массив. Поэтому делаем два прохода.
    db.heightfields = copy_to_device(scene.heightfields);
    if (db.heightfields && !scene.heightfields.empty()) {
        for (size_t i = 0; i < scene.heightfields.size(); i++) {
            const HeightField& hf = scene.heightfields[i];
            if (!hf.image || hf.img_w < 1 || hf.img_h < 1) continue;
            size_t n = (size_t)hf.img_w * hf.img_h;
            void* d_img = nullptr;
            if (cudaMalloc(&d_img, n) != cudaSuccess) { set_error("hf image alloc failed"); return -1; }
            cudaMemcpy(d_img, hf.image, n, cudaMemcpyHostToDevice);
            db.heightmap_pixels.push_back((unsigned char*)d_img);
            // адресуем конкретный HF в устройственной копии и правим image
            cudaMemcpy(&db.heightfields[i].image, &d_img, sizeof(void*), cudaMemcpyHostToDevice);
        }
    }
    db.superellipsoids = copy_to_device(scene.superellipsoids);
    db.lathe_points = copy_to_device(scene.lathe_points);
    db.lathes      = copy_to_device(scene.lathes);
    db.prism_points = copy_to_device(scene.prism_points);
    db.prisms       = copy_to_device(scene.prisms);
    db.csg_nodes   = copy_to_device(scene.csg_nodes);
    db.planes     = copy_to_device(scene.planes);
    db.materials  = copy_to_device(scene.materials);
    db.lights     = copy_to_device(scene.lights);
    db.media      = copy_to_device(scene.media);

    // Текстуры: пиксели + референсы
    std::vector<TextureRef> refs(scene.textures.size());
    for (size_t i = 0; i < scene.textures.size(); i++) {
        const auto& t = scene.textures[i];
        if (t.pixels.empty()) { refs[i].pixels = nullptr; continue; }
        void* p = alloc(t.pixels.size());
        if (!p) { set_error("tex alloc failed"); return -1; }
        cudaMemcpy(p, t.pixels.data(), t.pixels.size(), cudaMemcpyHostToDevice);
        db.tex_pixels.push_back((unsigned char*)p);
        refs[i].pixels = (const unsigned char*)p;
        refs[i].width = t.w;
        refs[i].height = t.h;
        refs[i].mips = t.mips;
    }
    if (!refs.empty()) {
        db.textures = (TextureRef*)alloc(refs.size() * sizeof(TextureRef));
        if (!db.textures) { set_error("tex refs alloc failed"); return -1; }
        cudaMemcpy(db.textures, refs.data(), refs.size() * sizeof(TextureRef), cudaMemcpyHostToDevice);
    }

    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) { set_error(cudaGetErrorString(e)); return -1; }

    // Каустика: карта фотонов строится до рендера; photons = 0 — путь выключен
    PhotonMapBuffers pm;
    Photon* d_photons = nullptr;
    unsigned int* d_pcount = nullptr;
    if (in->photons > 0 && in->num_lights > 0) {
        long long cap = (long long)in->photons * in->num_lights;
        if (cap > 4000000) cap = 4000000;
        int max_photons = (int)cap;
        if (cudaMalloc(&d_photons, (size_t)max_photons * sizeof(Photon)) != cudaSuccess ||
            cudaMalloc(&d_pcount, sizeof(unsigned int)) != cudaSuccess) {
            set_error("photon alloc failed");
            return -1;
        }
        PhotonScene ps{};
        ps.bvh_nodes = db.bvh; ps.objects = db.objects;
        ps.num_objects = (int)scene.objects.size();
        ps.strand_bvh_nodes = db.strand_bvh; ps.strands = db.strands;
        ps.sweep_points = db.points; ps.num_strands = (int)scene.sweep_strands.size();
        ps.mesh_bvh_nodes = db.mesh_bvh; ps.triangles = db.triangles;
        ps.num_triangles = (int)scene.triangles.size();
        ps.hf_bvh_nodes = db.hf_bvh; ps.heightfields = db.heightfields;
        ps.num_heightfields = (int)scene.heightfields.size();
        ps.se_bvh_nodes = db.se_bvh; ps.superellipsoids = db.superellipsoids;
        ps.num_superellipsoids = (int)scene.superellipsoids.size();
        ps.lathe_bvh_nodes = db.lathe_bvh; ps.lathes = db.lathes;
        ps.lathe_points = db.lathe_points; ps.num_lathes = (int)scene.lathes.size();
        ps.prism_bvh_nodes = db.prism_bvh; ps.prisms = db.prisms;
        ps.prism_points = db.prism_points; ps.num_prisms = (int)scene.prisms.size();
        ps.csg_nodes = db.csg_nodes; ps.csg_root = scene.csg_root;
        ps.planes = db.planes; ps.num_planes = (int)scene.planes.size();
        ps.materials = db.materials; ps.num_materials = (int)scene.materials.size();
        ps.lights = db.lights; ps.num_lights = (int)scene.lights.size();
        PhotonStats pst = launch_photons(ps, in->photons, d_photons, max_photons, d_pcount);
        if (pst.stored > 0)
            build_photon_map(d_photons, pst.stored,
                             in->photon_cell > 0.0f ? in->photon_cell : 0.25f, pm);
    }

    // 3. Запуск (warmup + замер)
    dim3 block(16, 16);
    dim3 grid((width + 15) / 16, (height + 15) / 16);

    auto launch = [&]() {
        render_kernel<<<grid, block>>>(
            db.output, width, height, scene.camera,
            db.bvh, db.objects, (int)scene.objects.size(),
            db.strand_bvh, db.strands, db.points, (int)scene.sweep_strands.size(),
            db.mesh_bvh, db.triangles, (int)scene.triangles.size(),
            db.hf_bvh, db.heightfields, (int)scene.heightfields.size(),
            db.se_bvh, db.superellipsoids, (int)scene.superellipsoids.size(),
            db.lathe_bvh, db.lathes, db.lathe_points, (int)scene.lathes.size(),
            db.prism_bvh, db.prisms, db.prism_points, (int)scene.prisms.size(),
            db.csg_nodes, scene.csg_root,
            db.planes, (int)scene.planes.size(),
            db.materials, (int)scene.materials.size(),
            db.textures, (int)refs.size(),
            db.lights, (int)scene.lights.size(),
            db.media, (int)scene.media.size(),
            aa_samples, photon_map_view(pm), in->caustics);
    };

    launch();
    if (cudaDeviceSynchronize() != cudaSuccess) {
        set_error(cudaGetErrorString(cudaGetLastError()));
        return -1;
    }

    free_photon_map(pm);
    cudaFree(d_photons);
    cudaFree(d_pcount);

    // 4. Скачивание + PNG
    std::vector<float4> host((size_t)width * height);
    if (cudaMemcpy(host.data(), db.output, host.size() * sizeof(float4), cudaMemcpyDeviceToHost) != cudaSuccess) {
        set_error(cudaGetErrorString(cudaGetLastError()));
        return -1;
    }

    std::vector<unsigned char> png(host.size() * 3);
    for (size_t i = 0; i < host.size(); i++) {
        png[i * 3 + 0] = sheen::linear_to_srgb_byte(host[i].x);
        png[i * 3 + 1] = sheen::linear_to_srgb_byte(host[i].y);
        png[i * 3 + 2] = sheen::linear_to_srgb_byte(host[i].z);
    }
    if (!stbi_write_png(output_path, width, height, 3, png.data(), width * 3)) {
        set_error("stbi_write_png failed");
        return -1;
    }

    return 0;
}

const char* sheen_get_last_error(void) {
    return g_error;
}

} // extern "C"