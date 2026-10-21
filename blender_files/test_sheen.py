#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Sheen — дымовой тест на установленном аддоне.
#
#   cmake --build build && cmake --install build
#   blender -b --python blender_files/test_sheen.py
#
# Проверяет единственный реальный путь: движок SHEEN в Blender.
# Регистрация свойств, F12 (render.render), цвета кадра, анимация.

import math
import os
import sys

import bpy
from mathutils import Vector

OUT_DIR = "/tmp/sheen_smoke"
FAILURES = []


def check(ok, message):
    print(("PASS  " if ok else "FAIL  ") + message)
    if not ok:
        FAILURES.append(message)


def color_stats(path):
    """Доли красного/синего/светлого по пикселям записанного PNG."""
    image = bpy.data.images.load(path, check_existing=False)
    try:
        width, height = image.size
        pixels = list(image.pixels)
    finally:
        bpy.data.images.remove(image)

    red = blue = bright = 0
    for i in range(0, len(pixels), 4):
        r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]
        if r > g + 0.15 and r > 0.3:
            red += 1
        elif b > r + 0.12 and b > 0.25:
            blue += 1
        elif r > 0.75 and g > 0.75 and b > 0.75:
            bright += 1
    return width, height, red, blue, bright


def build_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.preferences.addon_enable(module="sheen_render")
    scene = bpy.context.scene

    red = bpy.data.materials.new("Red")
    red.diffuse_color = (0.85, 0.12, 0.1, 1.0)
    blue = bpy.data.materials.new("Blue")
    blue.diffuse_color = (0.15, 0.3, 0.85, 1.0)
    green = bpy.data.materials.new("Green")
    green.diffuse_color = (0.2, 0.7, 0.25, 1.0)

    bpy.ops.mesh.primitive_plane_add(size=12)
    bpy.context.object.data.materials.append(red)

    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.9, location=(-1.2, 0.0, 0.9))
    sphere = bpy.context.object
    sphere.data.materials.append(blue)
    for poly in sphere.data.polygons:
        poly.use_smooth = True

    bpy.ops.mesh.primitive_cube_add(size=1.2, location=(1.3, 0.2, 0.6))
    cube = bpy.context.object
    cube.data.materials.append(green)
    cube.rotation_euler = (0.0, math.radians(35.0), math.radians(25.0))

    bpy.ops.object.camera_add(location=(0.0, -6.0, 3.2))
    camera = bpy.context.object
    direction = Vector((0.0, 0.1, 0.6)) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera

    bpy.ops.object.light_add(type="SUN")
    sun = bpy.context.object
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(50.0), 0.0, math.radians(35.0))

    scene.render.resolution_x = 320
    scene.render.resolution_y = 240
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.sheen_aa_samples = 8
    return scene, sphere


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    scene, sphere = build_scene()

    check(hasattr(scene, "sheen_aa_samples"), "свойства аддона зарегистрированы")
    try:
        scene.render.engine = "SHEEN"
    except TypeError:
        pass
    check(scene.render.engine == "SHEEN", "движок SHEEN доступен в Render Properties")

    still = os.path.join(OUT_DIR, "still.png")
    scene.render.filepath = still
    bpy.ops.render.render(write_still=True)
    check(os.path.exists(still), "F12 записал файл")

    if os.path.exists(still):
        width, height, red, blue, bright = color_stats(still)
        check((width, height) == (320, 240), f"размер кадра {width}x{height}")
        check(red > 500, f"пол красный ({red} px)")
        check(blue > 500, f"сфера синяя ({blue} px)")
        check(bright > 500, f"небо светлое ({bright} px)")

    # Анимация: два кадра с разным положением сферы
    scene.frame_start, scene.frame_end = 1, 2
    scene.render.filepath = os.path.join(OUT_DIR, "anim_")
    for frame, x in ((1, -1.2), (2, 1.2)):
        scene.frame_set(frame)
        sphere.location = (x, 0.0, 0.9)
        sphere.keyframe_insert("location", frame=frame)
    bpy.ops.render.render(animation=True)

    frames = [os.path.join(OUT_DIR, f"anim_{i:04d}.png") for i in (1, 2)]
    check(all(os.path.exists(f) for f in frames), "анимация записала оба кадра")
    if all(os.path.exists(f) for f in frames):
        with open(frames[0], "rb") as f1, open(frames[1], "rb") as f2:
            check(f1.read() != f2.read(), "кадры анимации различаются")

    print()
    if FAILURES:
        print("ПРОВАЛ:", len(FAILURES), "проверок")
        sys.exit(1)
    print("ВСЁ ЗЕЛЁНОЕ")


main()
