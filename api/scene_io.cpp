#include "scene_io.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <sstream>
#include <iostream>
#include <cstring>
#include <random>
#include <map>
#include <algorithm>

namespace {

struct Params {
    std::map<std::string, std::string> kv;
    bool has(const std::string& k) const { return kv.count(k) > 0; }
    float f(const std::string& k, float d) const {
        auto it = kv.find(k);
        return it == kv.end() ? d : std::stof(it->second);
    }
    int i(const std::string& k, int d) const {
        auto it = kv.find(k);
        return it == kv.end() ? d : std::stoi(it->second);
    }
    unsigned u(const std::string& k, unsigned d) const {
        auto it = kv.find(k);
        return it == kv.end() ? d : (unsigned)std::stoul(it->second);
    }
    float3 v3(const std::string& k, float3 d) const {
        auto it = kv.find(k);
        if (it == kv.end()) return d;
        std::string v = it->second;
        std::replace(v.begin(), v.end(), ',', ' ');
        std::istringstream ss(v);
        float x, y, z;
        if (!(ss >> x >> y >> z)) return d;
        return {x, y, z};
    }
    static Params parse(std::istringstream& in) {
        Params p;
        std::string tok;
        while (in >> tok) {
            auto eq = tok.find('=');
            if (eq == std::string::npos) continue;
            p.kv[tok.substr(0, eq)] = tok.substr(eq + 1);
        }
        return p;
    }
};

} // namespace
#include <cmath>

// Стек префиксного парсера CSG-дерева (см. csg_begin/csg_leaf/csg_end)
static std::vector<int> csg_stack;

// ---------------------------------------------------------
// Bezier_spline для prism/lathe.
// Принцип: примитив остаётся линейным (конусные сегменты/полилиния).
// Кубические кривые Безье тесселируются на host-стороне в плотный
// линейный профиль ДО попадания точек в lathe_points/prism_points.
// CUDA-ядро и бинарный контракт не меняются.
// ---------------------------------------------------------

// 2D-точка профиля (для prism — (x,z), для lathe — (y,radius)).
struct Bz2 { float a; float b; };

// Точка кубической кривой Безье: P0,P3 — концы, P1,P2 — направляющие.
static Bz2 bezier_point(const Bz2& p0, const Bz2& p1, const Bz2& p2,
                        const Bz2& p3, float t) {
    float u = 1.0f - t;
    float w0 = u*u*u;
    float w1 = 3.0f*u*u*t;
    float w2 = 3.0f*u*t*t;
    float w3 = t*t*t;
    return {w0*p0.a + w1*p1.a + w2*p2.a + w3*p3.a,
            w0*p0.b + w1*p1.b + w2*p2.b + w3*p3.b};
}

// Тесселяция прогона кубических Безье: контрольные точки идут группами
// по 4 (одна группа = один сегмент кубика от точки 0 до точки 3; точка 3
// одной группы является точкой 0 следующей — кривая непрерывна).
// Результат дописывается в out как плотная полилиния (subdiv отрезков
// на каждый сегмент кубика). Возвращает число точек начальной кривой
// (не добавляет явное замыкание; замыкание — на совести вызывающего).
static void tessellate_bezier_run(const std::vector<Bz2>& ctrl, int subdiv,
                                  std::vector<Bz2>& out) {
    if (ctrl.size() < 4) return;
    if (subdiv < 1) subdiv = 1;
    int nseg = (int)ctrl.size() / 4;
    out.push_back(ctrl[0]);
    for (int s = 0; s < nseg; s++) {
        const Bz2& P0 = ctrl[4*s + 0];
        const Bz2& P1 = ctrl[4*s + 1];
        const Bz2& P2 = ctrl[4*s + 2];
        const Bz2& P3 = ctrl[4*s + 3];
        for (int k = 1; k <= subdiv; k++) {
            out.push_back(bezier_point(P0, P1, P2, P3,
                                       (float)k / (float)subdiv));
        }
    }
}

static void add_hair(Scene& scene, const Params& p) {
    float3 c   = p.v3("center", {0.0f, 1.6f, 0.0f});
    float head = p.f("head", 0.55f);
    int count  = p.i("count", 500);
    int segs   = p.i("segs", 10);
    unsigned seed = p.u("seed", 1u);
    int mat    = p.i("mat", 0);
    float rad  = p.f("radius", 0.009f);
    float taper= p.f("taper", 0.55f);
    float len  = p.f("length", 0.9f);
    float wob  = p.f("wobble", 0.012f);
    float curl = p.f("curl", 0.9f);
    float phi0 = p.f("phi0", 0.15f);
    float phi1 = p.f("phi1", 1.20f);
    float spread = p.f("spread", 1.02f);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        float theta = 6.2831853f * U(rng);
        float ph    = phi0 + (phi1 - phi0) * U(rng);
        float r0    = rad * (0.75f + 0.5f * U(rng));
        float fall  = len * head * (0.6f + 0.8f * U(rng));

        std::vector<SweepPoint> pts;
        float3 pos = c;
        for (int k = 0; k < segs; k++) {
            float t = (float)k / (float)(segs - 1);
            float phi = ph + t * curl;
            float3 q;
            if (phi < 1.75f) {
                float rr = head * (spread + 0.10f * t);
                q = {c.x + rr * sinf(phi) * cosf(theta),
                     c.y + rr * cosf(phi),
                     c.z + rr * sinf(phi) * sinf(theta)};
            } else {
                q = pos;
                q.y -= fall * t;
                q.x += sinf(t * 6.0f + i) * wob;
                q.z += cosf(t * 5.0f + i * 0.7f) * wob;
            }
            pos = q;

            SweepPoint sp;
            sp.position = q;
            sp.radius = r0 * (1.0f - taper * t);
            pts.push_back(sp);
        }
        scene.add_sphere_sweep_smooth(pts, mat, 2);
    }
}


static void add_hair_mesh(Scene& scene, const Params& p) {
    int mat    = p.i("mat", 1);
    int count  = p.i("count", 500);
    int segs   = p.i("segs", 10);
    unsigned seed = p.u("seed", 7u);
    float rad  = p.f("radius", 0.004f);
    float taper= p.f("taper", 0.6f);
    float len  = p.f("length", 0.25f);
    float wob  = p.f("wobble", 0.01f);
    float y0   = p.f("y0", 0.0f);
    float y1   = p.f("y1", 1e9f);
    float lift = p.f("lift", 0.5f);

    std::vector<float3> roots, norms;
    for (const auto& t : scene.triangles) {
        float3 n = normalize(cross(t.v1 - t.v0, t.v2 - t.v0));
        const float3 vs[3] = {t.v0, t.v1, t.v2};
        for (int k = 0; k < 3; k++) {
            if (vs[k].y >= y0 && vs[k].y <= y1) {
                roots.push_back(vs[k]);
                norms.push_back(n);
            }
        }
    }
    if (roots.empty()) {
        std::cerr << "hair_mesh: в диапазоне y нет вершин\n";
        return;
    }

    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> pick(0, roots.size() - 1);

    for (int i = 0; i < count; i++) {
        size_t idx = pick(rng);
        float3 root = roots[idx] + norms[idx] * 0.002f;
        float3 dir = normalize(norms[idx] * lift + float3{0.0f, -1.0f, 0.0f});
        float r0 = rad * (0.7f + 0.6f * (float)(rng() % 1000) / 1000.0f);

        std::vector<SweepPoint> pts;
        float3 pos = root;
        float step = len / (float)(segs - 1);
        for (int k = 0; k < segs; k++) {
            float t = (float)k / (float)(segs - 1);
            SweepPoint sp;
            sp.position = pos;
            sp.radius = r0 * (1.0f - taper * t);
            pts.push_back(sp);

            float3 wob_v = {sinf(t * 9.0f + i * 1.3f) * wob,
                            0.0f,
                            cosf(t * 7.0f + i * 0.9f) * wob};
            dir = normalize(dir + float3{0.0f, -0.6f * step, 0.0f} + wob_v);
            pos = pos + dir * step;
        }
        scene.add_sphere_sweep_smooth(pts, mat, 1);
    }
}

static bool parse_line(std::istringstream& in, Scene& scene, SceneSettings& st) {
    std::string cmd;
    in >> cmd;
    if (cmd.empty() || cmd[0] == '#') return true;

    if (cmd == "resolution") {
        std::streampos save = in.tellg();
        std::string first;
        in >> first;
        in.seekg(save);
        if (first.find('=') != std::string::npos) {
            Params p = Params::parse(in);
            st.width  = p.i("width", st.width);
            st.height = p.i("height", st.height);
            st.aa     = p.i("aa", st.aa);
            if (p.has("out")) st.output = p.kv.at("out");
            return true;
        }
        in >> st.width >> st.height >> st.aa >> st.output;
        return true;
    }
    if (cmd == "camera") {
        std::streampos save = in.tellg();
        std::string first;
        in >> first;
        in.seekg(save);

        if (first.find('=') != std::string::npos) {
            Params p = Params::parse(in);
            scene.camera.position = p.v3("loc", {0.0f, 0.0f, 3.0f});
            scene.camera.right    = p.v3("right", {1.0f, 0.0f, 0.0f});
            scene.camera.up       = p.v3("up",    {0.0f, 1.0f, 0.0f});
            scene.camera.forward  = p.v3("fwd",   {0.0f, 0.0f, -1.0f});
            scene.camera.focal_length = p.f("lens", 50.0f);
            scene.camera.sensor_width = p.f("sensor", 36.0f);
            scene.camera.shift_x = p.f("shift_x", 0.0f);
            scene.camera.shift_y = p.f("shift_y", 0.0f);
            return true;
        }

        float px, py, pz, fx, fy, fz;
        in >> px >> py >> pz >> fx >> fy >> fz;
        scene.camera.position = {px, py, pz};
        scene.camera.forward  = {fx, fy, fz};
        scene.camera.right    = {1.0f, 0.0f, 0.0f};
        scene.camera.up       = {0.0f, 1.0f, 0.0f};
        scene.camera.focal_length = 0.0f;
        return true;
    }
    if (cmd == "material") {
        std::streampos save = in.tellg();
        std::string first;
        in >> first;
        in.seekg(save);

        if (first.find('=') != std::string::npos) {
            Params p = Params::parse(in);
            float3 col = p.v3("diffuse", {0.8f, 0.8f, 0.8f});
            scene.add_material(col,
                p.f("spec", 0.0f), p.f("shin", 32.0f), p.f("amb", 0.1f),
                p.f("aniso", 0.0f), p.f("shift", 0.1f),
                p.f("spec_r", 0.0f), p.f("exp_r", 0.0f),
                p.f("spec_t", 0.0f), p.f("exp_t", 0.0f),
                p.f("back", 0.0f));
            if (p.has("tex")) scene.materials.back().texture_id = p.i("tex", -1);
            if (p.has("mask")) scene.materials.back().mask_id = p.i("mask", -1);
            if (p.has("normal")) scene.materials.back().normal_id = p.i("normal", -1);
            if (p.has("nstr")) scene.materials.back().normal_strength = p.f("nstr", 1.0f);
            return true;
        }

        float r, g, b, spec, shin, amb;
        in >> r >> g >> b >> spec >> shin >> amb;
        float aniso = 0, shift = 0.1f, sr = 0, er = 0, st2 = 0, et = 0, back = 0;
        if (!(in >> aniso)) {
            scene.add_material({r, g, b}, spec, shin, amb);
            return true;
        }
        in >> shift >> sr >> er >> st2 >> et >> back;
        scene.add_material({r, g, b}, spec, shin, amb, aniso, shift, sr, er, st2, et, back);
        return true;
    }
    if (cmd == "sphere") {
        float x, y, z, r; int m;
        in >> x >> y >> z >> r >> m;
        scene.add_sphere({x, y, z}, r, m);
        return true;
    }
    if (cmd == "plane") {
        float nx, ny, nz, d; int m;
        in >> nx >> ny >> nz >> d >> m;
        scene.add_plane({nx, ny, nz}, d, m);
        return true;
    }
    if (cmd == "cube") {
        float x, y, z, s; int m;
        in >> x >> y >> z >> s >> m;
        scene.add_cube({x, y, z}, s, m);
        return true;
    }
    if (cmd == "obj") {
        Params p = Params::parse(in);
        std::string file = p.has("file") ? p.kv.at("file") : "";
        if (file.empty()) { std::cerr << "obj: нужен file=\n"; return false; }
        int mat = p.i("mat", 0);
        float3 off = p.v3("offset", {0.0f, 0.0f, 0.0f});
        float sc = p.f("scale", 1.0f);
        bool smooth = p.i("smooth", 1) != 0;

        std::ifstream f(file);
        if (!f) { std::cerr << "obj: не открыть " << file << "\n"; return false; }

        std::vector<float3> verts, norms;
        std::vector<float2> uvs;
        std::string line;
        int faces = 0;
        while (std::getline(f, line)) {
            if (line.size() < 2) continue;
            std::istringstream ls(line);
            std::string tag;
            ls >> tag;
            if (tag == "v") {
                float x, y, z; ls >> x >> y >> z;
                verts.push_back({x * sc + off.x, y * sc + off.y, z * sc + off.z});
            } else if (tag == "vn") {
                float x, y, z; ls >> x >> y >> z;
                norms.push_back({x, y, z});
            } else if (tag == "vt") {
                float u, v; ls >> u >> v;
                uvs.push_back({u, v});
            } else if (tag == "f") {
                std::vector<int> vi, ni, ti;
                std::string tok;
                bool bad = false;
                while (ls >> tok) {
                    int a = 0, b = 0, c = 0;
                    int got = std::sscanf(tok.c_str(), "%d/%d/%d", &a, &b, &c);
                    if (got < 2) { b = 0; }
                    if (got == 1) {
                        c = 0;
                        if (std::sscanf(tok.c_str(), "%d//%d", &a, &c) != 2) c = 0;
                    }
                    int vidx = (a < 0) ? (int)verts.size() + a : a - 1;
                    int nidx = (c == 0) ? -1 : ((c < 0) ? (int)norms.size() + c : c - 1);
                    int tidx = (b == 0) ? -1 : ((b < 0) ? (int)uvs.size() + b : b - 1);
                    if (vidx < 0 || vidx >= (int)verts.size()) { bad = true; break; }
                    vi.push_back(vidx);
                    ni.push_back(nidx);
                    ti.push_back(tidx);
                }
                if (bad || vi.size() < 3) continue;
                for (size_t k = 2; k < vi.size(); k++) {
                    int i0 = 0, i1 = (int)k - 1, i2 = (int)k;
                    auto nrm = [&](int slot) -> float3 {
                        int nidx = ni[slot];
                        if (nidx >= 0 && nidx < (int)norms.size()) return norms[nidx];
                        return {0.0f, 0.0f, 0.0f};
                    };
                    float3 a = nrm(i0), b = nrm(i1), c2 = nrm(i2);
                    if (!smooth) { a = b = c2 = {0.0f, 0.0f, 0.0f}; }
                    auto tex = [&](int slot) -> float2 {
                        int k = ti[slot];
                        return (k >= 0 && k < (int)uvs.size()) ? uvs[k] : float2{0.0f, 0.0f};
                    };
                    scene.add_triangle_n(verts[vi[i0]], verts[vi[i1]], verts[vi[i2]],
                                         a, b, c2, mat, tex(i0), tex(i1), tex(i2));
                    faces++;
                }
            }
        }
        if (!faces) { std::cerr << "obj: нет граней в " << file << "\n"; return false; }
        return true;
    }
    if (cmd == "triangle") {
        float ax, ay, az, bx, by, bz, cx, cy, cz; int m;
        in >> ax >> ay >> az >> bx >> by >> bz >> cx >> cy >> cz >> m;
        scene.add_triangle({ax, ay, az}, {bx, by, bz}, {cx, cy, cz}, m);
        return true;
    }
    if (cmd == "grid") {
        int nx, nz, matmod;
        float step, y, rad;
        in >> nx >> nz >> step >> y >> rad >> matmod;
        if ((long long)nx * nz > 2000000) {
            std::cerr << "grid: слишком много сфер\n"; return false;
        }
        for (int i = 0; i < nx * nz; i++) {
            float x = (i % nx) * step - nx * step * 0.5f;
            float z = (i / nx) * step - 2.0f;
            scene.add_sphere({x, y, z}, rad, matmod > 0 ? (i % matmod) : 0);
        }
        return true;
    }
    if (cmd == "hair") {
        std::streampos save = in.tellg();
        std::string first;
        in >> first;
        in.seekg(save);

        Params p;
        if (first.find('=') == std::string::npos) {
            // позиционный вид: cx cy cz head count segs seed mat
            float cx, cy, cz, head; int count, segs, mat; unsigned seed;
            in >> cx >> cy >> cz >> head >> count >> segs >> seed >> mat;
            std::stringstream ss;
            ss << "center=" << cx << "," << cy << "," << cz << " head=" << head
               << " count=" << count << " segs=" << segs << " seed=" << seed << " mat=" << mat;
            std::istringstream rebuilt(ss.str());
            p = Params::parse(rebuilt);
        } else {
            p = Params::parse(in);
        }
        add_hair(scene, p);
        return true;
    }
    if (cmd == "hair_mesh") {
        add_hair_mesh(scene, Params::parse(in));
        return true;
    }
    if (cmd == "texture") {
        Params p = Params::parse(in);
        std::string file = p.has("file") ? p.kv.at("file") : "";
        int max_dim = p.i("max", 2048);
        if (file.empty()) { std::cerr << "texture: нужен file=\n"; return false; }
        // Поддерживаем P6 (двоичный PPM): его легко готовить из PNG на стороне Blender.
        std::ifstream tf(file, std::ios::binary);
        if (!tf) { std::cerr << "texture: не открыть " << file << "\n"; return false; }
        std::string magic;
        int w = 0, h = 0, maxv = 0;
        tf >> magic >> w >> h >> maxv;
        if (magic != "P6" || w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            std::cerr << "texture: нужен P6 PPM разумных размеров\n"; return false;
        }
        tf.get();  // один пробельный символ после maxval
        std::vector<unsigned char> raw((size_t)w * h * 3);
        tf.read(reinterpret_cast<char*>(raw.data()), raw.size());
        if (!tf || tf.gcount() != (std::streamsize)raw.size()) {
            std::cerr << "texture: файл короче заявленного размера\n"; return false;
        }

        // при необходимости уменьшаем: nearest, шаг целочисленный
        int step = 1;
        while ((w / step) > max_dim || (h / step) > max_dim) step++;
        if (step > 1) {
            int nw = w / step, nh = h / step;
            std::vector<unsigned char> small((size_t)nw * nh * 3);
            for (int y = 0; y < nh; y++) {
                for (int x = 0; x < nw; x++) {
                    size_t src = ((size_t)(y * step) * w + (size_t)(x * step)) * 3;
                    size_t dst = ((size_t)y * nw + x) * 3;
                    small[dst] = raw[src];
                    small[dst + 1] = raw[src + 1];
                    small[dst + 2] = raw[src + 2];
                }
            }
            std::cerr << "texture: " << file << " " << w << "x" << h
                      << " -> " << nw << "x" << nh << "\n";
            scene.add_texture(nw, nh, small);
        } else {
            scene.add_texture(w, h, raw);
        }
        std::cerr << "texture: " << file << " " << w << "x" << h << "\n";
        return true;
    }
    if (cmd == "mesh") {
        Params p = Params::parse(in);
        std::string file = p.has("file") ? p.kv.at("file") : "";
        int mat_base = p.i("mat0", 0);
        float sc = p.f("scale", 1.0f);
        if (file.empty()) { std::cerr << "mesh: нужен file=\n"; return false; }

        std::ifstream mf(file, std::ios::binary);
        if (!mf) { std::cerr << "mesh: не открыть " << file << "\n"; return false; }
        char magic[8] = {0};
        mf.read(magic, 8);
        if (std::string(magic, 6) != "SMESH1") {
            std::cerr << "mesh: не тот формат\n"; return false;
        }
        uint32_t nv = 0, nn = 0, nu = 0, ntri = 0;
        mf.read((char*)&nv, 4); mf.read((char*)&nn, 4);
        mf.read((char*)&nu, 4); mf.read((char*)&ntri, 4);

        // Защита от мусорных заголовков: объём должен биться с размером файла.
        mf.seekg(0, std::ios::end);
        uint64_t fsize = (uint64_t)mf.tellg();
        mf.seekg(24, std::ios::beg);
        uint64_t need = 24ull + (uint64_t)nv * 12 + (uint64_t)nn * 12
                      + (uint64_t)nu * 8 + (uint64_t)ntri * 36;
        if (fsize < need || nv > 50000000u || ntri > 50000000u) {
            std::cerr << "mesh: битый заголовок (вершин " << nv
                      << ", треугольников " << ntri << ", файл " << fsize << ")\n";
            return false;
        }

        std::vector<float3> verts(nv), norms(nn);
        std::vector<float2> uvs(nu);
        mf.read((char*)verts.data(), (size_t)nv * 12);
        mf.read((char*)norms.data(), (size_t)nn * 12);
        mf.read((char*)uvs.data(), (size_t)nu * 8);

        struct TriRec { int32_t v[3], n[3], t[3], mat; };
        std::vector<TriRec> tris(ntri);
        mf.read((char*)tris.data(), (size_t)ntri * sizeof(TriRec));

        std::vector<int> skip;
        if (p.has("skip")) {
            std::string sk = p.kv.at("skip");
            std::replace(sk.begin(), sk.end(), ',', ' ');
            std::istringstream ss(sk);
            int v;
            while (ss >> v) skip.push_back(v);
        }
        for (const auto& t : tris) {
            if (std::find(skip.begin(), skip.end(), mat_base + t.mat) != skip.end()) continue;
            auto vv = [&](int k) {
                float3 q = verts[t.v[k]];
                return float3{q.x * sc, q.y * sc, q.z * sc};
            };
            auto nm = [&](int k) {
                return (t.n[k] >= 0 && t.n[k] < (int)norms.size())
                     ? norms[t.n[k]] : float3{0.0f, 0.0f, 0.0f};
            };
            auto tc = [&](int k) {
                return (t.t[k] >= 0 && t.t[k] < (int)uvs.size())
                     ? uvs[t.t[k]] : float2{0.0f, 0.0f};
            };
            scene.add_triangle_n(vv(0), vv(1), vv(2), nm(0), nm(1), nm(2),
                                 mat_base + t.mat, tc(0), tc(1), tc(2));
        }
        std::cerr << "mesh: " << file << " вершин " << nv << ", треугольников " << ntri << "\n";
        return true;
    }
    if (cmd == "strands") {
        Params p = Params::parse(in);
        std::string file = p.has("file") ? p.kv.at("file") : "";
        int mat = p.i("mat", 1);
        float scale = p.f("scale", 1.0f);
        float rad_override = p.f("radius", 0.0f);   // если >0, радиусы из файла игнорируются
        if (file.empty()) { std::cerr << "strands: нужен file=\n"; return false; }
        std::ifstream f(file);
        if (!f) { std::cerr << "strands: не открыть " << file << "\n"; return false; }

        std::string line;
        int made = 0;
        while (std::getline(f, line)) {
            std::istringstream ls(line);
            std::string tag;
            ls >> tag;
            if (tag != "strand") continue;
            float r0, r1; int cnt;
            ls >> r0 >> r1 >> cnt;
            if (cnt < 2 || cnt > 100000) continue;
            std::vector<SweepPoint> pts;
            pts.reserve(cnt);
            for (int i = 0; i < cnt; i++) {
                float x, y, z;
                if (!(ls >> x >> y >> z)) break;
                float t = (cnt > 1) ? (float)i / (float)(cnt - 1) : 0.0f;
                SweepPoint sp;
                sp.position = {x * scale, y * scale, z * scale};
                sp.radius = (rad_override > 0.0f)
                          ? rad_override * scale
                          : (r0 + (r1 - r0) * t) * scale;
                pts.push_back(sp);
            }
            if (pts.size() < 2) continue;
            // Длинную прядь режем на куски: иначе её bbox в BVH огромный
            // и теневые лучи проверяют её целиком.
            const size_t chunk = 8;
            if (pts.size() <= chunk) {
                scene.add_sphere_sweep_smooth(pts, mat, 1);
                made++;
            } else {
                for (size_t a = 0; a + 1 < pts.size(); a += chunk - 1) {
                    size_t b = std::min(a + chunk, pts.size());
                    std::vector<SweepPoint> part(pts.begin() + a, pts.begin() + b);
                    if (part.size() >= 2) {
                        scene.add_sphere_sweep_smooth(part, mat, 1);
                        made++;
                    }
                }
            }
        }
        std::cerr << "strands: прочитано " << made << " прядей\n";
        return true;
    }
    if (cmd == "light") {
        float dx, dy, dz, r, g, b, inten;
        in >> dx >> dy >> dz >> r >> g >> b >> inten;
        scene.add_directional_light({dx, dy, dz}, {r, g, b}, inten);
        return true;
    }
    if (cmd == "point") {
        // point cx cy cz r g b power radius
        float cx, cy, cz, r, g, b, power, radius;
        in >> cx >> cy >> cz >> r >> g >> b >> power >> radius;
        scene.add_point_light({cx, cy, cz}, {r, g, b}, power, radius);
        return true;
    }
    if (cmd == "spot") {
        // spot cx cy cz dx dy dz r g b power angle blend radius beam
        // angle — полный угол конуса в радианах (как spot_size в Blender);
        // beam — яркость видимого конуса (0 — не рисовать)
        float cx, cy, cz, dx, dy, dz, r, g, b, power, angle, blend, radius, beam;
        in >> cx >> cy >> cz >> dx >> dy >> dz >> r >> g >> b
           >> power >> angle >> blend >> radius >> beam;
        scene.add_spot_light({cx, cy, cz}, {dx, dy, dz}, {r, g, b},
                             power, angle * 0.5f, blend, radius, beam);
        return true;
    }
    if (cmd == "area") {
        // area cx cy cz sx sy r g b power — прямоугольник в плоскости XZ,
        // светит вниз (-Y)
        float cx, cy, cz, sx, sy, r, g, b, power;
        in >> cx >> cy >> cz >> sx >> sy >> r >> g >> b >> power;
        float3 pos = {cx, cy, cz};
        scene.add_area_light(pos, {sx * 0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, sy * 0.5f},
                             {r, g, b}, power);
        return true;
    }
    if (cmd == "reflect") {
        // reflect <material_index> <value> — доля зеркального отражения
        int idx;
        float v;
        in >> idx >> v;
        if (idx >= 0 && idx < (int)scene.materials.size())
            scene.materials[idx].reflect = v;
        return true;
    }
    if (cmd == "sun_angle") {
        // sun_angle <радианы> — угловой радиус последнего добавленного света
        float a;
        in >> a;
        if (!scene.lights.empty()) scene.lights.back().angle = a;
        return true;
    }
    if (cmd == "photons") {
        // photons <на источник> — проход фотонов (0 — выключен)
        int n;
        in >> n;
        st.photons = n;
        return true;
    }
    if (cmd == "caustics") {
        // caustics <сила> — вклад фотонной карты (0 — выключен)
        float v;
        in >> v;
        st.caustics = v;
        return true;
    }
    if (cmd == "photon_cell") {
        // photon_cell <размер ячейки карты>
        float v;
        in >> v;
        if (v > 0.0f) st.photon_cell = v;
        return true;
    }
    if (cmd == "emit") {
        // emit <material_index> r g b strength — собственное свечение
        int idx;
        float r, g, b, s;
        in >> idx >> r >> g >> b >> s;
        if (idx >= 0 && idx < (int)scene.materials.size()) {
            scene.materials[idx].emission_color = {r, g, b};
            scene.materials[idx].emission = s;
        }
        return true;
    }
    if (cmd == "glass") {
        // glass <material_index> <transmission> <ior>
        int idx;
        float t, ior;
        in >> idx >> t >> ior;
        if (idx >= 0 && idx < (int)scene.materials.size()) {
            scene.materials[idx].transmission = t;
            scene.materials[idx].ior = ior;
        }
        return true;
    }
    if (cmd == "gobo") {
        // gobo <light_index> <texture_index>
        int li, ti;
        in >> li >> ti;
        if (li >= 0 && li < (int)scene.lights.size())
            scene.lights[li].texture_id = ti;
        return true;
    }
    if (cmd == "media") {
        // media cx cy cz hx hy hz sr sg sb sigma_t samples
        // sigma_s — цвет рассеяния, sigma_t — ослабление, samples — шагов
        float cx, cy, cz, hx, hy, hz, sr, sg, sb, st;
        int samples;
        in >> cx >> cy >> cz >> hx >> hy >> hz >> sr >> sg >> sb >> st >> samples;
        scene.add_medium({cx, cy, cz}, {hx, hy, hz}, {sr, sg, sb}, st, samples);
        return true;
    }
    if (cmd == "fill_light") {
        // fill_light dx dy dz r g b intensity
        // как light, но не взаимодействует со средой (media_interaction off)
        float dx, dy, dz, r, g, b, inten;
        in >> dx >> dy >> dz >> r >> g >> b >> inten;
        scene.add_directional_light({dx, dy, dz}, {r, g, b}, inten);
        scene.lights.back().flags = 0;
        return true;
    }
    if (cmd == "hf") {
        // hf cx cy cz size_x size_z height freq octaves seed mat
        float cx, cy, cz, sx, sz, h, freq;
        int oct, seed, mat;
        in >> cx >> cy >> cz >> sx >> sz >> h >> freq >> oct >> seed >> mat;
        scene.add_heightfield({cx, cy, cz}, sx, sz, h, freq, oct, seed, mat);
        return true;
    }
    if (cmd == "hf_image") {
        // hf_image cx cy cz size_x size_z height file.pgm mat
        // grayscale PGM (P5) 0..255, отображается на [0,height] по тайлу.
        float cx, cy, cz, sx, sz, h;
        std::string file;
        int mat;
        in >> cx >> cy >> cz >> sx >> sz >> h >> file >> mat;
        std::ifstream imf(file, std::ios::binary);
        if (!imf) { std::cerr << "hf_image: не открыть " << file << "\n"; return false; }
        std::string magic;
        int w = 0, hgt = 0, maxv = 0;
        imf >> magic >> w >> hgt >> maxv;
        if (magic != "P5" || w <= 0 || hgt <= 0 || w > 16384 || hgt > 16384) {
            std::cerr << "hf_image: нужен grayscale PGM (P5) разумных размеров\n"; return false;
        }
        imf.get();  // пробел после maxval
        std::vector<unsigned char> pix((size_t)w * hgt);
        imf.read(reinterpret_cast<char*>(pix.data()), pix.size());
        if (!imf || imf.gcount() != (std::streamsize)pix.size()) {
            std::cerr << "hf_image: файл короче заявленного размера\n"; return false;
        }
        scene.add_heightfield_from_image({cx, cy, cz}, sx, sz, h,
                                         pix.data(), w, hgt, mat);
        std::cerr << "hf_image: " << file << " " << w << "x" << hgt << "\n";
        return true;
    }
    if (cmd == "se") {
        // se cx cy cz a b c n mat  (суперэллипсоид, куб с фасками при n>2)
        float cx, cy, cz, a, b, c, n;
        int mat;
        in >> cx >> cy >> cz >> a >> b >> c >> n >> mat;
        scene.add_superellipsoid({cx, cy, cz}, a, b, c, n, mat);
        return true;
    }
    if (cmd == "se_csg") {
        // se_csg cx cy cz a b c n mat — суперэллипсоид только для CSG,
        // отдельно не рендерится (стены с проёмами и т.п.)
        float cx, cy, cz, a, b, c, n;
        int mat;
        in >> cx >> cy >> cz >> a >> b >> c >> n >> mat;
        scene.add_superellipsoid_hidden({cx, cy, cz}, a, b, c, n, mat);
        return true;
    }
    if (cmd == "lathe") {
        // lathe cx cy cz mat  n  y1 r1 y2 r2 ...  (профиль: высота + радиус)
        float cx, cy, cz;
        int mat;
        in >> cx >> cy >> cz >> mat;
        int n;
        in >> n;
        if (n > 0 && n <= 512) {
            std::vector<LathePoint> pts;
            pts.reserve(n);
            for (int i = 0; i < n; i++) {
                float y, r;
                in >> y >> r;
                pts.push_back({y, r, 0, 0});
            }
            scene.add_lathe({cx, cy, cz}, pts, mat);
        }
        return true;
    }
    if (cmd == "lathe_bezier") {
        // lathe_bezier cx cy cz mat subdiv  n  y1 r1 y2 r2 ...
        // Контрольные точки профиля (y, radius) — это управляющие точки
        // кубических Безье: идут группами по 4, одна группа = один сегмент
        // кубика от первой до четвёртой точки (непрерывная кривая).
        float cx, cy, cz;
        int mat, subdiv;
        in >> cx >> cy >> cz >> mat >> subdiv;
        int n;
        in >> n;
        if (n > 0 && n <= 2048 && subdiv >= 1 && subdiv <= 64) {
            std::vector<Bz2> ctrl;
            ctrl.reserve(n);
            for (int i = 0; i < n; i++) {
                float y, r;
                in >> y >> r;
                ctrl.push_back({y, r});
            }
            // Отбрасываем «хвост» меньше 4 точек — неполная группа не кривая.
            ctrl.resize((int)ctrl.size() / 4 * 4);
            std::vector<Bz2> line;
            tessellate_bezier_run(ctrl, subdiv, line);
            std::vector<LathePoint> pts;
            pts.reserve(line.size());
            for (const auto& q : line) pts.push_back({q.a, q.b, 0, 0});
            scene.add_lathe({cx, cy, cz}, pts, mat);
        }
        return true;
    }
    if (cmd == "prism_bezier") {
        // prism_bezier cx cy cz y1 y2 sweep mat subdiv  n  x1 z1 x2 z2 ...
        // Контрольные точки (x,z) — кубики группами по 4. Контур ОБЯЗАТЕЛЬНО
        // замкнут: первая точка добавляется повторно в конец (как и в prism).
        float cx, cy, cz, y1, y2, sweep;
        int mat, subdiv;
        in >> cx >> cy >> cz >> y1 >> y2 >> sweep >> mat >> subdiv;
        int n;
        in >> n;
        if (n > 0 && n <= 2048 && subdiv >= 1 && subdiv <= 64) {
            std::vector<Bz2> ctrl;
            ctrl.reserve(n);
            for (int i = 0; i < n; i++) {
                float x, z;
                in >> x >> z;
                ctrl.push_back({x, z});
            }
            ctrl.resize((int)ctrl.size() / 4 * 4);
            std::vector<Bz2> line;
            tessellate_bezier_run(ctrl, subdiv, line);
            std::vector<PrismPoint> pts;
            pts.reserve(line.size() + 1);
            for (const auto& q : line) pts.push_back({q.a, q.b, 0, 0});
            if (!pts.empty()) pts.push_back(pts.front());  // замыкание контура
            scene.add_prism({cx, cy, cz}, pts, y1, y2, sweep, mat);
        }
        return true;
    }
    if (cmd == "prism") {
        // prism cx cy cz y1 y2 sweep mat  n  x1 z1 x2 z2 ...  (XZ-контуры, Y-экструзия)
        float cx, cy, cz, y1, y2, sweep;
        int mat;
        in >> cx >> cy >> cz >> y1 >> y2 >> sweep >> mat;
        int n;
        in >> n;
        if (n > 0 && n <= 2048) {
            std::vector<PrismPoint> pts;
            pts.reserve(n);
            for (int i = 0; i < n; i++) {
                float x, z;
                in >> x >> z;
                pts.push_back({x, z, 0, 0});
            }
            scene.add_prism({cx, cy, cz}, pts, y1, y2, sweep, mat);
        }
        return true;
    }
    if (cmd == "csg_begin") {
        // csg_begin op   (0=union, 1=intersection, 2=difference)
        int op;
        in >> op;
        CSGNode n;
        n.op = op;
        n.leaf_kind = -1;
        n.leaf_index = -1;
        n.left = n.right = -1;
        scene.csg_nodes.push_back(n);
        csg_stack.push_back((int)scene.csg_nodes.size() - 1);
        return true;
    }
    if (cmd == "csg_leaf") {
        // csg_leaf kind index   (0=sphere, 1=superellipsoid)
        int kind, idx;
        in >> kind >> idx;
        CSGNode n;
        n.op = 0;
        n.leaf_kind = kind;
        n.leaf_index = idx;
        n.left = n.right = -1;
        scene.csg_nodes.push_back(n);
        if (!csg_stack.empty()) {
            int parent = csg_stack.back();
            if (scene.csg_nodes[parent].left < 0) scene.csg_nodes[parent].left = (int)scene.csg_nodes.size() - 1;
            else                               scene.csg_nodes[parent].right = (int)scene.csg_nodes.size() - 1;
        }
        return true;
    }
    if (cmd == "csg_end") {
        // завершить узел: привязать к родителю, если есть
        if (!csg_stack.empty()) {
            int cur = csg_stack.back();
            csg_stack.pop_back();
            if (!csg_stack.empty()) {
                int parent = csg_stack.back();
                if (scene.csg_nodes[parent].left < 0) scene.csg_nodes[parent].left = cur;
                else                                 scene.csg_nodes[parent].right = cur;
            } else {
                scene.csg_root = cur;   // корень всего дерева
            }
        }
        return true;
    }
    if (cmd == "sweep" || cmd == "sweep_smooth") {
        int mat, subdivs;
        in >> mat >> subdivs;
        std::vector<SweepPoint> pts;
        float x, y, z, r;
        while (in >> x >> y >> z >> r) pts.push_back({{x, y, z}, r});
        if (pts.size() < 2) {
            std::cerr << "sweep: нужно минимум две точки\n";
            return false;
        }
        if (subdivs > 0) scene.add_sphere_sweep_smooth(pts, mat, subdivs);
        else             scene.add_sphere_sweep(pts, mat);
        return true;
    }

    std::cerr << "неизвестная команда: " << cmd << "\n";
    return false;
}

bool load_scene_file(const std::string& path, Scene& scene, SceneSettings& settings) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "не открыть сцену: " << path << "\n";
        return false;
    }
    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        line_no++;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream in(line);
        if (!parse_line(in, scene, settings)) {
            std::cerr << "строка " << line_no << ": " << line << "\n";
            return false;
        }
    }
    // Проверка ссылок на материалы: индекс вне диапазона даёт мусор в шейдере.
    const int nm = (int)scene.materials.size();
    auto clamp_mat = [&](int& m) {
        if (nm > 0 && (m < 0 || m >= nm)) {
            std::cerr << "предупреждение: материал " << m << " вне диапазона, взят 0\n";
            m = 0;
        }
    };
    for (auto& s : scene.sweep_strands) clamp_mat(s.material_id);
    for (auto& t : scene.triangles) clamp_mat(t.material_id);
    for (auto& o : scene.objects) clamp_mat(o.material_id);
    for (auto& pl : scene.planes) clamp_mat(pl.material_id);
    for (auto& hf : scene.heightfields) clamp_mat(hf.material_id);
    for (auto& se : scene.superellipsoids) clamp_mat(se.material_id);
    for (auto& lt : scene.lathes) clamp_mat(lt.material_id);
    for (auto& pr : scene.prisms) clamp_mat(pr.material_id);

    scene.build_bvh();
    return true;
}
