#include <cstdio>
#include <chrono>
#include <vector>
#include "render_kernel.h"
#include "intersect.h"
#include "bvh.h"
#include "integrator/raycast.h"
#include "sampling/aa.h"
#include "integrator/shading.h"

// Направление на солнце с учётом углового размера: jitter в конусе
// радиуса light.angle (радианы). При angle = 0 — прежнее жёсткое солнце.
GPU_DEVICE inline float3 sun_dir(const Light& light, unsigned int seed) {
    float3 base = -light.direction;
    if (light.angle <= 0.0f) return base;

    float3 up = (fabsf(base.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                        : make_float3(1.0f, 0.0f, 0.0f);
    float3 t1 = normalize(cross(base, up));
    float3 t2 = cross(base, t1);
    float r = sqrtf(aa_rand(seed ^ 0x68e31da4u)) * tanf(light.angle);
    float phi = 6.2831853f * aa_rand(seed ^ 0xb5297a4du);
    return normalize(base + t1 * (cosf(phi) * r) + t2 * (sinf(phi) * r));
}

GPU_DEVICE inline float3 compute_lighting(
    const HitRecord& rec,
    const Material* materials,
    const TextureRef* textures,
    int num_textures,
    const Light* lights,
    int num_lights,
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
    float3 view_dir,
    float footprint,
    PhotonMap photon_map,
    float caustic_scale,
    unsigned int seed
) {
    const Material& mat = materials[rec.material_id];
    ShadeInput in;
    in.point    = rec.point;
    in.normal   = rec.normal;
    in.tangent  = rec.tangent;
    in.uv       = rec.uv;
    in.view_dir = view_dir;

    // Размер пикселя в единицах UV: по умолчанию 1 UV = 1 единица мира
    // (так устроены HF/SE), для мешей — по плотности UV треугольника.
    float uv_fp = footprint;
    // Тангенциальный базис из UV треугольника: без него карта нормалей
    // ложится в произвольную ориентацию (для изотропной детали незаметно,
    // для направленной — ткань, дерево, поры — нет).
    float3 tan_u = {0.0f, 0.0f, 0.0f}, tan_v = {0.0f, 0.0f, 0.0f};
    if (rec.object_id < 0 && triangles && num_triangles > 0) {
        int ti = -rec.object_id - 1;
        if (ti >= 0 && ti < num_triangles) {
            const Triangle& tr = triangles[ti];
            float3 e1 = tr.v1 - tr.v0, e2 = tr.v2 - tr.v0;
            float2 d1 = {tr.uv1.x - tr.uv0.x, tr.uv1.y - tr.uv0.y};
            float2 d2 = {tr.uv2.x - tr.uv0.x, tr.uv2.y - tr.uv0.y};
            float w1 = sqrtf(dot(e1, e1)), w2 = sqrtf(dot(e2, e2));
            float u1 = sqrtf(d1.x * d1.x + d1.y * d1.y);
            float u2 = sqrtf(d2.x * d2.x + d2.y * d2.y);
            float a1 = (w1 > 1.0e-6f) ? u1 / w1 : 0.0f;
            float a2 = (w2 > 1.0e-6f) ? u2 / w2 : 0.0f;
            float dens = (a1 > a2) ? a1 : a2;
            if (dens > 1.0e-6f) uv_fp = footprint * dens;

            float det = d1.x * d2.y - d2.x * d1.y;
            if (fabsf(det) > 1.0e-8f) {
                // dp/du и dp/dv из решения 2x2-системы по рёбрам
                float3 du = (e1 * d2.y - e2 * d1.y) / det;
                float3 dv = (e2 * d1.x - e1 * d2.x) / det;
                float3 n0 = rec.normal;
                du = du - n0 * dot(du, n0);
                dv = dv - n0 * dot(dv, n0);
                float lu = dot(du, du);
                if (lu > 1.0e-12f) {
                    tan_u = du * rsqrtf(lu);
                    dv = dv - tan_u * dot(dv, tan_u);
                    float lv = dot(dv, dv);
                    if (lv > 1.0e-12f) tan_v = dv * rsqrtf(lv);
                }
            }
        }
    }
    in.uv_fp = uv_fp;

    // Карта нормалей: тангентное пространство разворачиваем в мир.
    // Есть UV-базис — берём его, иначе строим произвольный от нормали.
    if (mat.normal_id >= 0 && mat.normal_id < num_textures) {
        const TextureRef& nt = textures[mat.normal_id];
        float3 ntex = sample_texture_lod(nt, in.uv, tex_lod(nt, in.uv_fp));
        float nx = (ntex.x * 2.0f - 1.0f) * mat.normal_strength;
        float ny = (ntex.y * 2.0f - 1.0f) * mat.normal_strength;
        float3 n0 = in.normal;
        float3 t1, t2;
        if (dot(tan_u, tan_u) > 0.5f && dot(tan_v, tan_v) > 0.5f) {
            t1 = tan_u;
            t2 = tan_v;
        } else {
            float3 up = (fabsf(n0.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                             : make_float3(1.0f, 0.0f, 0.0f);
            t1 = normalize(cross(up, n0));
            t2 = cross(n0, t1);
        }
        in.normal = normalize(n0 + t1 * nx + t2 * ny);
    }

    float3 base = albedo(mat, in, textures, num_textures);
    // Собственное свечение материала: видимая лампа, а не только её свет.
    // Не зависит от источников и не затеняется — светится сама поверхность.
    float3 result = base * mat.ambient + mat.emission_color * mat.emission;

    // Карта свойств: глянец берётся из B, чтобы золото/слюда блестели
    // иначе, чем камень, в пределах одного материала.
    float shininess = mat.shininess;
    if (has_mask(mat, num_textures)) {
        const TextureRef& mt = textures[mat.mask_id];
        float3 mask = sample_texture_lod(mt, in.uv, tex_lod(mt, in.uv_fp));
        shininess = roughness_to_shininess(mask.z);
    }

    for (int i = 0; i < num_lights; i++) {
        const Light& light = lights[i];
        unsigned int light_seed = seed ^ (unsigned int)(i * 2654435761u);

        float3 L;          // направление НА источник
        float3 radiance;   // цвет * мощность * затухание
        float3 target;     // точка, на которую бросаем теневой луч
        float t_max = 1e10f;

        if (light.type == 0) {
            // SUN: направленный, затухания нет; angle — угловой радиус
            L = sun_dir(light, light_seed);
            radiance = light.color * light.intensity;
            target = rec.point + L * 1.0e6f;
        } else {
            // POINT / SPOT / AREA: выбираем точку на источнике
            float3 sample_pos = light.position;
            if (light.type == 2) {
                float ru = aa_rand(light_seed) * 2.0f - 1.0f;
                float rv = aa_rand(light_seed ^ 0x9e3779b9u) * 2.0f - 1.0f;
                sample_pos = light.position + light.u_axis * ru + light.v_axis * rv;
            }

            float3 to_light = sample_pos - rec.point;
            float dist2 = fmaxf(dot(to_light, to_light), 1.0e-4f);
            float dist = sqrtf(dist2);
            L = to_light / dist;

            // мягкая тень точечного/прожекторного источника: сдвиг по диску
            if ((light.type == 1 || light.type == 3) && light.radius > 0.0f) {
                float3 up = (fabsf(L.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                                : make_float3(1.0f, 0.0f, 0.0f);
                float3 t1 = normalize(cross(L, up));
                float3 t2 = cross(L, t1);
                float r = sqrtf(aa_rand(light_seed ^ 0x85ebca6bu)) * light.radius;
                float phi = 6.2831853f * aa_rand(light_seed ^ 0xc2b2ae35u);
                sample_pos = sample_pos + t1 * (cosf(phi) * r) + t2 * (sinf(phi) * r);
                to_light = sample_pos - rec.point;
                dist2 = fmaxf(dot(to_light, to_light), 1.0e-4f);
                dist = sqrtf(dist2);
                L = to_light / dist;
            }

            // мощность распределяется по сфере; площадь и конус режут вклад
            float atten = light.intensity / (12.5663706f * dist2);
            float3 gobo_color = make_float3(1.0f, 1.0f, 1.0f);

            if (light.type == 2) {
                // площадь светит с одной стороны: нормаль = cross(u, v)
                float3 n = normalize(cross(light.u_axis, light.v_axis));
                float cos_l = dot(n, L);
                if (cos_l <= 0.0f) continue;
                atten *= cos_l;
            } else if (light.type == 3) {
                // конус прожектора вокруг direction
                float3 to_surface = normalize(rec.point - sample_pos);
                float c = dot(to_surface, light.direction);
                float cos_half = cosf(light.angle);
                if (c <= cos_half) continue;
                float blend = fminf(fmaxf(light.blend, 0.0f), 1.0f);
                float cos_soft = cosf(light.angle * (1.0f - blend));
                if (cos_soft > cos_half + 1.0e-4f) {
                    atten *= fminf((c - cos_half) / (cos_soft - cos_half), 1.0f);
                }

                // gobo: проецируем текстуру по конусу (u,v в [0,1])
                if (light.texture_id >= 0 && light.texture_id < num_textures) {
                    float3 axis = light.direction;
                    float3 up = (fabsf(axis.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                                       : make_float3(1.0f, 0.0f, 0.0f);
                    float3 t1 = normalize(cross(axis, up));
                    float3 t2 = cross(axis, t1);
                    float tan_half = tanf(fmaxf(light.angle, 1.0e-3f));
                    float u = 0.5f + 0.5f * dot(to_surface, t1) / tan_half;
                    float v = 0.5f + 0.5f * dot(to_surface, t2) / tan_half;
                    gobo_color = sample_texture(textures[light.texture_id],
                                                make_float2(u, v));
                }
            }

            radiance = light.color * atten * gobo_color;
            target = sample_pos;
            t_max = dist * 0.999f;
        }

        Ray shadow_ray;
        shadow_ray.origin = rec.point + rec.normal * 0.002f;
        shadow_ray.direction = normalize(target - shadow_ray.origin);
        shadow_ray.t_min = 0.001f;
        shadow_ray.t_max = t_max;

        // Стекло для теневого луча прозрачно: пропускаем до трёх граней
        HitRecord shadow_rec;
        Ray sray = shadow_ray;
        bool blocked = false;
        for (int pass = 0; pass < 3; pass++) {
            if (!scene_intersect_opaque(sray,
                                   bvh_nodes, objects, num_objects,
                                   mesh_bvh_nodes, triangles, num_triangles,
                                   hf_bvh_nodes, heightfields, num_heightfields,
                                   se_bvh_nodes, superellipsoids, num_superellipsoids,
                                   lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                                   prism_bvh_nodes, prisms, prism_points, num_prisms,
                                   csg_nodes, csg_root,
                                   planes, num_planes,
                                   shadow_rec)) {
                blocked = false;
                break;
            }
            if (hit_transmission(shadow_rec, materials[shadow_rec.material_id],
                                 textures, num_textures) > 0.5f) {
                sray.origin = shadow_rec.point + sray.direction * 0.002f;
                continue;
            }
            blocked = true;
            break;
        }
        if (mat.shininess < 0.0f) return blocked ? make_float3(1.0f, 0.0f, 0.0f)
                                                 : make_float3(0.0f, 1.0f, 0.0f);
        if (blocked) continue;

        // пряди не перекрывают свет полностью: копим оптическую плотность.
        // Сами пряди не затеняем: сегментов мало, и самозатенение только гасит блик.
        float transmittance = 1.0f;
        if (mat.anisotropy <= 0.0f) {
            float tau = strand_bvh_tau(shadow_ray, strand_bvh_nodes, strands,
                                       sweep_points, num_strands, 6.0f);
            transmittance = expf(-tau);
        }

        result += shade_direct(mat, in, L, radiance, textures, num_textures,
                               shininess) * transmittance;
    }

    // Каустика: плотность фотонов в окрестности точки → радианс.
    // E ≈ Σflux/(π r²), L = albedo/π · E; caustic_scale — ручная калибровка.
    if (photon_map.photons && caustic_scale > 0.0f) {
        float r = photon_map.cell_size * 0.5f;
        float3 flux = photon_gather(photon_map, rec.point, rec.normal, r, 128);
        // DEBUG: печать при caustics > 0 — сколько фотонов собрано
        printf("DEBUG caustic: point=(%f,%f,%f) flux=(%f,%f,%f) r=%f scale=%f\n",
               rec.point.x, rec.point.y, rec.point.z,
               flux.x, flux.y, flux.z, r, caustic_scale);
        result = result + base * flux * (caustic_scale / (9.8696044f * r * r));
    }
    return result;
}


// Пересечение луча с AABB среды: интервал [t0, t1] внутри бокса.
GPU_DEVICE inline bool ray_box(const Ray& ray, float3 c, float3 h,
                               float& t0, float& t1) {
    t0 = -1.0e30f;
    t1 = 1.0e30f;
    const float eps = 1.0e-8f;
    for (int a = 0; a < 3; a++) {
        float o = (a == 0) ? ray.origin.x : (a == 1) ? ray.origin.y : ray.origin.z;
        float d = (a == 0) ? ray.direction.x : (a == 1) ? ray.direction.y : ray.direction.z;
        float cc = (a == 0) ? c.x : (a == 1) ? c.y : c.z;
        float hh = (a == 0) ? h.x : (a == 1) ? h.y : h.z;
        if (fabsf(d) < eps) {
            if (o < cc - hh || o > cc + hh) return false;
            continue;
        }
        float inv = 1.0f / d;
        float tn = (cc - hh - o) * inv;
        float tf = (cc + hh - o) * inv;
        if (tn > tf) { float tmp = tn; tn = tf; tf = tmp; }
        t0 = fmaxf(t0, tn);
        t1 = fminf(t1, tf);
        if (t0 > t1) return false;
    }
    return true;
}


// Цвет неба (фиксированный градиент) — общий для первичного и вторичного луча.
GPU_DEVICE inline float3 sky_color(const float3& dir) {
    float t = 0.5f * (dir.y + 1.0f);
    return {(1.0f - t) * 1.0f + t * 0.5f,
            (1.0f - t) * 1.0f + t * 0.7f,
            (1.0f - t) * 1.0f + t * 1.0f};
}

// Рассеивающая среда вдоль луча: возвращает цвет рассеяния, в tau_out —
// оптическую толщу (для ослабления поверхности). Теневые лучи — на каждый
// источник, поэтому окно/решётка дают лучи.
GPU_DEVICE inline float3 media_march(
    const Ray& ray, float t_surface,
    const Medium* media, int num_media,
    const Light* lights, int num_lights,
    const BVHNode* bvh_nodes, const Object* objects, int num_objects,
    const BVHNode* mesh_bvh_nodes, const Triangle* triangles, int num_triangles,
    const BVHNode* hf_bvh_nodes, const HeightField* heightfields, int num_heightfields,
    const BVHNode* se_bvh_nodes, const Superellipsoid* superellipsoids, int num_superellipsoids,
    const BVHNode* lathe_bvh_nodes, const Lathe* lathes, const LathePoint* lathe_points, int num_lathes,
    const BVHNode* prism_bvh_nodes, const Prism* prisms, const PrismPoint* prism_points, int num_prisms,
    const CSGNode* csg_nodes, int csg_root,
    const Plane* planes, int num_planes,
    unsigned int seed,
    float& tau_out
) {
    float3 in_scatter = {0.0f, 0.0f, 0.0f};
    float tau_total = 0.0f;

    for (int m = 0; m < num_media; m++) {
        const Medium& med = media[m];
        if (med.samples <= 0) continue;

        float m0, m1;
        if (!ray_box(ray, med.center, med.half, m0, m1)) continue;
        m0 = fmaxf(m0, ray.t_min);
        m1 = fminf(m1, t_surface);
        if (m1 <= m0) continue;

        int n = med.samples > 64 ? 64 : med.samples;
        float dt = (m1 - m0) / (float)n;
        float tau = 0.0f;

        for (int k = 0; k < n; k++) {
            // джиттер внутри шага: ступеньки превращаются в шум, который
            // усредняет AA — граница луча перестаёт быть рваной
            float jitter = aa_rand(seed ^ (unsigned int)(m * 9781u)
                                        ^ (unsigned int)(k * 6271u));
            float t = m0 + ((float)k + jitter) * dt;
            float3 p = ray.origin + ray.direction * t;

            float3 radiance = {0.0f, 0.0f, 0.0f};
            for (int i = 0; i < num_lights; i++) {
                const Light& light = lights[i];
                if (!(light.flags & 1)) continue;   // media_interaction off

                float3 ldir; float ldist; float3 lrad;
                if (light.type == 0) {
                    ldir = sun_dir(light, seed ^ (unsigned int)(i * 2654435761u)
                                            ^ (unsigned int)(k * 2246822519u));
                    ldist = 1.0e9f;
                    lrad = light.color * light.intensity;
                } else {
                    float3 to_light = light.position - p;
                    float d2 = fmaxf(dot(to_light, to_light), 1.0e-4f);
                    ldist = sqrtf(d2);
                    ldir = to_light / ldist;
                    lrad = light.color * (light.intensity / (12.5663706f * d2));
                    if (light.type == 3) {
                        float c = dot(-ldir, light.direction);
                        float cos_half = cosf(light.angle);
                        if (c <= cos_half) continue;
                        float blend = fminf(fmaxf(light.blend, 0.0f), 1.0f);
                        float cos_soft = cosf(light.angle * (1.0f - blend));
                        if (cos_soft > cos_half + 1.0e-4f)
                            lrad = lrad * fminf((c - cos_half) / (cos_soft - cos_half), 1.0f);
                    }
                }

                Ray sr;
                sr.origin = p;
                sr.direction = ldir;
                sr.t_min = 0.002f;
                sr.t_max = ldist;
                HitRecord srec;
                if (scene_intersect_opaque(sr,
                           bvh_nodes, objects, num_objects,
                           mesh_bvh_nodes, triangles, num_triangles,
                           hf_bvh_nodes, heightfields, num_heightfields,
                           se_bvh_nodes, superellipsoids, num_superellipsoids,
                           lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                           prism_bvh_nodes, prisms, prism_points, num_prisms,
                           csg_nodes, csg_root,
                           planes, num_planes,
                           srec)) continue;

                radiance = radiance + lrad;
            }

            float3 scatter = med.sigma_s * radiance * 0.07957747f;   // 1/4π
            in_scatter = in_scatter + scatter * (expf(-tau) * dt);
            tau += med.sigma_t * dt;
        }
        tau_total += tau;
    }

    tau_out = tau_total;
    return in_scatter;
}

__global__ __launch_bounds__(256, 2) void render_kernel(
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
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const int ns = (aa_samples < 1) ? 1 : ((aa_samples > SHEEN_AA_MAX) ? SHEEN_AA_MAX : aa_samples);

    float3 accumulated_color = {0.0f, 0.0f, 0.0f};

    for (int s = 0; s < ns; s++) {
        float u, v;
        aa_uv(x, y, width, height, aa_sample_offset_n(s, ns, x, y), u, v);
        unsigned int sample_seed = aa_hash((unsigned int)x * 73856093u
                                           ^ (unsigned int)y * 19349663u
                                           ^ (unsigned int)s * 83492791u);
        
        // Блендеровская камера: focal_length в мм, sensor_width в мм.
        // Если focal_length = 0 — старый режим, где фокус задавала длина forward.
        float tan_h = 1.0f, tan_x = 1.0f, tan_y = 1.0f;
        if (camera.focal_length > 0.0f) {
            tan_h = 0.5f * camera.sensor_width / camera.focal_length;
            if (width >= height) {
                tan_x = tan_h;
                tan_y = tan_h * float(height) / float(width);
            } else {
                tan_y = tan_h;
                tan_x = tan_h * float(width) / float(height);
            }
        }

        Ray ray;
        ray.origin = camera.position;
        ray.direction = normalize(camera.forward
                                + camera.right * (u * tan_x + camera.shift_x * tan_h)
                                + camera.up    * (v * tan_y + camera.shift_y * tan_h));
        ray.t_min = 0.001f;
        ray.t_max = 1e10f;

        // угловой размер пикселя: из него считается footprint для мипов
        const float pix_angle = 2.0f * tan_x / (float)width;
        
        HitRecord rec;
        bool hit = scene_intersect(ray,
                                   bvh_nodes, objects, num_objects,
                                   strand_bvh_nodes, strands, sweep_points, num_strands,
                                   mesh_bvh_nodes, triangles, num_triangles,
                                   hf_bvh_nodes, heightfields, num_heightfields,
                                   se_bvh_nodes, superellipsoids, num_superellipsoids,
                                   lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                                   prism_bvh_nodes, prisms, prism_points, num_prisms,
                                   csg_nodes, csg_root,
                                   planes, num_planes,
                                   rec);
        
        float3 color;
        if (hit) {
            color = compute_lighting(
                rec, materials, textures, num_textures, lights, num_lights,
                bvh_nodes, objects, num_objects,
                strand_bvh_nodes, strands, sweep_points, num_strands,
                mesh_bvh_nodes, triangles, num_triangles,
                hf_bvh_nodes, heightfields, num_heightfields,
                se_bvh_nodes, superellipsoids, num_superellipsoids,
                lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                prism_bvh_nodes, prisms, prism_points, num_prisms,
                csg_nodes, csg_root,
                planes, num_planes,
                -ray.direction,
                rec.t * pix_angle,
                photon_map, caustic_scale,
                sample_seed
            );
        } else {
            color = sky_color(ray.direction);
        }
        // Видимый поток света от прожекторов: аддитивный конус с теневым
        // лучом на каждый шаг — геометрия внутри конуса вырезает тёмные
        // полосы. Яркость обрывается на первой поверхности (rec.t).
        for (int i = 0; i < num_lights; i++) {
            const Light& light = lights[i];
            if (light.type != 3 || light.beam <= 0.0f) continue;

            float t_end = hit ? fminf(rec.t, 30.0f) : 30.0f;
            if (t_end <= 0.0f) continue;

            const int STEPS = 6;
            float dt = t_end / (float)STEPS;
            float cos_half = cosf(light.angle);
            float blend = fminf(fmaxf(light.blend, 0.0f), 1.0f);
            float cos_soft = cosf(light.angle * (1.0f - blend));

            float3 acc = {0.0f, 0.0f, 0.0f};
            for (int k = 0; k < STEPS; k++) {
                float t = ((float)k + 0.5f) * dt;
                float3 p = ray.origin + ray.direction * t;
                float3 v = p - light.position;
                float d2 = fmaxf(dot(v, v), 1.0e-4f);
                float c = dot(v, light.direction) * rsqrtf(d2);
                if (c <= cos_half) continue;
                float w = 1.0f;
                if (cos_soft > cos_half + 1.0e-4f)
                    w = fminf((c - cos_half) / (cos_soft - cos_half), 1.0f);
                // 1/(1+kd²) вместо 1/d²: у самой лампы нет бесконечности,
                // масштаб подобран так, чтобы beam=1 давал заметный, но не
                // выжженный конус при мощности ~1000 Вт
                float dens = light.beam * light.intensity * 1.0e-3f
                           / (1.0f + 0.25f * d2);

                // Тень внутри объёма: геометрия между точкой шага и лампой
                // гасит шаг. Сдвиг по диску radius даёт ту же мягкость, что
                // у теней на поверхности. Ветка живёт только внутри
                // beam > 0, поэтому сцена без луча по-прежнему платит ноль.
                float3 target = light.position;
                if (light.radius > 0.0f) {
                    float3 axis = normalize(-v);   // от лампы к точке шага
                    float3 up = (fabsf(axis.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                                       : make_float3(1.0f, 0.0f, 0.0f);
                    float3 t1 = normalize(cross(axis, up));
                    float3 t2 = cross(axis, t1);
                    float rr = sqrtf(aa_rand(sample_seed ^ (unsigned int)(k * 2654435761u)
                                                  ^ 0x85ebca6bu)) * light.radius;
                    float ph = 6.2831853f * aa_rand(sample_seed ^ (unsigned int)(k * 2246822519u)
                                                         ^ 0xc2b2ae35u);
                    target = light.position + t1 * (cosf(ph) * rr) + t2 * (sinf(ph) * rr);
                }

                float3 to_l = target - p;
                float ldist = sqrtf(fmaxf(dot(to_l, to_l), 1.0e-8f));
                float3 ldir = to_l / ldist;
                float remain = ldist * 0.999f;

                Ray sray;
                sray.origin = p;
                sray.direction = ldir;
                sray.t_min = 0.002f;
                sray.t_max = remain;

                // Стекло прозрачно для теневого луча: как и на поверхности,
                // пропускаем до трёх граней, каждый раз уменьшая остаток.
                HitRecord srec;
                bool blocked = false;
                for (int pass = 0; pass < 3; pass++) {
                    if (!scene_intersect_opaque(sray,
                               bvh_nodes, objects, num_objects,
                               mesh_bvh_nodes, triangles, num_triangles,
                               hf_bvh_nodes, heightfields, num_heightfields,
                               se_bvh_nodes, superellipsoids, num_superellipsoids,
                               lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                               prism_bvh_nodes, prisms, prism_points, num_prisms,
                               csg_nodes, csg_root,
                               planes, num_planes,
                               srec)) {
                        blocked = false;
                        break;
                    }
                    if (materials[srec.material_id].transmission > 0.5f) {
                        remain -= srec.t + 0.002f;
                        if (remain <= 0.0f) { blocked = false; break; }
                        sray.origin = srec.point + ldir * 0.002f;
                        sray.t_max = remain;
                        continue;
                    }
                    blocked = true;
                    break;
                }
                if (blocked) continue;

                acc = acc + light.color * (dens * w * dt);
            }
            color = color + acc;
        }

        if (num_media > 0) {
            float tau = 0.0f;
            float3 inscatter = media_march(ray, hit ? rec.t : 1.0e9f,
                                           media, num_media, lights, num_lights,
                                           bvh_nodes, objects, num_objects,
                                           mesh_bvh_nodes, triangles, num_triangles,
                                           hf_bvh_nodes, heightfields, num_heightfields,
                                           se_bvh_nodes, superellipsoids, num_superellipsoids,
                                           lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                                           prism_bvh_nodes, prisms, prism_points, num_prisms,
                                           csg_nodes, csg_root,
                                           planes, num_planes,
                                           sample_seed,
                                           tau);
            color = color * expf(-tau) + inscatter;
        }

        // Зеркальное отражение глубины 1: вторичный луч, включая среду на нём
        if (hit) {
            const Material& pm = materials[rec.material_id];
            if (pm.reflect > 0.0f) {
                float3 rd = ray.direction - rec.normal * (2.0f * dot(ray.direction, rec.normal));
                Ray rr;
                rr.origin = rec.point + rec.normal * 0.002f;
                rr.direction = normalize(rd);
                rr.t_min = 0.001f;
                rr.t_max = 1.0e10f;

                HitRecord rrec;
                bool rhit = scene_intersect(rr,
                                   bvh_nodes, objects, num_objects,
                                   strand_bvh_nodes, strands, sweep_points, num_strands,
                                   mesh_bvh_nodes, triangles, num_triangles,
                                   hf_bvh_nodes, heightfields, num_heightfields,
                                   se_bvh_nodes, superellipsoids, num_superellipsoids,
                                   lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                                   prism_bvh_nodes, prisms, prism_points, num_prisms,
                                   csg_nodes, csg_root,
                                   planes, num_planes,
                                   rrec);

                float3 rcol = rhit
                    ? compute_lighting(rrec, materials, textures, num_textures,
                                       lights, num_lights,
                                       bvh_nodes, objects, num_objects,
                                       strand_bvh_nodes, strands, sweep_points, num_strands,
                                       mesh_bvh_nodes, triangles, num_triangles,
                                       hf_bvh_nodes, heightfields, num_heightfields,
                                       se_bvh_nodes, superellipsoids, num_superellipsoids,
                                       lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                                       prism_bvh_nodes, prisms, prism_points, num_prisms,
                                       csg_nodes, csg_root,
                                       planes, num_planes,
                                       -rr.direction,
                                       (rhit ? rrec.t : 0.0f) * pix_angle,
                                       photon_map, caustic_scale,
                                       sample_seed)
                    : sky_color(rr.direction);

                if (num_media > 0) {
                    float rtau = 0.0f;
                    float3 rins = media_march(rr, rhit ? rrec.t : 1.0e9f,
                                              media, num_media, lights, num_lights,
                                              bvh_nodes, objects, num_objects,
                                              mesh_bvh_nodes, triangles, num_triangles,
                                              hf_bvh_nodes, heightfields, num_heightfields,
                                              se_bvh_nodes, superellipsoids, num_superellipsoids,
                                              lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                                              prism_bvh_nodes, prisms, prism_points, num_prisms,
                                              csg_nodes, csg_root,
                                              planes, num_planes,
                                              sample_seed,
                                              rtau);
                    rcol = rcol * expf(-rtau) + rins;
                }

                float w = fminf(pm.reflect, 1.0f);
                color = color * (1.0f - w) + rcol * w;
            }
        }

        // Преломление: проход через стекло (до трёх граней подряд).
        if (hit) {
            const Material& gm = materials[rec.material_id];
            // Пропускание — скаляр материала × маска в точке (жила слюды).
            float gtr = hit_transmission(rec, gm, textures, num_textures);
            if (gtr > 0.0f) {
                float3 through = {0.0f, 0.0f, 0.0f};
                float w = 1.0f;
                Ray gr = ray;
                HitRecord gh = rec;
                // Нормаль в HitRecord всегда развёрнута к лучу, поэтому
                // вход/выход различаем по счётчику граней, а не по знаку.
                bool entering = true;

                for (int b = 0; b < 3; b++) {
                    const Material& sm = materials[gh.material_id];
                    float3 n = gh.normal;          // уже против луча
                    float3 d = gr.direction;
                    float ior = fmaxf(hit_ior(gh, sm, textures, num_textures), 1.0f);
                    float eta = entering ? (1.0f / ior) : ior;
                    float cosi = fmaxf(-dot(d, n), 1.0e-4f);
                    float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
                    float3 td = (k < 0.0f)
                        ? normalize(d + 2.0f * cosi * n)                    // полное внутреннее отражение
                        : normalize(eta * d + (eta * cosi - sqrtf(k)) * n);  // Снелл
                    w *= hit_transmission(gh, sm, textures, num_textures);

                    gr.origin = gh.point + td * 0.002f;
                    gr.direction = td;
                    gr.t_min = 0.001f;
                    gr.t_max = 1.0e10f;

                    HitRecord nh;
                    if (!scene_intersect(gr,
                               bvh_nodes, objects, num_objects,
                               strand_bvh_nodes, strands, sweep_points, num_strands,
                               mesh_bvh_nodes, triangles, num_triangles,
                               hf_bvh_nodes, heightfields, num_heightfields,
                               se_bvh_nodes, superellipsoids, num_superellipsoids,
                               lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                               prism_bvh_nodes, prisms, prism_points, num_prisms,
                               csg_nodes, csg_root,
                               planes, num_planes,
                               nh)) {
                        through = through + w * sky_color(td);
                        break;
                    }
                    if (hit_transmission(nh, materials[nh.material_id], textures, num_textures) > 0.0f) {
                        entering = !entering;    // следующая грань — выход
                        gh = nh;
                        continue;
                    }
                    through = through + w * compute_lighting(
                        nh, materials, textures, num_textures, lights, num_lights,
                        bvh_nodes, objects, num_objects,
                        strand_bvh_nodes, strands, sweep_points, num_strands,
                        mesh_bvh_nodes, triangles, num_triangles,
                        hf_bvh_nodes, heightfields, num_heightfields,
                        se_bvh_nodes, superellipsoids, num_superellipsoids,
                        lathe_bvh_nodes, lathes, lathe_points, num_lathes,
                        prism_bvh_nodes, prisms, prism_points, num_prisms,
                        csg_nodes, csg_root,
                        planes, num_planes,
                        -td,
                        nh.t * pix_angle,
                        photon_map, caustic_scale,
                        sample_seed);
                    break;
                }

                color = color * (1.0f - fminf(gtr, 1.0f)) + through;
            }
        }

        accumulated_color += color;
    }
    
    float3 final_color = accumulated_color * (1.0f / float(ns));
    output[y * width + x] = {final_color.x, final_color.y, final_color.z, 1.0f};
}
// ---------------------------------------------------------
// Проход фотонов: шаг 1. Эмиссия из локальных источников, трассировка
// через стекло и зеркала, накопление диффузных попаданий в буфер.
// Карта и выборка при затенении — следующим шагом; кадр пока не меняется.
// ---------------------------------------------------------
__global__ void photon_kernel(PhotonScene s, int per_light, Photon* out,
                              int max_photons, unsigned int* count,
                              unsigned int seed) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = s.num_lights * per_light;
    if (idx >= total || per_light <= 0) return;

    const Light& light = s.lights[idx / per_light];
    if (light.type == 0) return;   // SUN пока не эмитим (нет площади старта)

    unsigned int sd = aa_hash(seed + (unsigned int)idx * 2654435761u);
    float u1 = aa_rand(sd);
    float u2 = aa_rand(sd ^ 0x9e3779b9u);
    float u3 = aa_rand(sd ^ 0x85ebca6bu);

    float3 pos = light.position;
    float3 dir;

    if (light.type == 3) {
        // конус прожектора: cos тета равномерен в [cos(angle), 1]
        float cos_half = cosf(light.angle);
        float ct = cos_half + (1.0f - cos_half) * u1;
        float st = sqrtf(fmaxf(1.0f - ct * ct, 0.0f));
        float phi = 6.2831853f * u2;
        float3 ax = light.direction;
        float3 up = (fabsf(ax.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                         : make_float3(1.0f, 0.0f, 0.0f);
        float3 t1 = normalize(cross(ax, up));
        float3 t2 = cross(ax, t1);
        dir = normalize(ax * ct + t1 * (cosf(phi) * st) + t2 * (sinf(phi) * st));
    } else if (light.type == 2) {
        // прямоугольник: точка на площади, косинусная полусфера
        pos = light.position + light.u_axis * (u1 * 2.0f - 1.0f)
                              + light.v_axis * (u2 * 2.0f - 1.0f);
        float3 n = normalize(cross(light.u_axis, light.v_axis));
        float3 up = (fabsf(n.y) < 0.9f) ? make_float3(0.0f, 1.0f, 0.0f)
                                        : make_float3(1.0f, 0.0f, 0.0f);
        float3 t1 = normalize(cross(n, up));
        float3 t2 = cross(n, t1);
        float cz = sqrtf(fmaxf(1.0f - u3, 0.0f));
        float r = sqrtf(u3);
        float phi = 6.2831853f * aa_rand(sd ^ 0xc2b2ae35u);
        dir = normalize(n * cz + t1 * (cosf(phi) * r) + t2 * (sinf(phi) * r));
    } else {
        // точка: равномерно по сфере
        float cz = 2.0f * u1 - 1.0f;
        float st = sqrtf(fmaxf(1.0f - cz * cz, 0.0f));
        float phi = 6.2831853f * u2;
        dir = make_float3(st * cosf(phi), cz, st * sinf(phi));
    }

    float3 flux = light.color * (light.intensity / (float)per_light);

    Ray ray;
    ray.origin = pos;
    ray.direction = dir;
    ray.t_min = 0.001f;
    ray.t_max = 1.0e10f;

    for (int bounce = 0; bounce < 4; bounce++) {
        HitRecord hit;
        if (!scene_intersect(ray,
                   s.bvh_nodes, s.objects, s.num_objects,
                   s.strand_bvh_nodes, s.strands, s.sweep_points, s.num_strands,
                   s.mesh_bvh_nodes, s.triangles, s.num_triangles,
                   s.hf_bvh_nodes, s.heightfields, s.num_heightfields,
                   s.se_bvh_nodes, s.superellipsoids, s.num_superellipsoids,
                   s.lathe_bvh_nodes, s.lathes, s.lathe_points, s.num_lathes,
                   s.prism_bvh_nodes, s.prisms, s.prism_points, s.num_prisms,
                   s.csg_nodes, s.csg_root,
                   s.planes, s.num_planes,
                   hit)) break;

        const Material& m = s.materials[hit.material_id];
        if (m.transmission > 0.5f) {
            float3 n = hit.normal;              // уже против луча
            float ior = fmaxf(m.ior, 1.0f);
            float eta = 1.0f / ior;
            float cosi = fmaxf(-dot(ray.direction, n), 1.0e-4f);
            float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
            float3 td = (k < 0.0f)
                ? normalize(ray.direction + 2.0f * cosi * n)
                : normalize(eta * ray.direction + (eta * cosi - sqrtf(k)) * n);
            flux = flux * m.transmission;
            ray.origin = hit.point + td * 0.002f;
            ray.direction = td;
            continue;
        }
        if (m.reflect > 0.5f) {
            float3 rd = ray.direction - hit.normal * (2.0f * dot(ray.direction, hit.normal));
            ray.origin = hit.point + hit.normal * 0.002f;
            ray.direction = normalize(rd);
            continue;
        }

        unsigned int slot = atomicAdd(count, 1u);
        if (slot < (unsigned int)max_photons) {
            Photon p;
            p.position = hit.point;
            p.direction = -ray.direction;
            p.normal = hit.normal;
            p.flux = flux;
            out[slot] = p;
        }
        break;
    }
}

PhotonStats launch_photons(const PhotonScene& s, int per_light,
                           Photon* d_photons, int max_photons,
                           unsigned int* d_count) {
    PhotonStats st{0, 0, 0.0f};
    int total = s.num_lights * per_light;
    if (total <= 0 || max_photons <= 0 || !d_photons || !d_count) return st;
    st.emitted = total;

    cudaMemset(d_count, 0, sizeof(unsigned int));
    auto t0 = std::chrono::high_resolution_clock::now();
    int block = 128;
    int grid = (total + block - 1) / block;
    photon_kernel<<<grid, block>>>(s, per_light, d_photons, max_photons,
                                   d_count, 0x5eed1234u);
    cudaError_t err = cudaDeviceSynchronize();
    auto t1 = std::chrono::high_resolution_clock::now();
    if (err != cudaSuccess) {
        std::printf("photons: CUDA error: %s\n", cudaGetErrorString(err));
        return st;
    }
    unsigned int c = 0;
    cudaMemcpy(&c, d_count, sizeof(unsigned int), cudaMemcpyDeviceToHost);
    st.stored = (int)c;
    st.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return st;
}

// ---------------------------------------------------------
// Шаг 2: карта фотонов. Хеш-сетка, ячейка cell; границы ячеек считаются
// префиксной суммой на хосте, раскладка — на устройстве.
// ---------------------------------------------------------
__global__ void photon_count_kernel(const Photon* p, int n, int* counts,
                                    int num_cells, float cell) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    int ix = (int)floorf(p[i].position.x / cell);
    int iy = (int)floorf(p[i].position.y / cell);
    int iz = (int)floorf(p[i].position.z / cell);
    atomicAdd(&counts[photon_cell_hash(ix, iy, iz, num_cells)], 1);
}

__global__ void photon_scatter_kernel(const Photon* p, int n, Photon* out,
                                      int* cursor, int num_cells, float cell) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    int ix = (int)floorf(p[i].position.x / cell);
    int iy = (int)floorf(p[i].position.y / cell);
    int iz = (int)floorf(p[i].position.z / cell);
    int c = photon_cell_hash(ix, iy, iz, num_cells);
    int slot = atomicAdd(&cursor[c], 1);
    out[slot] = p[i];
}

__global__ void photon_probe_kernel(PhotonMap m, const Photon* src, int n,
                                    int stride, float radius, int* counts) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int idx = i * stride;
    if (idx >= n) return;
    const Photon& ph = src[idx];
    int taken = 0;
    photon_gather(m, ph.position, ph.normal, radius, 4096, &taken);
    counts[i] = taken;
}

PhotonMapStats build_photon_map(const Photon* d_photons, int n, float cell,
                                PhotonMapBuffers& out) {
    PhotonMapStats st{0, 0, 0.0, 0, 0, 0};
    if (!d_photons || n <= 0 || cell <= 0.0f) return st;

    int num_cells = 1 << 18;   // 262 144 ячейки: 1 МБ на массив
    out.num_cells = num_cells;
    out.cell_size = cell;
    out.num_photons = n;

    auto t0 = std::chrono::high_resolution_clock::now();
    cudaMalloc(&out.photons, (size_t)n * sizeof(Photon));
    cudaMalloc(&out.cell_count, (size_t)num_cells * sizeof(int));
    cudaMalloc(&out.cell_start, (size_t)(num_cells + 1) * sizeof(int));
    cudaMemset(out.cell_count, 0, (size_t)num_cells * sizeof(int));

    int block = 128;
    int grid = (n + block - 1) / block;
    photon_count_kernel<<<grid, block>>>(d_photons, n, out.cell_count, num_cells, cell);
    if (cudaDeviceSynchronize() != cudaSuccess) return st;

    // префиксная сумма и статистика — на хосте: 262k int, дёшево
    std::vector<int> host_counts(num_cells), host_start(num_cells + 1);
    cudaMemcpy(host_counts.data(), out.cell_count,
               (size_t)num_cells * sizeof(int), cudaMemcpyDeviceToHost);
    int acc = 0, used = 0, maxc = 0;
    for (int i = 0; i < num_cells; i++) {
        host_start[i] = acc;
        acc += host_counts[i];
        if (host_counts[i] > 0) used++;
        if (host_counts[i] > maxc) maxc = host_counts[i];
    }
    host_start[num_cells] = acc;
    cudaMemcpy(out.cell_start, host_start.data(),
               (size_t)(num_cells + 1) * sizeof(int), cudaMemcpyHostToDevice);

    int* cursor = nullptr;
    cudaMalloc(&cursor, (size_t)num_cells * sizeof(int));
    cudaMemcpy(cursor, host_start.data(), (size_t)num_cells * sizeof(int),
               cudaMemcpyHostToDevice);
    photon_scatter_kernel<<<grid, block>>>(d_photons, n, out.photons, cursor,
                                           num_cells, cell);
    if (cudaDeviceSynchronize() != cudaSuccess) {
        cudaFree(cursor);
        return st;
    }
    cudaFree(cursor);

    auto t1 = std::chrono::high_resolution_clock::now();
    st.used_cells = used;
    st.max_cell = maxc;
    st.build_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return st;
}

void free_photon_map(PhotonMapBuffers& m) {
    cudaFree(m.photons);
    cudaFree(m.cell_start);
    cudaFree(m.cell_count);
    m = PhotonMapBuffers();
}

PhotonMap photon_map_view(const PhotonMapBuffers& m) {
    PhotonMap v;
    v.photons = m.photons;
    v.cell_start = m.cell_start;
    v.cell_count = m.cell_count;
    v.num_cells = m.num_cells;
    v.cell_size = m.cell_size;
    return v;
}

void probe_photon_map(const PhotonMapBuffers& m, int stride,
                      int& min_c, int& avg_c, int& max_c) {
    min_c = avg_c = max_c = 0;
    if (!m.photons || m.num_photons <= 0 || stride <= 0) return;
    int probes = (m.num_photons + stride - 1) / stride;
    int* d_counts = nullptr;
    cudaMalloc(&d_counts, (size_t)probes * sizeof(int));
    int block = 128;
    int grid = (probes + block - 1) / block;
    photon_probe_kernel<<<grid, block>>>(photon_map_view(m), m.photons,
                                         m.num_photons, stride,
                                         m.cell_size * 0.5f, d_counts);
    if (cudaDeviceSynchronize() != cudaSuccess) { cudaFree(d_counts); return; }
    std::vector<int> host(probes);
    cudaMemcpy(host.data(), d_counts, (size_t)probes * sizeof(int),
               cudaMemcpyDeviceToHost);
    cudaFree(d_counts);

    long long sum = 0;
    min_c = 1 << 30;
    for (int c : host) {
        if (c < min_c) min_c = c;
        if (c > max_c) max_c = c;
        sum += c;
    }
    avg_c = (int)(sum / (long long)probes);
}

__global__ void photon_probe_brute_kernel(const Photon* probes, const Photon* all,
                                          int n, int stride, float radius, int* counts) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int idx = i * stride;
    if (idx >= n) return;
    const Photon& p0 = probes[idx];
    float r2 = radius * radius;
    int taken = 0;
    for (int j = 0; j < n && taken < 4096; j++) {
        float3 d = all[j].position - p0.position;
        if (dot(d, d) > r2) continue;
        if (dot(all[j].normal, p0.normal) < 0.1f) continue;
        taken++;
    }
    counts[i] = taken;
}

void probe_bruteforce(const Photon* d_probes, const Photon* d_all, int n, int stride,
                      int& min_c, int& avg_c, int& max_c) {
    min_c = avg_c = max_c = 0;
    if (!d_probes || !d_all || n <= 0 || stride <= 0) return;
    int probes = (n + stride - 1) / stride;
    int* d_counts = nullptr;
    cudaMalloc(&d_counts, (size_t)probes * sizeof(int));
    int block = 128;
    int grid = (probes + block - 1) / block;
    photon_probe_brute_kernel<<<grid, block>>>(d_probes, d_all, n, stride,
                                               0.125f, d_counts);
    if (cudaDeviceSynchronize() != cudaSuccess) { cudaFree(d_counts); return; }
    std::vector<int> host(probes);
    cudaMemcpy(host.data(), d_counts, (size_t)probes * sizeof(int),
               cudaMemcpyDeviceToHost);
    cudaFree(d_counts);
    long long sum = 0;
    min_c = 1 << 30;
    for (int c : host) {
        if (c < min_c) min_c = c;
        if (c > max_c) max_c = c;
        sum += c;
    }
    avg_c = (int)(sum / (long long)probes);
}
