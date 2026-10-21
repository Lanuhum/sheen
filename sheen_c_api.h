#ifndef SHEEN_C_API_H
#define SHEEN_C_API_H

// Sheen C API — бинарный контракт между аддоном Blender и ядром рендера.
// Никаких файлов, JSON и парсеров: сцена передаётся плоскими массивами
// C-структур (раскладка — types.h). BVH строится внутри C-слоя.
// Структуры копируются по значению (POD), поэтому контракт стабилен
// между процессами и версиями при одинаковом types.h.

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Состав сцены. Все массивы — POD, копируются в GPU как есть.
// Поля передаются указателями; nullptr при нулевом количестве.
typedef struct SheenScene {
    // Камера — единственная «по значению» структура (все поля POD)
    CameraData camera;

    // Сферы (Object.type == 0) + их количество
    const Object* objects;
    int num_objects;

    // Пряди: точки и описатели прядей (SweepPoint/SweepStrand)
    const SweepPoint* sweep_points;
    int num_sweep_points;
    const SweepStrand* sweep_strands;
    int num_sweep_strands;

    // Меш: треугольники с нормалями/UV
    const Triangle* triangles;
    int num_triangles;

    // HeightField: процедурные ландшафты (геометрия как функция)
    const HeightField* heightfields;
    int num_heightfields;

    // Superellipsoid: |x/a|^n+|y/b|^n+|z/c|^n=1 (кубы с фасками)
    const Superellipsoid* superellipsoids;
    int num_superellipsoids;

    // Lathe: тела вращения профиля вокруг Y (точки профиля + описатели)
    const LathePoint* lathe_points;
    int num_lathe_points;
    const Lathe* lathes;
    int num_lathes;

    // Prism: 2D-профиль (XZ), экструзия по Y с полостями по чётности
    const PrismPoint* prism_points;
    int num_prism_points;
    const Prism* prisms;
    int num_prisms;

    // CSG-дерево: узлы (leaf=примитив, interior=операция). csg_root — индекс корня.
    const CSGNode* csg_nodes;
    int csg_root;
    int num_csg_nodes;

    // Плоскости
    const Plane* planes;
    int num_planes;

    // Материалы
    const Material* materials;
    int num_materials;

    // Текстуры: пиксели RGB (по 3 байта на пиксель) + размеры
    const unsigned char* const* texture_pixels; // [num_textures]
    const int* texture_widths;                  // [num_textures]
    const int* texture_heights;                 // [num_textures]
    int num_textures;

    // Направленные источники света
    const Light* lights;
    int num_lights;

    // Рассеивающие среды (ограниченные AABB)
    // Каустика (фотонная карта): photons = 0 — путь выключен
    int photons;
    float photon_cell;
    float caustics;

    const Medium* media;
    int num_media;
} SheenScene;

// Инициализация CUDA. device_id — номер GPU, cuda_arch — compute capability
// (61 для Pascal/GTX 1080 Ti). Returns 0 on success, -1 on error.
int sheen_init(int device_id, int cuda_arch);

// Освобождение всех устройствных буферов и контекста CUDA.
void sheen_cleanup(void);

// Рендер сцены. Вход — бинарный состав сцены (см. SheenScene).
// Выход — PNG по пути output_path.
// Returns 0 on success, -1 on error.
int sheen_render_scene(
    const SheenScene* scene,
    int width, int height,
    int aa_samples,
    const char* output_path
);

// Последняя ошибка (указатель на статический буфер, валиден до след. вызова).
const char* sheen_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // SHEEN_C_API_H