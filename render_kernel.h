#pragma once
#include "types.h"
#include "bvh.h"

// ---------------------------------------------------------
// Фотоны (шаг 1: эмиссия и трассировка, без карты и без влияния на кадр).
// Фотон — попадание в диффузную поверхность после зеркальных/стеклянных
// отражений. Карта и выборка появятся следующим шагом.
// ---------------------------------------------------------
struct alignas(16) Photon {
    float3 position;
    float3 direction;   // направление прихода (от поверхности к свету)
    float3 normal;
    float3 flux;        // цвет × мощность, поделённая на число фотонов
};

// Указатели сцены для прохода фотонов (заполняет хост).
struct PhotonScene {
    const BVHNode* bvh_nodes; const Object* objects; int num_objects;
    const BVHNode* strand_bvh_nodes; const SweepStrand* strands;
    const SweepPoint* sweep_points; int num_strands;
    const BVHNode* mesh_bvh_nodes; const Triangle* triangles; int num_triangles;
    const BVHNode* hf_bvh_nodes; const HeightField* heightfields; int num_heightfields;
    const BVHNode* se_bvh_nodes; const Superellipsoid* superellipsoids; int num_superellipsoids;
    const BVHNode* lathe_bvh_nodes; const Lathe* lathes;
    const LathePoint* lathe_points; int num_lathes;
    const BVHNode* prism_bvh_nodes; const Prism* prisms;
    const PrismPoint* prism_points; int num_prisms;
    const CSGNode* csg_nodes; int csg_root;
    const Plane* planes; int num_planes;
    const Material* materials; int num_materials;
    const Light* lights; int num_lights;
};

struct PhotonStats { int emitted; int stored; float ms; };

// Запускает проход фотонов на уже выделенных буферах устройства.
PhotonStats launch_photons(const PhotonScene& s, int per_light,
                           Photon* d_photons, int max_photons,
                           unsigned int* d_count);

// ---------------------------------------------------------
// Шаг 2: карта фотонов — хеш-сетка с ячейкой cell_size.
// Ячейки не хранятся: индекс — хеш целочисленных координат, диапазон
// фотонов ячейки — [cell_start[c], cell_start[c]+cell_count[c]).
// ---------------------------------------------------------
struct PhotonMap {
    const Photon* photons = nullptr;    // отсортированы по ячейкам
    const int* cell_start = nullptr;
    const int* cell_count = nullptr;
    int num_cells = 0;
    float cell_size = 0.0f;
};

GPU_DEVICE inline int photon_cell_hash(int ix, int iy, int iz, int num_cells) {
    unsigned int h = ((unsigned int)ix * 73856093u) ^ ((unsigned int)iy * 19349663u)
                   ^ ((unsigned int)iz * 83492791u);
    // перемешивание: без него у линейной комбинации есть структурные коллизии,
    // и соседние ячейки попадают в один бакет дважды (фотоны считаются дважды)
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return (int)(h & (unsigned int)(num_cells - 1));
}

// Сумма flux фотонов в окрестности точки, обращённых в ту же сторону, что и
// нормаль (не более max_samples). Нет карты — ноль.
GPU_DEVICE inline float3 photon_gather(const PhotonMap& m, float3 p, float3 n,
                                       float radius, int max_samples,
                                       int* count_out = nullptr) {
    if (!m.photons || m.num_cells <= 0 || m.cell_size <= 0.0f) return {0.0f, 0.0f, 0.0f};
    // диапазон ячеек, накрывающий шар радиуса radius (не больше 2×2×2 при
    // radius <= cell), вместо фиксированных 27 — дешевле в разы
    int x0 = (int)floorf((p.x - radius) / m.cell_size);
    int x1 = (int)floorf((p.x + radius) / m.cell_size);
    int y0 = (int)floorf((p.y - radius) / m.cell_size);
    int y1 = (int)floorf((p.y + radius) / m.cell_size);
    int z0 = (int)floorf((p.z - radius) / m.cell_size);
    int z1 = (int)floorf((p.z + radius) / m.cell_size);
    float r2 = radius * radius;
    float3 acc = {0.0f, 0.0f, 0.0f};
    int taken = 0;

    int cells[8];
    int ncell = 0;
    for (int iz = z0; iz <= z1; iz++)
        for (int iy = y0; iy <= y1; iy++)
            for (int ix = x0; ix <= x1; ix++) {
                int c = photon_cell_hash(ix, iy, iz, m.num_cells);
                bool seen = false;
                for (int q = 0; q < ncell; q++)
                    if (cells[q] == c) { seen = true; break; }
                if (!seen && ncell < 8) cells[ncell++] = c;
            }

    for (int q = 0; q < ncell && taken < max_samples; q++) {
        int c = cells[q];
        int start = m.cell_start[c];
        int cnt = m.cell_count[c];
        for (int k = 0; k < cnt && taken < max_samples; k++) {
            const Photon& ph = m.photons[start + k];
            float3 d = ph.position - p;
            if (dot(d, d) > r2) continue;
            if (dot(ph.normal, n) < 0.1f) continue;
            acc = acc + ph.flux;
            taken++;
        }
    }
    if (count_out) *count_out = taken;
    return acc;
}

// --- хост: построение карты -------------------------------------------
struct PhotonMapBuffers {
    Photon* photons = nullptr;   // отсортированные по ячейкам
    int* cell_start = nullptr;
    int* cell_count = nullptr;
    int num_cells = 0;
    float cell_size = 0.0f;
    int num_photons = 0;
};

struct PhotonMapStats { int used_cells; int max_cell; double build_ms; int min_probe; int avg_probe; int max_probe; };

// Строит карту из готового буфера фотонов (count и prefix — на хосте).
PhotonMapStats build_photon_map(const Photon* d_photons, int n, float cell,
                                PhotonMapBuffers& out);
void free_photon_map(PhotonMapBuffers& m);
PhotonMap photon_map_view(const PhotonMapBuffers& m);

// Проверка: считать фотоны вокруг каждой stride-й точки самого буфера.
void probe_photon_map(const PhotonMapBuffers& m, int stride,
                      int& min_c, int& avg_c, int& max_c);

// Контроль: то же самое перебором всех фотонов (без карты).
void probe_bruteforce(const Photon* d_probes, const Photon* d_all, int n, int stride,
                      int& min_c, int& avg_c, int& max_c);

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
    const BVHNode* mesh_bvh_nodes,
    const Triangle* triangles,
    int num_triangles,
    const BVHNode* hf_bvh_nodes,
    const HeightField* heightfields,
    int num_heightfields,
    const BVHNode* se_bvh_nodes,
    const Superellipsoid* superellipsoids,
    int num_superellipsoids,
    const BVHNode* lathe_bvh_nodes,
    const Lathe* lathes,
    const LathePoint* lathe_points,
    int num_lathes,
    const BVHNode* prism_bvh_nodes,
    const Prism* prisms,
    const PrismPoint* prism_points,
    int num_prisms,
    const CSGNode* csg_nodes,
    int csg_root,
    const Plane* planes,
    int num_planes,
    const Material* materials,
    int num_materials,
    const TextureRef* textures,
    int num_textures,
    const Light* lights,
    int num_lights,
    const Medium* media,
    int num_media,
    int aa_samples,
    PhotonMap photon_map,
    float caustic_scale
);
