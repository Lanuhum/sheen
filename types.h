#pragma once
#include "gpu_compat.h"

// ---------------------------------------------------------
// 1. Transform
// ---------------------------------------------------------
struct alignas(16) Transform {
    float m[12]; 
    GPU_HOST_DEVICE inline float3 transform_point(float3 p) const {
        return {m[0]*p.x + m[1]*p.y + m[2]*p.z + m[3],
                m[4]*p.x + m[5]*p.y + m[6]*p.z + m[7],
                m[8]*p.x + m[9]*p.y + m[10]*p.z + m[11]};
    }
    GPU_HOST_DEVICE inline float3 transform_vector(float3 v) const {
        return {m[0]*v.x + m[1]*v.y + m[2]*v.z,
                m[4]*v.x + m[5]*v.y + m[6]*v.z,
                m[8]*v.x + m[9]*v.y + m[10]*v.z};
    }
};

// ---------------------------------------------------------
// 2. Ray
// ---------------------------------------------------------
struct alignas(16) Ray {
    float3 origin;
    float3 direction;
    float  t_min;
    float  t_max;
};

// ---------------------------------------------------------
// 3. HitRecord
// ---------------------------------------------------------
struct alignas(16) HitRecord {
    float  t;
    int    material_id;
    int    object_id;
    int    hit;
    float3 point;
    float3 normal;
    float3 tangent;   // касательная пряди (для анизотропного затенения)
    float2 uv;        // для текстур
    float  _pad0;
};

// ---------------------------------------------------------
// 4. CameraData
// ---------------------------------------------------------
struct alignas(16) CameraData {
    float3 position;
    float3 right;
    float3 up;
    float3 forward;
    float  focal_length = 0.0f;
    float  sensor_width = 0.0f;
    float  shift_x = 0.0f;
    float  shift_y = 0.0f;
};

// ---------------------------------------------------------
// 5. Material
// ---------------------------------------------------------
struct alignas(16) Material {
    float3 diffuse_color;
    float  specular;
    float  shininess;
    float  ambient;
    int    texture_id;    // -1 — без текстуры
    int    _pad_tex;
    float  anisotropy;    // 0 — обычный Blinn-Phong, >0 — модель волос
    float  hair_shift;    // сдвиг касательной для лепестков
    float  hair_spec_r;   // вес R-лепестка (прямое отражение)
    float  hair_exp_r;    // экспонента R-лепестка
    float  hair_spec_t;   // вес TRT-лепестка (прошёл сквозь волос)
    float  hair_exp_t;    // экспонента TRT-лепестка
    float  hair_backlight;// диффуз с изнанки пряди
    float  reflect;       // 0 — без отражения, 1 — зеркало
    float  transmission;  // 0 — непрозрачный, 1 — стекло
    float  ior;           // показатель преломления (1.45 — стекло)
    float3 emission_color; // собственное свечение (линейный цвет)
    float  emission;      // сила свечения (0 — не светится)
    int    mask_id;       // карта свойств: R=transmission, G=IOR-1, B=roughness
    int    normal_id;     // карта нормалей (тангентное пространство), -1 — нет
    float  normal_strength; // сила рельефа
};

// ---------------------------------------------------------
// 5b. Triangle (меш)
// ---------------------------------------------------------
struct alignas(16) Triangle {
    float3 v0, v1, v2;
    float3 n0, n1, n2;   // нормали вершин; нули означают плоское затенение
    float2 uv0, uv1, uv2;
    int    material_id;
    int    _pad0, _pad1;
};

// Текстура на устройстве: пиксели идут цепочкой мипов подряд,
// уровень l начинается со смещения, которое считается по width/height.
struct alignas(16) TextureRef {
    const unsigned char* pixels;
    int width;
    int height;
    int mips;          // число уровней (1 — только базовый)
};

// ---------------------------------------------------------
// 6. Object (сферы, type == 0)
// ---------------------------------------------------------
struct alignas(16) Object {
    int    type;
    int    material_id;
    float3 param1;
    float  param2;
    float  _pad0, _pad1, _pad2;
};

// ---------------------------------------------------------
// 7. Light
// ---------------------------------------------------------
// 7. Light — источник света.
//    type 0: SUN   — direction (куда светит), intensity = Вт/м².
//    type 1: POINT — position, intensity = Вт (мощность), radius — мягкая тень.
//    type 2: AREA  — position, u_axis/v_axis — полуоси прямоугольника,
//                    intensity = Вт, мягкая тень из площади.
//    type 3: SPOT  — position + direction, angle — половина угла конуса,
//                    blend — мягкость края, beam — яркость видимого конуса
//                    (0 — поток света не рисуется), radius — мягкая тень.
//    flags bit0 = 1: свет взаимодействует со средой (media_interaction).
struct alignas(16) Light {
    float3 direction;
    float3 position;
    float3 u_axis;
    float3 v_axis;
    float3 color;
    float  intensity;
    float  radius;
    float  angle;      // SPOT: половина угла конуса, радианы
    float  blend;      // SPOT: мягкость края, 0..1
    float  beam;       // SPOT: яркость видимого конуса (0 — выключен)
    int    type;
    int    flags;      // bit0: свет взаимодействует со средой
    int    texture_id; // SPOT: gobo-текстура (-1 — нет)
    int    _pad1;
};

// ---------------------------------------------------------
// 7b. Medium — рассеивающая среда в ограниченном боксе (AABB).
//     Однократное рассеяние: sigma_s — цвет рассеяния, sigma_t — ослабление,
//     samples — число шагов вдоль луча (0 — среда не считается).
// ---------------------------------------------------------
struct alignas(16) Medium {
    float3 center;
    float3 half;
    float3 sigma_s;
    float  sigma_t;
    int    samples;
    int    _pad0;
};

// ---------------------------------------------------------
// 8. Plane (отдельно от BVH)
// ---------------------------------------------------------
struct alignas(16) Plane {
    float3 normal;
    float  d;
    int    material_id;
    int    _pad0, _pad1, _pad2;
};

// ---------------------------------------------------------
// 9. SweepPoint (одна контрольная точка пряди, 16 байт)
// ---------------------------------------------------------
struct alignas(16) SweepPoint {
    float3 position;  // 12 bytes
    float  radius;    // 4 bytes
};

// ---------------------------------------------------------
// 10. SweepStrand (описание одной пряди, 32 байта)
// ---------------------------------------------------------
struct alignas(16) SweepStrand {
    int point_offset;   // индекс начала в массиве sweep_points[]
    int point_count;    // количество точек в этой пряди
    int material_id;
    int _pad0;
};

// ---------------------------------------------------------
// 11. SceneData
// ---------------------------------------------------------
struct SceneData {
    int num_objects;
    int num_materials;
    int num_lights;
};

// ---------------------------------------------------------
// 12. HeightField (процедурный ландшафт, геометрия как функция y=h(x,z))
// ---------------------------------------------------------
struct alignas(16) HeightField {
    float3 center;      // центр основания тайла
    float  size_x;      // размер по X
    float  size_z;      // размер по Z
    float  height;      // амплитуда высоты (y от center.y до center.y+height)
    float  freq;        // базовая частота шума (ед. мира на ячейку)
    int    octaves;     // октав fBm (уровень детализации процедурного режима)
    int    seed;        // зерно генератора (процедурный режим; -1 = карта высот)
    int    material_id;
    // --- Карта высот (градации серого) ---
    // Если image != nullptr — высота берётся из изображения (grayscale,
    // img_w*img_h байт), а не из процедурного шума. Пиксели — на GPU.
    const unsigned char* image;   // карта высот (1 байт/пиксель) или nullptr
    int    img_w;                 // ширина карты (в пикселях)
    int    img_h;                 // высота карты (в пикселях)
    int    _pad0;
};

// ---------------------------------------------------------
// 14. Lathe: тело вращения профиля вокруг оси Y.
// Профиль — цепочка точек (LathePoint: y + радиус), каждые две —
// конический сегмент с осью Y (как sphere_sweep, но без полусфер на концах).
// Список точек общий для всех латехов (point_offset/count в Lathe).
// ---------------------------------------------------------
struct alignas(16) LathePoint {
    float y;      // высота вдоль оси Y
    float radius; // радиус на этой высоте
    int   _pad0, _pad1;
};

struct alignas(16) Lathe {
    float3 center;      // центр основания (смещение профиля по осям)
    int    point_offset; // начало профиля в lathe_points[]
    int    point_count;  // число точек профиля
    int    material_id;
    int    _pad0;
};

// ---------------------------------------------------------
// 15. Prism (семантика POV-Ray): 2D-профиль в плоскости XZ, экструзия по Y.
// Профиль = один или несколько замкнутых под-контуров (linear_spline):
// каждый закрывается повтором первой точки; где накладывается чётное
// число под-контуров — там дырка.
// ---------------------------------------------------------
struct alignas(16) PrismPoint {
    float x;      // координата в плоскости профиля (XZ)
    float z;
    int   _pad0, _pad1;
};

struct alignas(16) Prism {
    float3 center;      // смещение центра профиля
    float  y_min;       // нижняя грань (по Y)
    float  y_max;       // верхняя грань
    float  sweep;       // 0 — linear (стены вертикальные), 1 — conic (конусный)
    int    point_offset; // начало точек в prism_points[]
    int    point_count;  // общее число точек (все под-контуры)
    int    material_id;
    int    _pad0;
};

// ---------------------------------------------------------
// CSG-интервал: отрезок [t_enter, t_exit] внутри тела примитива.
// Материал — того примитива, чей объём дал этот отрезок.
// ---------------------------------------------------------
struct alignas(16) CSGInterval {
    float t_enter;      // вход в тело (вдоль луча)
    float t_exit;       // выход из тела
    int   material_id;  // материал примитива
    int   leaf_id;      // индекс листа (для нормали/точки)
    int   kind;         // 0=sphere, 1=superellipsoid, 2=heightfield, ...
    int   _pad0;
};

// ---------------------------------------------------------
// CSGNode: узел дерева CSG.
// Лист: kind==1 ссылается на примитив (leaf_kind, leaf_index).
// Внутренний: kind==0, op (0=union,1=intersection,2=difference),
// дети indices left/right в массиве csg_nodes.
// ---------------------------------------------------------
struct alignas(16) CSGNode {
    int op;            // 0=union, 1=intersection, 2=difference; для листа не используется
    int leaf_kind;     // -1 для внутреннего; 0=sphere, 1=superellipsoid, 2=heightfield, 3=mesh, 4=plane
    int leaf_index;    // индекс в соответствующем массиве примитивов
    int left;          // индекс левого ребёнка (внутренний) или -1
    int right;         // индекс правого ребёнка (внутренний) или -1
};

// ---------------------------------------------------------
// 13. Superellipsoid: |x/a|^n + |y/b|^n + |z/c|^n = 1.
// n=2 — эллипсоид, n>2 — куб с фасками (n≈8..16 — почти куб).
// ---------------------------------------------------------
struct alignas(16) Superellipsoid {
    float3 center;
    float  a;           // полуоси
    float  b;
    float  c;
    float  n;           // показатель (>= 2)
    int    material_id;
    int    _pad0, _pad1;
};

// ---------------------------------------------------------
// Вспомогательные функции
// ---------------------------------------------------------
GPU_HOST_DEVICE inline float dot(float3 a, float3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

GPU_HOST_DEVICE inline float3 cross(float3 a, float3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}

GPU_HOST_DEVICE inline float3 normalize(float3 v) {
    float invLen = rsqrtf(dot(v, v));
    return {v.x * invLen, v.y * invLen, v.z * invLen};
}

GPU_HOST_DEVICE inline float3 operator+(float3 a, float3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
GPU_HOST_DEVICE inline float3 operator-(float3 a, float3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
GPU_HOST_DEVICE inline float3 operator-(float3 a) { return {-a.x, -a.y, -a.z}; }
GPU_HOST_DEVICE inline float3 operator*(float3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
GPU_HOST_DEVICE inline float3 operator*(float b, float3 a) { return {a.x*b, a.y*b, a.z*b}; }
GPU_HOST_DEVICE inline float3 operator*(float3 a, float3 b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
GPU_HOST_DEVICE inline float3 operator/(float3 a, float b) { return {a.x/b, a.y/b, a.z/b}; }
GPU_HOST_DEVICE inline float3 operator/(float3 a, float3 b) { return {a.x/b.x, a.y/b.y, a.z/b.z}; }
GPU_HOST_DEVICE inline float3& operator+=(float3& a, float3 b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
GPU_HOST_DEVICE inline float3& operator-=(float3& a, float3 b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; return a; }
GPU_HOST_DEVICE inline float3& operator*=(float3& a, float b) { a.x*=b; a.y*=b; a.z*=b; return a; }