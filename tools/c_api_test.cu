// Консольная проверка C-API (бинарный контракт).
// 1. tiny-сцена жёстко зашита (сфера + плоскость + свет) — без парсера.
// 2. демо-сцена через файловый загрузчик -> SheenScene -> C-API; результат
//    сравнивается MD5 с выходом scene_render: обязаны совпасть бит-в-бит.

#include "sheen_c_api.h"
#include "scene.h"
#include "api/scene_io.h"
#include <cstdio>
#include <cstring>

static void fill_sheen_scene(const Scene& s, SheenScene& out) {
    std::memset(&out, 0, sizeof(out));
    out.camera = s.camera;
    out.objects = s.objects.data();
    out.num_objects = (int)s.objects.size();
    out.sweep_points = s.sweep_points.data();
    out.num_sweep_points = (int)s.sweep_points.size();
    out.sweep_strands = s.sweep_strands.data();
    out.num_sweep_strands = (int)s.sweep_strands.size();
    out.triangles = s.triangles.data();
    out.num_triangles = (int)s.triangles.size();
    out.planes = s.planes.data();
    out.num_planes = (int)s.planes.size();
    out.materials = s.materials.data();
    out.num_materials = (int)s.materials.size();
    out.lights = s.lights.data();
    out.num_lights = (int)s.lights.size();
    out.csg_nodes = s.csg_nodes.data();
    out.csg_root = s.csg_root;
    out.num_csg_nodes = (int)s.csg_nodes.size();
    out.heightfields = s.heightfields.data();
    out.num_heightfields = (int)s.heightfields.size();
    out.superellipsoids = s.superellipsoids.data();
    out.num_superellipsoids = (int)s.superellipsoids.size();
    out.lathe_points = s.lathe_points.data();
    out.num_lathe_points = (int)s.lathe_points.size();
    out.lathes = s.lathes.data();
    out.num_lathes = (int)s.lathes.size();
    out.prism_points = s.prism_points.data();
    out.num_prism_points = (int)s.prism_points.size();
    out.prisms = s.prisms.data();
    out.num_prisms = (int)s.prisms.size();
}

int main() {
    // --- тёплый прогон: маленькая сцена вручную ---
    Scene tiny;
    tiny.camera.position = {0.0f, 1.0f, 3.0f};
    tiny.camera.forward  = {0.0f, 0.0f, -1.0f};
    tiny.camera.right    = {1.0f, 0.0f, 0.0f};
    tiny.camera.up       = {0.0f, 1.0f, 0.0f};
    tiny.camera.focal_length = 0.0f;
    tiny.add_material({0.8f, 0.2f, 0.2f}, 0.3f, 32.0f, 0.15f);
    tiny.add_material({0.6f, 0.6f, 0.6f}, 0.1f, 8.0f, 0.2f);
    tiny.add_sphere({0.0f, 1.0f, 0.0f}, 0.5f, 0);
    tiny.add_plane({0.0f, 1.0f, 0.0f}, 0.0f, 1);
    tiny.add_directional_light({-0.6f, -0.8f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0.9f);
    tiny.build_bvh();

    SheenScene ts;
    fill_sheen_scene(tiny, ts);

    if (sheen_init(0, 61) != 0) {
        std::printf("init failed: %s\n", sheen_get_last_error());
        return 1;
    }
    if (sheen_render_scene(&ts, 320, 240, 4, "/tmp/c_api_simple.png") != 0) {
        std::printf("tiny render failed: %s\n", sheen_get_last_error());
        return 1;
    }
    std::printf("tiny: /tmp/c_api_simple.png ok\n");

    // --- демо через загрузчик сцены: сверка с scene_render ---
    Scene demo;
    SceneSettings st;
    if (!load_scene_file("scenes/demo.sheen", demo, st)) {
        std::printf("demo load failed\n");
        return 1;
    }
    SheenScene ds;
    fill_sheen_scene(demo, ds);
    // текстуры в демо нет — контракт допускает nullptr
    if (sheen_render_scene(&ds, 800, 600, 4, "/tmp/c_api_demo.png") != 0) {
        std::printf("demo render failed: %s\n", sheen_get_last_error());
        return 1;
    }
    std::printf("demo: /tmp/c_api_demo.png ok\n");

    sheen_cleanup();
    return 0;
}