#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Sheen C-API — ctypes-контракт, бит-в-бит совпадающий с types.h.
# Раскладка проверена: Object=48, Material=64, CameraData=64, Triangle=112,
# Plane=32, Light=32, SweepPoint=16, SweepStrand=16 (64-бит, выравнивание 16).

import ctypes


class Float3(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float), ("z", ctypes.c_float)]


class Float2(ctypes.Structure):
    _fields_ = [("u", ctypes.c_float), ("v", ctypes.c_float)]


class CameraData(ctypes.Structure):
    _fields_ = [
        ("position", Float3),
        ("right", Float3),
        ("up", Float3),
        ("forward", Float3),
        ("focal_length", ctypes.c_float),
        ("sensor_width", ctypes.c_float),
        ("shift_x", ctypes.c_float),
        ("shift_y", ctypes.c_float),
    ]


class Object(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("material_id", ctypes.c_int),
        ("param1", Float3),
        ("param2", ctypes.c_float),
        ("_pad0", ctypes.c_float),
        ("_pad1", ctypes.c_float),
        ("_pad2", ctypes.c_float),
        # C: alignas(16) — размер округляется до 48 (36 + 12 хвоста)
        ("_tail", ctypes.c_byte * 12),
    ]


class SweepStrand(ctypes.Structure):
    _fields_ = [
        ("point_offset", ctypes.c_int),
        ("point_count", ctypes.c_int),
        ("material_id", ctypes.c_int),
        ("_pad0", ctypes.c_int),
    ]


class SweepPoint(ctypes.Structure):
    _fields_ = [
        ("position", Float3),
        ("radius", ctypes.c_float),
    ]


class Triangle(ctypes.Structure):
    _fields_ = [
        ("v0", Float3), ("v1", Float3), ("v2", Float3),
        ("n0", Float3), ("n1", Float3), ("n2", Float3),
        ("uv0", Float2), ("uv1", Float2), ("uv2", Float2),
        ("material_id", ctypes.c_int),
        ("_pad0", ctypes.c_int),
        ("_pad1", ctypes.c_int),
        # C: alignas(16) — размер округляется до 112 (108 + 4 хвоста)
        ("_tail", ctypes.c_byte * 4),
    ]


class Plane(ctypes.Structure):
    _fields_ = [
        ("normal", Float3),
        ("d", ctypes.c_float),
        ("material_id", ctypes.c_int),
        ("_pad0", ctypes.c_float),
        ("_pad1", ctypes.c_float),
        ("_pad2", ctypes.c_float),
    ]


class Material(ctypes.Structure):
    _fields_ = [
        ("diffuse_color", Float3),
        ("specular", ctypes.c_float),
        ("shininess", ctypes.c_float),
        ("ambient", ctypes.c_float),
        ("texture_id", ctypes.c_int),
        ("_pad_tex", ctypes.c_int),
        ("anisotropy", ctypes.c_float),
        ("hair_shift", ctypes.c_float),
        ("hair_spec_r", ctypes.c_float),
        ("hair_exp_r", ctypes.c_float),
        ("hair_spec_t", ctypes.c_float),
        ("hair_exp_t", ctypes.c_float),
        ("hair_backlight", ctypes.c_float),
        ("reflect", ctypes.c_float),
        ("transmission", ctypes.c_float),
        ("ior", ctypes.c_float),
        ("emission_color", Float3),
        ("emission", ctypes.c_float),
        ("mask_id", ctypes.c_int),
        ("normal_id", ctypes.c_int),
        ("normal_strength", ctypes.c_float),
        # C: alignas(16) — размер 112 (100 + 12 хвоста)
        ("_tail", ctypes.c_byte * 12),
    ]


class Light(ctypes.Structure):
    _fields_ = [
        ("direction", Float3),
        ("position", Float3),
        ("u_axis", Float3),
        ("v_axis", Float3),
        ("color", Float3),
        ("intensity", ctypes.c_float),
        ("radius", ctypes.c_float),
        ("angle", ctypes.c_float),
        ("blend", ctypes.c_float),
        ("beam", ctypes.c_float),
        ("type", ctypes.c_int),
        ("flags", ctypes.c_int),
        ("texture_id", ctypes.c_int),
        ("_pad1", ctypes.c_int),
    ]


class Medium(ctypes.Structure):
    _fields_ = [
        ("center", Float3),
        ("half", Float3),
        ("sigma_s", Float3),
        ("sigma_t", ctypes.c_float),
        ("samples", ctypes.c_int),
        ("_pad0", ctypes.c_int),
        # C: alignas(16), 48 байт ровно
    ]


class HeightField(ctypes.Structure):
    _fields_ = [
        ("center", Float3),
        ("size_x", ctypes.c_float),
        ("size_z", ctypes.c_float),
        ("height", ctypes.c_float),
        ("freq", ctypes.c_float),
        ("octaves", ctypes.c_int),
        ("seed", ctypes.c_int),
        ("material_id", ctypes.c_int),
        ("image", ctypes.POINTER(ctypes.c_ubyte)),  # карта высот или None
        ("img_w", ctypes.c_int),
        ("img_h", ctypes.c_int),
        ("_pad0", ctypes.c_int),
        # C: alignas(16) — размер 64 (52 + 12 хвоста)
        ("_tail", ctypes.c_byte * 12),
    ]


class Superellipsoid(ctypes.Structure):
    _fields_ = [
        ("center", Float3),
        ("a", ctypes.c_float),
        ("b", ctypes.c_float),
        ("c", ctypes.c_float),
        ("n", ctypes.c_float),
        ("material_id", ctypes.c_int),
        ("_pad0", ctypes.c_int),
        ("_pad1", ctypes.c_int),
        # C: alignas(16) — размер 48 (40 + 8 хвоста)
        ("_tail", ctypes.c_byte * 8),
    ]


class CSGNode(ctypes.Structure):
    _fields_ = [
        ("op", ctypes.c_int),
        ("leaf_kind", ctypes.c_int),
        ("leaf_index", ctypes.c_int),
        ("left", ctypes.c_int),
        ("right", ctypes.c_int),
    ]


class LathePoint(ctypes.Structure):
    _fields_ = [
        ("y", ctypes.c_float),
        ("radius", ctypes.c_float),
        ("_pad0", ctypes.c_int),
        ("_pad1", ctypes.c_int),
    ]


class Lathe(ctypes.Structure):
    _fields_ = [
        ("center", Float3),
        ("point_offset", ctypes.c_int),
        ("point_count", ctypes.c_int),
        ("material_id", ctypes.c_int),
        ("_pad0", ctypes.c_int),
        # C: alignas(16) — размер 32 (28 + 4 хвоста)
        ("_tail", ctypes.c_byte * 4),
    ]


class PrismPoint(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("z", ctypes.c_float),
        ("_pad0", ctypes.c_int),
        ("_pad1", ctypes.c_int),
    ]


class Prism(ctypes.Structure):
    _fields_ = [
        ("center", Float3),
        ("y_min", ctypes.c_float),
        ("y_max", ctypes.c_float),
        ("sweep", ctypes.c_float),
        ("point_offset", ctypes.c_int),
        ("point_count", ctypes.c_int),
        ("material_id", ctypes.c_int),
        ("_pad0", ctypes.c_int),
        # C: alignas(16) — размер 48 (40 + 8 хвоста)
        ("_tail", ctypes.c_byte * 8),
    ]


class SheenScene(ctypes.Structure):
    _fields_ = [
        ("camera", CameraData),
        ("objects", ctypes.POINTER(Object)),
        ("num_objects", ctypes.c_int),
        ("sweep_points", ctypes.POINTER(SweepPoint)),
        ("num_sweep_points", ctypes.c_int),
        ("sweep_strands", ctypes.POINTER(SweepStrand)),
        ("num_sweep_strands", ctypes.c_int),
        ("triangles", ctypes.POINTER(Triangle)),
        ("num_triangles", ctypes.c_int),
        ("heightfields", ctypes.POINTER(HeightField)),
        ("num_heightfields", ctypes.c_int),
        ("superellipsoids", ctypes.POINTER(Superellipsoid)),
        ("num_superellipsoids", ctypes.c_int),
        ("lathe_points", ctypes.POINTER(LathePoint)),
        ("num_lathe_points", ctypes.c_int),
        ("lathes", ctypes.POINTER(Lathe)),
        ("num_lathes", ctypes.c_int),
        ("prism_points", ctypes.POINTER(PrismPoint)),
        ("num_prism_points", ctypes.c_int),
        ("prisms", ctypes.POINTER(Prism)),
        ("num_prisms", ctypes.c_int),
        ("csg_nodes", ctypes.POINTER(CSGNode)),
        ("csg_root", ctypes.c_int),
        ("num_csg_nodes", ctypes.c_int),
        ("planes", ctypes.POINTER(Plane)),
        ("num_planes", ctypes.c_int),
        ("materials", ctypes.POINTER(Material)),
        ("num_materials", ctypes.c_int),
        ("texture_pixels", ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))),
        ("texture_widths", ctypes.POINTER(ctypes.c_int)),
        ("texture_heights", ctypes.POINTER(ctypes.c_int)),
        ("num_textures", ctypes.c_int),
        ("lights", ctypes.POINTER(Light)),
        ("num_lights", ctypes.c_int),
        ("photons", ctypes.c_int),
        ("photon_cell", ctypes.c_float),
        ("caustics", ctypes.c_float),
        ("media", ctypes.POINTER(Medium)),
        ("num_media", ctypes.c_int),
    ]