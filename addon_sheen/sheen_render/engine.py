#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Sheen Render — движок: Blender (bpy) → бинарный SheenScene → libsheen_c_api.so
#
# Этап 1 (ROADMAP): bpy-мост через ctypes. Никаких файлов-прокладок, JSON
# и процессов. Данные камеры/меша/материалов/волос читаются из Blender
# в память (depsgraph), упаковываются в C-структуры и передаются в ядро
# одним ctypes-вызовом.
#
# Структуры (c_api_contract.py) обязаны совпадать с types.h бит-в-бит.

import ctypes
import os
import sys
from pathlib import Path

import bpy

from .c_api_contract import (
    CameraData, Float2, Float3, HeightField, Light, Material, Medium, Object,
    Plane, SheenScene, SweepPoint, SweepStrand, Triangle,
)

SHEEN_CUDA_ARCH = 61

# Библиотека и нативный модуль лежат рядом с engine.py — их кладёт туда
# `cmake --install build`. Никакого поиска по каталогам сборки: если файла
# нет, значит аддон не установлен. Для разработки можно указать SHEEN_LIB.
_NATIVE_MODULE = Path(__file__).parent / "sheen.so"
SHEEN_LIB_PATH = Path(os.environ.get("SHEEN_LIB") or (Path(__file__).parent / "libsheen_c_api.so"))

# Конвенция координат: Blender (Z-up) → Sheen (Y-up)
def _vec(v):
    return Float3(v[0], v[2], -v[1])

def _unit(v, fallback=(0.0, 1.0, 0.0)):
    x, y, z = float(v[0]), float(v[1]), float(v[2])
    n = (x * x + y * y + z * z) ** 0.5
    if n < 1e-12:
        x, y, z = fallback
        n = (x * x + y * y + z * z) ** 0.5 or 1.0
    return (x / n, y / n, z / n)

class _NativeRenderer:
    """Рендер через нативный Python-модуль sheen (Этап 2, import sheen)."""

    def __init__(self):
        self._native = None

    def load(self):
        if self._native is not None:
            return True
        if not _NATIVE_MODULE.exists():
            print(f"[Sheen] Нативный модуль не найден: {_NATIVE_MODULE}")
            return False
        try:
            # Модуль лежит рядом с engine.py; в Blender его каталог может
            # не быть в sys.path — добавляем явно.
            if str(_NATIVE_MODULE.parent) not in sys.path:
                sys.path.insert(0, str(_NATIVE_MODULE.parent))
            import sheen as native
            native.init(0, SHEEN_CUDA_ARCH)
            self._native = native
            print("[Sheen] Нативный модуль загружен (import sheen)")
            return True
        except Exception as e:
            print(f"[Sheen] Не удалось загрузить нативный модуль: {e}")
            return False

    def render(self, scene, width, height, aa_samples, output_path) -> bool:
        if not self.load():
            return False
        try:
            self._native.render_scene(scene, width, height, aa_samples, output_path)
            return True
        except Exception as e:
            print(f"[Sheen] Ошибка рендера: {e}")
            return False

    def cleanup(self):
        if self._native is not None:
            try:
                self._native.cleanup()
            except Exception:
                pass
            self._native = None

class SheenRenderer:
    """Тонкая обёртка над libsheen_c_api.so через ctypes."""

    def __init__(self):
        self._lib = None

    def load(self):
        if self._lib is not None:
            return True
        if not SHEEN_LIB_PATH.exists():
            print(f"[Sheen] Библиотека не найдена: {SHEEN_LIB_PATH}\n"
                  f"[Sheen] Собери и установи аддон: cmake --build build && cmake --install build")
            return False
        lib = ctypes.CDLL(str(SHEEN_LIB_PATH))

        lib.sheen_init.argtypes = [ctypes.c_int, ctypes.c_int]
        lib.sheen_init.restype = ctypes.c_int

        lib.sheen_cleanup.argtypes = []
        lib.sheen_cleanup.restype = None

        lib.sheen_render_scene.argtypes = [
            ctypes.POINTER(SheenScene),
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_char_p,
        ]
        lib.sheen_render_scene.restype = ctypes.c_int

        lib.sheen_get_last_error.argtypes = []
        lib.sheen_get_last_error.restype = ctypes.c_char_p

        if lib.sheen_init(0, SHEEN_CUDA_ARCH) != 0:
            print(f"[Sheen] Ошибка инициализации: {lib.sheen_get_last_error().decode()}")
            return False

        self._lib = lib
        # DEBUG: verify fresh library is loaded
        try:
            mtime = os.path.getmtime(str(SHEEN_LIB_PATH))
            print(f"[Sheen] CUDA-слой загружен: {SHEEN_LIB_PATH.name} (mtime={mtime:.0f})")
        except Exception:
            print(f"[Sheen] CUDA-слой загружен: {SHEEN_LIB_PATH}")
        return True

    def cleanup(self):
        if self._lib is not None:
            self._lib.sheen_cleanup()
            self._lib = None

    def render(self, scene: SheenScene, width: int, height: int,
               aa_samples: int, output_path: str) -> bool:
        if not self.load():
            return False
        result = self._lib.sheen_render_scene(
            ctypes.byref(scene), width, height, aa_samples,
            output_path.encode("utf-8"))
        if result != 0:
            print(f"[Sheen] Ошибка рендера: {self._lib.sheen_get_last_error().decode()}")
            return False
        return True


# ======================================================================
# Упаковка данных из Blender в SheenScene
# ======================================================================

def _extract_camera(scene):
    """Камера сцены: активная, иначе первая видимая.

    Базис берём из matrix_world целиком (поворот + масштаб): локальные оси
    Blender-камеры — -Z вперёд, +Y вверх, +X вправо. Раньше из направления
    вычиталась позиция камеры, из-за чего кадр не совпадал с вьюпортом.
    """
    cam_obj = scene.camera
    if cam_obj is None:
        for obj in scene.objects:
            if obj.type == "CAMERA" and obj.visible_get():
                cam_obj = obj
                break

    cam = CameraData()
    if cam_obj is None:
        print("[Sheen] В сцене нет камеры — беру обзор по умолчанию")
        cam.position = Float3(0.0, 0.0, 0.0)
        cam.forward = Float3(0.0, 0.0, -1.0)
        cam.right = Float3(1.0, 0.0, 0.0)
        cam.up = Float3(0.0, 1.0, 0.0)
        cam.focal_length = 0.0
        return cam

    from mathutils import Vector
    mw = cam_obj.matrix_world
    rot = mw.to_quaternion()
    origin = mw.translation
    fwd = rot @ Vector((0.0, 0.0, -1.0))
    up = rot @ Vector((0.0, 1.0, 0.0))
    right = rot @ Vector((1.0, 0.0, 0.0))

    cam.position = _vec((origin.x, origin.y, origin.z))
    cam.forward = _vec(_unit((fwd.x, fwd.y, fwd.z), (0.0, 0.0, -1.0)))
    cam.up = _vec(_unit((up.x, up.y, up.z), (0.0, 1.0, 0.0)))
    cam.right = _vec(_unit((right.x, right.y, right.z), (1.0, 0.0, 0.0)))

    data = cam_obj.data
    if hasattr(data, "lens") and data.lens:
        cam.focal_length = data.lens        # мм
        cam.sensor_width = data.sensor_width if hasattr(data, "sensor_width") else 36.0
    else:
        cam.focal_length = 0.0
    cam.shift_x = data.shift_x if hasattr(data, "shift_x") else 0.0
    cam.shift_y = data.shift_y if hasattr(data, "shift_y") else 0.0
    return cam


# ----------------------------------------------------------------------
# Материалы
# ----------------------------------------------------------------------

def _default_material() -> Material:
    m = Material()
    m.diffuse_color = Float3(0.8, 0.8, 0.8)
    m.specular = 0.0
    m.shininess = 32.0
    m.ambient = 0.1
    m.texture_id = -1
    m.anisotropy = 0.0
    m.reflect = 0.0
    m.transmission = 0.0
    m.ior = 1.45
    m.emission_color = Float3(0.0, 0.0, 0.0)
    m.emission = 0.0
    m.mask_id = -1
    m.normal_id = -1
    m.normal_strength = 1.0
    return m


class _TextureTable:
    """Изображения материалов → плоские RGB-байты для ядра.

    Пиксели Blender — линейные float RGBA; в контракте текстура — байты RGB,
    и ядро делит их на 255 в линейном пространстве, поэтому sRGB-декод не
    нужен: просто квантуем линейные значения.
    """

    def __init__(self):
        self.pixels = []
        self.widths = []
        self.heights = []
        self._ids = {}

    def id_of(self, image) -> int:
        key = image.name
        if key in self._ids:
            return self._ids[key]

        import numpy as np
        w, h = image.size
        if w < 1 or h < 1:
            return -1
        buf = np.empty(w * h * 4, dtype=np.float32)
        image.pixels.foreach_get(buf)
        rgb = (np.clip(buf.reshape(-1, 4)[:, :3], 0.0, 1.0) * 255.0).astype(np.uint8)
        data = (ctypes.c_ubyte * rgb.size).from_buffer_copy(rgb.tobytes())

        idx = len(self.widths)
        self.pixels.append(data)
        self.widths.append(w)
        self.heights.append(h)
        self._ids[key] = idx
        return idx


def _node_image(mat):
    """Image Texture, подключённая к Base Color у Principled."""
    if not mat.use_nodes or not mat.node_tree:
        return None
    for node in mat.node_tree.nodes:
        if node.type != "BSDF_PRINCIPLED":
            continue
        base = node.inputs.get("Base Color")
        if base is not None and base.is_linked:
            src = base.links[0].from_node
            if src.type == "TEX_IMAGE" and src.image is not None:
                return src.image
    return None


def _node_normal(mat):
    """Normal Map (или Image Texture сразу) на входе Normal у Principled.

    Возвращает (image, strength); картинка нормалей — в тангентном
    пространстве, сила берётся из Normal Map.Strength.
    """
    if not mat.use_nodes or not mat.node_tree:
        return None, 1.0
    for node in mat.node_tree.nodes:
        if node.type != "BSDF_PRINCIPLED":
            continue
        inp = node.inputs.get("Normal")
        if inp is None or not inp.is_linked:
            return None, 1.0
        src = inp.links[0].from_node
        if src.type == "NORMAL_MAP":
            strength = float(src.inputs["Strength"].default_value)
            color = src.inputs.get("Color")
            if color is not None and color.is_linked:
                tex = color.links[0].from_node
                if tex.type == "TEX_IMAGE" and tex.image is not None:
                    return tex.image, strength
        elif src.type == "TEX_IMAGE" and src.image is not None:
            return src.image, 1.0
        return None, 1.0
    return None, 1.0


def _material_params(mat, textures=None) -> Material:
    """Blender-материал → Material ядра.

    Цвет и глянец берём из Principled BSDF (так материалы выглядят для
    пользователя), иначе — из Viewport Display. Раньше нодовые материалы
    молча становились серыми 0.8.
    """
    m = _default_material()
    color = tuple(mat.diffuse_color)[:3]
    spec = 0.0
    shininess = 32.0

    if mat.use_nodes and mat.node_tree:
        for node in mat.node_tree.nodes:
            if node.type != "BSDF_PRINCIPLED":
                continue
            color = tuple(node.inputs["Base Color"].default_value)[:3]
            rough = float(node.inputs["Roughness"].default_value)
            spec_in = node.inputs.get("Specular IOR Level")
            spec = float(spec_in.default_value) if spec_in else 0.5
            # GGX roughness → показатель Блинна-Фонга: alpha = r^2, exp = 2/alpha^2 - 2
            shininess = 2.0 / max(rough, 0.02) ** 4 - 2.0
            break

    m.diffuse_color = Float3(*[min(max(float(c), 0.0), 1.0) for c in color])
    m.specular = min(max(spec, 0.0), 1.0)
    m.shininess = min(max(shininess, 1.0), 4096.0)
    m.ambient = 0.1
    m.texture_id = -1
    if textures is not None:
        image = _node_image(mat)
        if image is not None:
            m.texture_id = textures.id_of(image)
    m.mask_id = -1
    if textures is not None:
        mask_image = getattr(mat, "sheen_mask", None)
        if mask_image is not None:
            m.mask_id = textures.id_of(mask_image)
    m.normal_id = -1
    m.normal_strength = 1.0
    if textures is not None:
        normal_image, normal_strength = _node_normal(mat)
        if normal_image is not None:
            m.normal_id = textures.id_of(normal_image)
            m.normal_strength = normal_strength
    m.anisotropy = 0.0
    m.reflect = float(getattr(mat, "sheen_reflect", 0.0))
    m.transmission = 0.0
    m.ior = 1.45
    m.emission_color = Float3(0.0, 0.0, 0.0)
    m.emission = 0.0
    if mat.use_nodes and mat.node_tree:
        for node in mat.node_tree.nodes:
            if node.type != "BSDF_PRINCIPLED":
                continue
            tr = node.inputs.get("Transmission Weight")
            io = node.inputs.get("IOR")
            if tr is not None:
                m.transmission = min(max(float(tr.default_value), 0.0), 1.0)
            if io is not None:
                m.ior = max(float(io.default_value), 1.0)
            # Собственное свечение: лампа видна как тело, а не только пятном.
            em = node.inputs.get("Emission Color")
            es = node.inputs.get("Emission Strength")
            if em is not None:
                ec = tuple(em.default_value)[:3]
                m.emission_color = Float3(*[max(float(c), 0.0) for c in ec])
            if es is not None:
                m.emission = max(float(es.default_value), 0.0)
            break
    return m


class _MaterialTable:
    """Материалы сцены с ленивым присвоением индексов.

    Индекс считается по фактически используемым материалам объекта-слота,
    а не по порядку bpy.data.materials — иначе все полигоны красились
    материалом №0.
    """

    def __init__(self, textures=None):
        self.materials = []
        self._ids = {}
        self._default_id = None
        self.textures = textures

    def id_of(self, mat) -> int:
        if mat is None:
            return self.default_id()
        key = mat.name
        if key not in self._ids:
            self._ids[key] = len(self.materials)
            self.materials.append(_material_params(mat, self.textures))
        return self._ids[key]

    def default_id(self) -> int:
        if self._default_id is None:
            self._default_id = len(self.materials)
            self.materials.append(_default_material())
        return self._default_id

    def seal(self):
        # C-API не принимает сцену без материалов
        if not self.materials:
            self.default_id()


def _slot_material(obj, index):
    slots = obj.material_slots
    if 0 <= index < len(slots):
        return slots[index].material
    return None


# ----------------------------------------------------------------------
# Геометрия
# ----------------------------------------------------------------------

def _extract_objects(scene, depsgraph, mats) -> list:
    """Видимые меши → треугольники с мировыми позициями/нормалями и UV."""
    triangles = []
    for obj in scene.objects:
        if obj.type != "MESH" or not obj.visible_get():
            continue
        if obj.get("sheen_hf_image") or obj.get("sheen_media"):
            continue   # служебные объекты: HF-превью и контейнер среды
        ev = obj.evaluated_get(depsgraph)
        me = ev.to_mesh()
        if not me:
            continue
        try:
            mw = ev.matrix_world
            rot = mw.to_3x3()
            uv_data = me.uv_layers.active.data if me.uv_layers.active else None

            corner_normals = None
            try:
                if len(me.corner_normals) == len(me.loops):
                    corner_normals = me.corner_normals
            except (AttributeError, RuntimeError):
                corner_normals = None

            for poly in me.polygons:
                if poly.loop_total < 3:
                    continue
                loops = list(poly.loop_indices)
                co = [me.vertices[i].co for i in poly.vertices]
                if corner_normals is not None:
                    normals = [corner_normals[k].vector for k in loops]
                else:
                    normals = [poly.normal] * len(loops)
                if uv_data is not None:
                    uvs = [tuple(uv_data[k].uv) for k in loops]
                else:
                    uvs = [(0.0, 0.0)] * len(loops)

                mid = mats.id_of(_slot_material(ev, poly.material_index))
                poly_normal = tuple(poly.normal)

                # Fan-триангуляция
                for k in range(1, len(loops) - 1):
                    idx = (0, k, k + 1)
                    tris = Triangle()
                    p = [mw @ co[i] for i in idx]
                    tris.v0 = _vec(p[0][:3])
                    tris.v1 = _vec(p[1][:3])
                    tris.v2 = _vec(p[2][:3])

                    n = [_vec(_unit((rot @ normals[i])[:3], poly_normal)) for i in idx]
                    tris.n0, tris.n1, tris.n2 = n
                    tris.uv0 = Float2(*uvs[idx[0]])
                    tris.uv1 = Float2(*uvs[idx[1]])
                    tris.uv2 = Float2(*uvs[idx[2]])
                    tris.material_id = mid
                    triangles.append(tris)
        finally:
            ev.to_mesh_clear()
    return triangles


def _extract_heightfields(scene, mats) -> tuple:
    """Объекты-HeightField (утёс) → (list[HeightField], list[buffer]).

    Источник — объекты с custom-пропсами sheen_hf_* (создаёт оператор
    SHEEN_OT_AddHeightField). Из выбранного изображения берём grayscale-
    массив (R-канал), кладём в HeightField.image; image/img_w/img_h мост
    копирует в C-контракт. Превью-меш (grid+displace) — из той же картинки,
    поэтому «что видишь во вьюпорте — то рендерится».

    Второй элемент возврата — список ctypes-буферов пикселей карт; без них
    (image указывает на них) ctypes мог бы их собрать до рендера. Держатель
    привязывает их к сцене и живёт, пока жив SheenScene.
    """
    hfs = []
    holders = []
    for obj in scene.objects:
        name = obj.get("sheen_hf_image")
        if not name:
            continue
        img = bpy.data.images.get(name)
        if img is None or img.size[0] < 1 or img.size[1] < 1:
            print(f"[Sheen] HF '{obj.name}': изображение '{name}' недоступно")
            continue
        w, h = img.size
        # grayscale-карта высот: берём R-канал как single channel byte 0..255
        px = img.pixels if img.pixels else None
        if not px or len(px) < w * h * 4:
            print(f"[Sheen] HF '{obj.name}': нет пикселей у изображения")
            continue
        data = (ctypes.c_ubyte * (w * h))()
        for y in range(h):
            src = (h - 1 - y) * w
            dst = y * w
            for x in range(w):
                r = px[(src + x) * 4 + 0]
                data[dst + x] = int(max(0.0, min(1.0, r)) * 255.0)

        sz = float(obj.get("sheen_hf_size", 4.0))
        ht = float(obj.get("sheen_hf_height", 1.0))
        mw = obj.matrix_world
        center = _vec((mw[0][3], mw[1][3], mw[2][3]))

        hf = HeightField()
        hf.center = Float3(center.x, center.y, center.z)
        hf.size_x = sz
        hf.size_z = sz
        hf.height = ht
        hf.freq = 1.0
        hf.octaves = 1
        hf.seed = -1
        hf.material_id = mats.id_of(_slot_material(obj, 0))
        # указатель на массив: ctypes держит его в data, переживает вызов
        hf.image = ctypes.cast(data, ctypes.POINTER(ctypes.c_ubyte))
        hf.img_w = w
        hf.img_h = h
        hfs.append(hf)
        holders.append(data)   # живёт, пока жив SheenScene
        print(f"[Sheen] HF '{obj.name}': {w}x{h} карта, size={sz}, height={ht}")
    return hfs, holders


def _extract_lights(scene, textures) -> list:
    """Лампы Blender → Light ядра.

    Каждое поле берётся из свойств объекта; своих значений не подставляем.
    Неподдержанный тип пропускается с сообщением, а не превращается в Sun.
    """
    from mathutils import Vector

    lights = []
    for obj in scene.objects:
        if obj.type != "LIGHT" or not obj.visible_get():
            continue
        data = obj.data
        kind = data.type
        mw = obj.matrix_world
        rot = mw.to_quaternion()
        pos = mw.translation
        forward = rot @ Vector((0.0, 0.0, -1.0))   # Blender: светит вдоль -Z

        light = Light()
        light.color = Float3(data.color.r, data.color.g, data.color.b)
        light.intensity = float(data.energy)
        # остальные поля — явно, чтобы ничего не появлялось «само»
        light.direction = Float3(0.0, 0.0, 0.0)
        light.position = Float3(0.0, 0.0, 0.0)
        light.u_axis = Float3(0.0, 0.0, 0.0)
        light.v_axis = Float3(0.0, 0.0, 0.0)
        light.radius = 0.0
        light.angle = 0.0
        light.blend = 0.0
        light.texture_id = -1
        light.flags = 1 if getattr(data, "sheen_media", True) else 0

        if kind == "SUN":
            light.type = 0
            light.direction = _vec(_unit((forward.x, forward.y, forward.z),
                                         (0.0, 0.0, -1.0)))
            # Blender хранит полный угловой диаметр; ядру нужен радиус
            light.angle = float(data.angle) * 0.5
        elif kind == "POINT":
            light.type = 1
            light.position = _vec((pos.x, pos.y, pos.z))
            light.radius = float(data.shadow_soft_size)
        elif kind == "SPOT":
            light.type = 3
            light.position = _vec((pos.x, pos.y, pos.z))
            light.direction = _vec(_unit((forward.x, forward.y, forward.z),
                                         (0.0, 0.0, -1.0)))
            light.radius = float(data.shadow_soft_size)
            light.angle = float(data.spot_size) * 0.5   # Blender: полный угол
            light.blend = float(data.spot_blend)
            light.beam = float(getattr(data, "sheen_beam", 0.0))
            gobo = getattr(data, "sheen_gobo", None)
            if gobo is not None:
                light.texture_id = textures.id_of(gobo)
        elif kind == "AREA":
            light.type = 2
            light.position = _vec((pos.x, pos.y, pos.z))
            sx = float(data.size) * 0.5
            if data.shape in {"RECTANGLE", "ELLIPSE"}:
                sy = float(data.size_y) * 0.5
            else:
                sy = sx
            u = rot @ Vector((sx, 0.0, 0.0))
            v = rot @ Vector((0.0, sy, 0.0))
            light.u_axis = _vec((u.x, u.y, u.z))
            light.v_axis = _vec((v.x, v.y, v.z))
        else:
            print(f"[Sheen] Лампа '{obj.name}': тип {kind} не поддержан — пропущена")
            continue

        lights.append(light)
    return lights


def _extract_media(scene, depsgraph) -> list:
    """Объекты-контейнеры среды (sheen_media) → Medium по их AABB.

    Контейнер не рендерится: он задаёт только объём, в котором считается
    рассеяние. Как и в POV-Ray, бокс должен быть минимальным — цена линейна
    по числу шагов вдоль луча.
    """
    from mathutils import Vector

    media = []
    for obj in scene.objects:
        if obj.type != "MESH" or not obj.get("sheen_media"):
            continue
        mw = obj.matrix_world
        corners = [mw @ Vector(c) for c in obj.bound_box]
        mn = Vector((min(c.x for c in corners), min(c.y for c in corners),
                     min(c.z for c in corners)))
        mx = Vector((max(c.x for c in corners), max(c.y for c in corners),
                     max(c.z for c in corners)))
        center = (mn + mx) * 0.5
        half = (mx - mn) * 0.5

        scatter = float(obj.get("sheen_media_scatter", 0.07))
        extinction = float(obj.get("sheen_media_extinction", 0.01))
        samples = int(obj.get("sheen_media_samples", 16))

        m = Medium()
        m.center = _vec((center.x, center.y, center.z))
        m.half = Float3(half.x, half.z, half.y)   # Y-up: полуоси без знака
        m.sigma_s = Float3(scatter, scatter, scatter)
        m.sigma_t = extinction
        m.samples = max(1, min(samples, 64))
        media.append(m)
    return media


def _extract_hair(scene, depsgraph, mats) -> tuple:
    """Волосы (particle hair) → (points, strands)."""
    points = []
    strands = []
    for obj in scene.objects:
        if obj.type != "MESH" or not obj.visible_get():
            continue
        for psys in obj.particle_systems:
            if psys.settings.type != "HAIR":
                continue
            r_root = psys.settings.root_radius
            r_tip = psys.settings.tip_radius
            mw = obj.matrix_world
            mid = mats.id_of(_slot_material(obj, psys.settings.material))
            offset = len(points)
            for part in psys.particles:
                keys = part.hair_keys
                if len(keys) < 2:
                    continue
                count = len(keys)
                strands.append(SweepStrand(
                    point_offset=offset,
                    point_count=count,
                    material_id=mid,
                ))
                for i, k in enumerate(keys):
                    w = mw @ k.co
                    t = i / (count - 1) if count > 1 else 0.0
                    r = r_root + (r_tip - r_root) * t
                    points.append(SweepPoint(_vec((w[0], w[1], w[2])), r))
                offset += count
    return points, strands


def build_sheen_scene(scene=None, depsgraph=None) -> SheenScene:
    """Собрать SheenScene из сцены Blender.

    Принимает сцену и depsgraph (путь RenderEngine) или bpy-контекст
    (путь операторов). Второй аргумент при передаче контекста — сам контекст.
    """
    if hasattr(scene, "evaluated_depsgraph_get"):     # передан контекст
        context = scene
        scn = context.scene
        depsgraph = context.evaluated_depsgraph_get()
    else:
        scn = scene if scene is not None else bpy.context.scene
        if depsgraph is None:
            depsgraph = bpy.context.evaluated_depsgraph_get()

    textures = _TextureTable()
    mats = _MaterialTable(textures)
    triangles = _extract_objects(scn, depsgraph, mats)
    hair_points, hair_strands = _extract_hair(scn, depsgraph, mats)
    lights = _extract_lights(scn, textures)
    heightfields, hf_holders = _extract_heightfields(scn, mats)
    media = _extract_media(scn, depsgraph)
    mats.seal()

    scene = SheenScene()
    scene.camera = _extract_camera(scn)
    scene.photons = int(getattr(scn, "sheen_photons", 0))
    scene.photon_cell = float(getattr(scn, "sheen_photon_cell", 0.25))
    scene.caustics = float(getattr(scn, "sheen_caustics", 0.0))
    # Debug: print values before kernel launch
    print(f"[Sheen DEBUG] photons={scene.photons}, caustics={scene.caustics}")
    # ctypes-структура хранит только адреса массивов. Если не держать сами
    # массивы, сборщик мусора может освободить их до вызова ядра, и рендер
    # получит мусор вместо материалов/треугольников.
    buffers = [hf_holders]
    scene._buffers = buffers

    def put(field_pointer, field_count, cls, items):
        arr = (cls * max(len(items), 1))(*items) if items else None
        setattr(scene, field_pointer, arr)
        setattr(scene, field_count, len(items))
        if arr is not None:
            buffers.append(arr)

    put("materials", "num_materials", Material, mats.materials)
    put("triangles", "num_triangles", Triangle, triangles)
    put("heightfields", "num_heightfields", HeightField, heightfields)
    put("sweep_points", "num_sweep_points", SweepPoint, hair_points)
    put("sweep_strands", "num_sweep_strands", SweepStrand, hair_strands)
    put("lights", "num_lights", Light, lights)
    put("media", "num_media", Medium, media)

    scene.objects = None
    scene.num_objects = 0
    scene.planes = None
    scene.num_planes = 0
    num_textures = len(textures.widths)
    scene.num_textures = num_textures
    if num_textures:
        ptrs = (ctypes.POINTER(ctypes.c_ubyte) * num_textures)(
            *[ctypes.cast(p, ctypes.POINTER(ctypes.c_ubyte)) for p in textures.pixels])
        scene.texture_pixels = ptrs
        scene.texture_widths = (ctypes.c_int * num_textures)(*textures.widths)
        scene.texture_heights = (ctypes.c_int * num_textures)(*textures.heights)
        buffers.extend([ptrs, scene.texture_widths, scene.texture_heights]
                       + textures.pixels)
    else:
        scene.texture_pixels = None
        scene.texture_widths = None
        scene.texture_heights = None
    # csg_root = 0 по умолчанию (ctypes обнуляет поле) означал «дерево есть»,
    # и ядро шло по нулевому указателю. -1 — деревьев нет.
    scene.csg_root = -1
    scene.num_csg_nodes = 0
    scene.csg_nodes = None
    return scene


# ======================================================================
# Выходные пути
# ======================================================================

def _abspath(path):
    p = bpy.path.abspath(path) if path else bpy.path.abspath("//")
    return p or os.getcwd()


def _looks_like_dir(path) -> bool:
    return bool(path) and (path.endswith(("/", os.sep)) or os.path.isdir(_abspath(path)))


def output_file(path, name) -> str:
    """Путь-каталог → файл с именем name; путь-файл остаётся как есть."""
    p = _abspath(path)
    if not path or _looks_like_dir(path):
        p = os.path.join(p, name)
    d = os.path.dirname(p)
    if d:
        os.makedirs(d, exist_ok=True)
    return p


def output_dir(path) -> str:
    p = _abspath(path)
    if not _looks_like_dir(path) and os.path.splitext(p)[1]:
        p = os.path.dirname(p)
    os.makedirs(p, exist_ok=True)
    return p


def render_size(scene) -> tuple:
    """Разрешение кадра с учётом Resolution %."""
    pct = getattr(scene.render, "resolution_percentage", 100) / 100.0
    w = max(1, int(scene.render.resolution_x * pct))
    h = max(1, int(scene.render.resolution_y * pct))
    return w, h


def temp_output_path() -> str:
    """Временный PNG для пути RenderEngine (Blender забирает пиксели сам)."""
    import tempfile
    fd, path = tempfile.mkstemp(prefix="sheen_frame_", suffix=".png")
    os.close(fd)
    return path


# ======================================================================
# Высокоуровневый API (вызывается из операторов)
# ======================================================================

_renderer = None


def get_renderer():
    """Рендерер: нативный модуль sheen (Этап 2) или ctypes-фолбэк (Этап 1)."""
    global _renderer
    if _renderer is None:
        native = _NativeRenderer()
        if native.load():
            _renderer = native
        else:
            _renderer = SheenRenderer()
    return _renderer


def cleanup_renderer():
    global _renderer
    if _renderer:
        _renderer.cleanup()
        _renderer = None


def render(scene: SheenScene, width: int, height: int, aa_samples: int,
           output_path: str) -> bool:
    return get_renderer().render(scene, width, height, aa_samples, output_path)


def render_current(context=None, aa_samples=8, output_path="//sheen/", filename="sheen.png",
                   scene=None, depsgraph=None) -> bool:
    scn = scene if scene is not None else context.scene
    pct = getattr(scn.render, "resolution_percentage", 100) / 100.0
    w = max(1, int(scn.render.resolution_x * pct))
    h = max(1, int(scn.render.resolution_y * pct))
    sheen_scene = build_sheen_scene(scn, depsgraph)
    path = output_file(output_path, filename)
    return render(sheen_scene, w, h, aa_samples, path)


def render_preview(context, aa_samples=8, output_path="//sheen/") -> bool:
    """Быстрый превью: длинная сторона 512 px, пропорции кадра сохраняются."""
    scn = context.scene
    orig_x, orig_y = scn.render.resolution_x, scn.render.resolution_y
    orig_pct = scn.render.resolution_percentage
    scale = 512.0 / max(orig_x, orig_y, 1)
    scn.render.resolution_x = max(1, int(round(orig_x * scale)))
    scn.render.resolution_y = max(1, int(round(orig_y * scale)))
    scn.render.resolution_percentage = 100
    try:
        return render_current(context, aa_samples, output_path, "sheen_preview.png")
    finally:
        scn.render.resolution_x = orig_x
        scn.render.resolution_y = orig_y
        scn.render.resolution_percentage = orig_pct


def render_full(context, aa_samples=32, output_path="//sheen/") -> bool:
    """Полный рендер текущей сцены в полном разрешении."""
    return render_current(context, aa_samples, output_path, "sheen.png")


def render_animation(context, aa_samples=16, output_path="//sheen/",
                     frame_start=1, frame_end=250, frame_step=1) -> bool:
    """Последовательность кадров: для каждого кадра своя SheenScene."""
    directory = output_dir(output_path)
    success = True
    for frame in range(frame_start, frame_end + 1, frame_step):
        context.scene.frame_set(frame)
        frame_path = os.path.join(directory, f"frame_{frame:04d}.png")
        print(f"[Sheen] кадр {frame} -> {frame_path}")
        if not render_current(context, aa_samples, frame_path):
            success = False
    return success
