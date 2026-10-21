#pragma once
#include "types.h"
#include "bvh.h"
#include <vector>

// Точка на сплайне Катмулла-Рома между p1 и p2 (p0, p3 — направляющие касательных).
// Матрица как в POV-Ray, core/shape/spheresweep.cpp.
GPU_HOST_DEVICE inline float3 catmull_rom_point(float3 p0, float3 p1, float3 p2, float3 p3, float u) {
    float u2 = u * u, u3 = u2 * u;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * u +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
}

GPU_HOST_DEVICE inline float catmull_rom_scalar(float r0, float r1, float r2, float r3, float u) {
    float u2 = u * u, u3 = u2 * u;
    return 0.5f * (
        (2.0f * r1) +
        (-r0 + r2) * u +
        (2.0f * r0 - 5.0f * r1 + 4.0f * r2 - r3) * u2 +
        (-r0 + 3.0f * r1 - 3.0f * r2 + r3) * u3);
}

struct Scene {
    std::vector<Object> objects;
    std::vector<Plane> planes;
    std::vector<Triangle> triangles;
    struct TextureData { std::vector<unsigned char> pixels; int w = 0, h = 0; int mips = 1; };
    std::vector<TextureData> textures;
    std::vector<SweepPoint> sweep_points;
    std::vector<SweepStrand> sweep_strands;
    std::vector<Material> materials;
    std::vector<Light> lights;
    std::vector<Medium> media;
    std::vector<int> se_hidden;   // SE только для CSG, не рисуются отдельно
    std::vector<HeightField> heightfields;
    std::vector<Superellipsoid> superellipsoids;
    std::vector<LathePoint> lathe_points;
    std::vector<Lathe> lathes;
    std::vector<PrismPoint> prism_points;
    std::vector<Prism> prisms;
    std::vector<CSGNode> csg_nodes;
    int csg_root = -1;   // корень CSG-дерева (-1 — нет дерева)
    CameraData camera;
    std::vector<BVHNode> bvh_nodes;
    std::vector<BVHNode> strand_bvh_nodes;
    std::vector<BVHNode> mesh_bvh_nodes;
    std::vector<BVHNode> hf_bvh_nodes;
    std::vector<BVHNode> se_bvh_nodes;
    std::vector<BVHNode> lathe_bvh_nodes;
    std::vector<BVHNode> prism_bvh_nodes;

    void add_heightfield(float3 center, float size_x, float size_z, float height,
                         float freq, int octaves, int seed, int material_id) {
        HeightField hf;
        hf.center = center;
        hf.size_x = size_x;
        hf.size_z = size_z;
        hf.height = height;
        hf.freq = freq;
        hf.octaves = octaves;
        hf.seed = seed;
        hf.material_id = material_id;
        hf.image = nullptr;
        hf.img_w = 0;
        hf.img_h = 0;
        hf._pad0 = 0;
        heightfields.push_back(hf);
    }

    // HF из grayscale-карты высот (pixels — img_w*img_h байт, 0..255).
    // Владелец массива — вызывающий (копируется и на GPU в C-слое).
    void add_heightfield_from_image(float3 center, float size_x, float size_z,
                                    float height, const unsigned char* pixels,
                                    int img_w, int img_h, int material_id) {
        add_heightfield(center, size_x, size_z, height, 1.0f, 1, -1, material_id);
        HeightField& hf = heightfields.back();
        hf.image = pixels;
        hf.img_w = img_w;
        hf.img_h = img_h;
    }

    void add_superellipsoid(float3 center, float a, float b, float c,
                            float n, int material_id) {
        Superellipsoid se;
        se.center = center;
        se.a = a; se.b = b; se.c = c;
        se.n = n;
        se.material_id = material_id;
        superellipsoids.push_back(se);
    }

    // SE, который участвует только в CSG: не попадает в отдельный BVH.
    void add_superellipsoid_hidden(float3 center, float a, float b, float c,
                                   float n, int material_id) {
        add_superellipsoid(center, a, b, c, n, material_id);
        se_hidden.push_back((int)superellipsoids.size() - 1);
    }

    // Добавляет латех из массива точек профиля (y, radius).
    void add_lathe(float3 center, const std::vector<LathePoint>& pts, int material_id) {
        Lathe l;
        l.center = center;
        l.point_offset = (int)lathe_points.size();
        l.point_count = (int)pts.size();
        l.material_id = material_id;
        for (const auto& p : pts) lathe_points.push_back(p);
        lathes.push_back(l);
    }

    // Добавляет призму: под-контуры в XZ, экструзия по Y [y_min, y_max].
    void add_prism(float3 center, const std::vector<PrismPoint>& pts,
                   float y_min, float y_max, float sweep, int material_id) {
        Prism pr;
        pr.center = center;
        pr.y_min = y_min;
        pr.y_max = y_max;
        pr.sweep = sweep;
        pr.point_offset = (int)prism_points.size();
        pr.point_count = (int)pts.size();
        pr.material_id = material_id;
        for (const auto& p : pts) prism_points.push_back(p);
        prisms.push_back(pr);
    }

    void add_sphere(float3 center, float radius, int material_id) {
        Object obj;
        obj.type = 0;
        obj.material_id = material_id;
        obj.param1 = center;
        obj.param2 = radius;
        objects.push_back(obj);
    }
    
    void add_plane(float3 normal, float d, int material_id) {
        Plane p;
        p.normal = normal;
        p.d = d;
        p.material_id = material_id;
        planes.push_back(p);
    }
    
    // Добавляет прядь: принимает вектор точек (position + radius)
    void add_sphere_sweep(const std::vector<SweepPoint>& pts, int material_id) {
        SweepStrand strand;
        strand.point_offset = (int)sweep_points.size();
        strand.point_count = (int)pts.size();
        strand.material_id = material_id;
        
        for (const auto& p : pts) {
            sweep_points.push_back(p);
        }
        sweep_strands.push_back(strand);
    }

    // Плавный sphere_sweep: тесселяция Катмулла-Рома в линейные сегменты.
    // Кривая проходит через все контрольные точки, концы — фантомные.
    void add_sphere_sweep_smooth(
        const std::vector<SweepPoint>& ctrl,
        int material_id,
        int subdivs = 8)
    {
        int nc = (int)ctrl.size();
        if (nc < 2) return;

        std::vector<SweepPoint> pts;
        pts.reserve(nc + 2);

        SweepPoint ph0 = ctrl[0];
        ph0.position = ctrl[0].position + (ctrl[0].position - ctrl[1].position);
        ph0.radius   = ctrl[0].radius   + (ctrl[0].radius   - ctrl[1].radius);
        pts.push_back(ph0);
        for (const auto& p : ctrl) pts.push_back(p);
        SweepPoint ph1 = ctrl[nc - 1];
        ph1.position = ctrl[nc - 1].position + (ctrl[nc - 1].position - ctrl[nc - 2].position);
        ph1.radius   = ctrl[nc - 1].radius   + (ctrl[nc - 1].radius   - ctrl[nc - 2].radius);
        pts.push_back(ph1);

        int n = (int)pts.size();

        std::vector<SweepPoint> tess;
        tess.push_back(pts[1]);

        for (int i = 1; i < n - 2; i++) {
            float3 p0 = pts[i - 1].position, p1 = pts[i].position;
            float3 p2 = pts[i + 1].position, p3 = pts[i + 2].position;
            float r0 = pts[i - 1].radius, r1 = pts[i].radius;
            float r2 = pts[i + 1].radius, r3 = pts[i + 2].radius;
            for (int j = 1; j <= subdivs; j++) {
                float u = (float)j / (float)subdivs;
                SweepPoint q;
                q.position = catmull_rom_point(p0, p1, p2, p3, u);
                q.radius   = catmull_rom_scalar(r0, r1, r2, r3, u);
                if (q.radius < 1e-5f) q.radius = 1e-5f;
                tess.push_back(q);
            }
        }

        SweepStrand strand;
        strand.point_offset = (int)sweep_points.size();
        strand.point_count  = (int)tess.size();
        strand.material_id  = material_id;
        for (const auto& p : tess) sweep_points.push_back(p);
        sweep_strands.push_back(strand);
    }
    
    void add_triangle(float3 v0, float3 v1, float3 v2, int material_id) {
        add_triangle_n(v0, v1, v2,
                       {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                       material_id);
    }

    void add_triangle_n(float3 v0, float3 v1, float3 v2,
                        float3 n0, float3 n1, float3 n2, int material_id,
                        float2 t0 = {0.0f, 0.0f}, float2 t1 = {0.0f, 0.0f},
                        float2 t2 = {0.0f, 0.0f}) {
        Triangle t;
        t.v0 = v0; t.v1 = v1; t.v2 = v2;
        t.n0 = n0; t.n1 = n1; t.n2 = n2;
        t.uv0 = t0; t.uv1 = t1; t.uv2 = t2;
        t.material_id = material_id;
        triangles.push_back(t);
    }

    // Достроить цепочку мипов (box 2×2) в тот же буфер: уровни идут подряд,
    // каждый следующий — половина предыдущего (не меньше 1). Ядро считает
    // смещение уровня по width/height, поэтому отдельные указатели не нужны.
    static void build_mips(TextureData& t) {
        t.mips = 1;
        if (t.w < 2 && t.h < 2) return;
        std::vector<unsigned char> chain(t.pixels);
        int sw = t.w, sh = t.h;
        size_t src = 0;
        while (sw > 1 || sh > 1) {
            int nw = sw > 1 ? sw / 2 : 1;
            int nh = sh > 1 ? sh / 2 : 1;
            size_t dst = chain.size();
            chain.resize(dst + (size_t)nw * nh * 3);
            for (int y = 0; y < nh; y++) {
                int y0 = (2 * y) < sh ? 2 * y : sh - 1;
                int y1 = (2 * y + 1) < sh ? 2 * y + 1 : sh - 1;
                for (int x = 0; x < nw; x++) {
                    int x0 = (2 * x) < sw ? 2 * x : sw - 1;
                    int x1 = (2 * x + 1) < sw ? 2 * x + 1 : sw - 1;
                    for (int c = 0; c < 3; c++) {
                        int s = chain[src + ((size_t)y0 * sw + x0) * 3 + c]
                              + chain[src + ((size_t)y0 * sw + x1) * 3 + c]
                              + chain[src + ((size_t)y1 * sw + x0) * 3 + c]
                              + chain[src + ((size_t)y1 * sw + x1) * 3 + c];
                        chain[dst + ((size_t)y * nw + x) * 3 + c] =
                            (unsigned char)((s + 2) / 4);
                    }
                }
            }
            src = dst; sw = nw; sh = nh;
            t.mips++;
        }
        t.pixels.swap(chain);
    }

    void add_texture(int w, int h, const std::vector<unsigned char>& px) {
        TextureData d; d.w = w; d.h = h; d.pixels = px;
        build_mips(d);
        textures.push_back(std::move(d));
    }

    // Куб из 12 треугольников с UV-развёрткой по граням (0..1 на грань).
    // Без UV все вершины давали (0,0) — текстура вырождалась в один тексель.
    void add_cube(float3 c, float s, int material_id) {
        float3 v[8];
        int k = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                for (int l = 0; l < 2; l++)
                    v[k++] = {c.x + (i ? s : -s), c.y + (j ? s : -s), c.z + (l ? s : -s)};
        // индексы вершин: 0..7 = (x,y,z) двоично: bit2=x, bit1=y, bit0=z
        const int f[12][3] = {
            {0,1,3},{0,3,2},   // -x
            {4,6,7},{4,7,5},   // +x
            {0,4,5},{0,5,1},   // -y
            {2,3,7},{2,7,6},   // +y
            {0,2,6},{0,6,4},   // -z
            {1,5,7},{1,7,3}    // +z
        };
        // ось грани: 0 — X, 1 — Y, 2 — Z; две другие оси дают UV
        const int axis[6] = {0, 0, 1, 1, 2, 2};
        auto uv_of = [&](float3 p, int a) {
            float u = (a == 0) ? (p.z - (c.z - s)) : (p.x - (c.x - s));
            float w = (a == 1) ? (p.z - (c.z - s)) : (p.y - (c.y - s));
            return make_float2(u / (2.0f * s), w / (2.0f * s));
        };
        for (int i = 0; i < 12; i++) {
            float3 a = v[f[i][0]], b = v[f[i][1]], d = v[f[i][2]];
            int ax = axis[i / 2];
            add_triangle_n(a, b, d,
                           {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                           material_id, uv_of(a, ax), uv_of(b, ax), uv_of(d, ax));
        }
    }

    void add_material(float3 color, float specular, float shininess, float ambient,
                      float anisotropy = 0.0f, float hair_shift = 0.1f,
                      float hair_spec_r = 0.0f, float hair_exp_r = 0.0f,
                      float hair_spec_t = 0.0f, float hair_exp_t = 0.0f,
                      float hair_backlight = 0.0f) {
        Material mat = {};   // иначе reflect/transmission/ior — мусор со стека
        mat.diffuse_color = color;
        mat.specular = specular;
        mat.shininess = shininess;
        mat.ambient = ambient;
        mat.anisotropy = anisotropy;
        mat.hair_shift = hair_shift;
        mat.hair_spec_r = hair_spec_r;
        mat.hair_exp_r = hair_exp_r;
        mat.hair_spec_t = hair_spec_t;
        mat.hair_exp_t = hair_exp_t;
        mat.hair_backlight = hair_backlight;
        mat.texture_id = -1;
        mat.mask_id = -1;
        mat.reflect = 0.0f;
        mat.transmission = 0.0f;
        mat.ior = 1.45f;
        mat.emission_color = {0.0f, 0.0f, 0.0f};
        mat.emission = 0.0f;
        mat.normal_id = -1;
        mat.normal_strength = 1.0f;
        materials.push_back(mat);
    }
    
    void add_directional_light(float3 direction, float3 color, float intensity) {
        Light light = make_light();
        light.type = 0;                       // SUN
        light.direction = normalize(direction);
        light.color = color;
        light.intensity = intensity;
        lights.push_back(light);
    }

    void add_point_light(float3 position, float3 color, float power, float radius) {
        Light light = make_light();
        light.type = 1;                       // POINT
        light.position = position;
        light.color = color;
        light.intensity = power;
        light.radius = radius;
        lights.push_back(light);
    }

    void add_area_light(float3 position, float3 u_axis, float3 v_axis,
                        float3 color, float power) {
        Light light = make_light();
        light.type = 2;                       // AREA
        light.position = position;
        light.u_axis = u_axis;
        light.v_axis = v_axis;
        light.color = color;
        light.intensity = power;
        lights.push_back(light);
    }

    void add_medium(float3 center, float3 half, float3 sigma_s,
                    float sigma_t, int samples) {
        Medium m;
        m.center = center;
        m.half = half;
        m.sigma_s = sigma_s;
        m.sigma_t = sigma_t;
        m.samples = samples;
        m._pad0 = 0;
        media.push_back(m);
    }

    void add_spot_light(float3 position, float3 direction, float3 color,
                        float power, float half_angle, float blend, float radius,
                        float beam) {
        Light light = make_light();
        light.type = 3;                       // SPOT
        light.position = position;
        light.direction = normalize(direction);
        light.color = color;
        light.intensity = power;
        light.radius = radius;
        light.angle = half_angle;
        light.blend = blend;
        light.beam = beam;
        lights.push_back(light);
    }

    static Light make_light() {
        Light light = {};
        light.direction = {0.0f, 0.0f, -1.0f};
        light.position = {0.0f, 0.0f, 0.0f};
        light.u_axis = {0.0f, 0.0f, 0.0f};
        light.v_axis = {0.0f, 0.0f, 0.0f};
        light.color = {1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;
        light.radius = 0.0f;
        light.angle = 0.0f;
        light.blend = 0.0f;
        light.beam = 0.0f;
        light.type = 0;
        light.flags = 1;                      // по умолчанию свет видит среду
        light.texture_id = -1;                // gobo нет
        light._pad1 = 0;
        return light;
    }

    void build_bvh() {
        // BVH для сфер
        std::vector<int> sphere_indices;
        for (size_t i = 0; i < objects.size(); i++) {
            if (objects[i].type == 0) {
                sphere_indices.push_back(i);
            }
        }
        bvh_nodes.clear();
        if (!sphere_indices.empty()) {
            build_bvh_recursive(bvh_nodes, objects, sphere_indices, 0, sphere_indices.size());
        }
        
        // BVH для прядей
        std::vector<int> strand_indices;
        for (size_t i = 0; i < sweep_strands.size(); i++) {
            strand_indices.push_back(i);
        }
        strand_bvh_nodes.clear();
        if (!strand_indices.empty()) {
            build_strand_bvh_recursive(
                strand_bvh_nodes, sweep_strands, sweep_points.data(),
                strand_indices, 0, strand_indices.size()
            );
        }

        // BVH для треугольников
        std::vector<int> tri_indices;
        for (size_t i = 0; i < triangles.size(); i++) tri_indices.push_back((int)i);
        mesh_bvh_nodes.clear();
        if (!tri_indices.empty()) {
            build_mesh_bvh_recursive(mesh_bvh_nodes, triangles, tri_indices, 0, tri_indices.size());
        }

        // BVH для HeightField
        std::vector<int> hf_indices;
        for (size_t i = 0; i < heightfields.size(); i++) hf_indices.push_back((int)i);
        hf_bvh_nodes.clear();
        if (!hf_indices.empty()) {
            build_hf_bvh_recursive(hf_bvh_nodes, heightfields, hf_indices, 0, hf_indices.size());
        }

        // BVH для Superellipsoid
        std::vector<int> se_indices;
        for (size_t i = 0; i < superellipsoids.size(); i++) {
            bool hidden = false;
            for (int h : se_hidden) if (h == (int)i) { hidden = true; break; }
            if (!hidden) se_indices.push_back((int)i);
        }
        se_bvh_nodes.clear();
        if (!se_indices.empty()) {
            build_se_bvh_recursive(se_bvh_nodes, superellipsoids, se_indices, 0, se_indices.size());
        }

        // BVH для Lathe
        std::vector<int> lathe_indices;
        for (size_t i = 0; i < lathes.size(); i++) lathe_indices.push_back((int)i);
        lathe_bvh_nodes.clear();
        if (!lathe_indices.empty()) {
            build_lathe_bvh_recursive(lathe_bvh_nodes, lathes, lathe_points.data(),
                                      lathe_indices, 0, lathe_indices.size());
        }

        // BVH для Prism
        std::vector<int> prism_indices;
        for (size_t i = 0; i < prisms.size(); i++) prism_indices.push_back((int)i);
        prism_bvh_nodes.clear();
        if (!prism_indices.empty()) {
            build_prism_bvh_recursive(prism_bvh_nodes, prisms, prism_points.data(),
                                      prism_indices, 0, prism_indices.size());
        }
    }
};