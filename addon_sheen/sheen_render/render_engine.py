# Sheen Render — движок Blender (RenderEngine) и панель в Render Properties.
#
# Ядро пишет PNG, Blender ждёт пиксели: кадр рендерится во временный файл,
# читается обратно как изображение и отдаётся в результат рендера. Дальше
# работают штатные механизмы Blender: F12, Render Animation, Output settings,
# цветокоррекция и сохранение в любой формат.

import os

import bpy

from . import engine

ENGINE_ID = "SHEEN"

# Стандартные панели Blender (Light, Material, Camera, Output) рисуются только
# для движков из их COMPAT_ENGINES. Без этой регистрации при выбранном Sheen
# они пустые, и свет приходится настраивать, переключаясь на EEVEE.
# Панели самих Cycles/EEVEE не трогаем: их настройки к Sheen не относятся.
_compat_panels = []

# В Blender 4.5 часть нужных панелей называется с префиксом EEVEE, хотя
# рисует обычные данные: свойства лампы и слоты материала (добавить/удалить).
# Их включаем поимённо — остальные EEVEE/Cycles-панели к Sheen не относятся.
_EXTRA_PANELS = {
    "DATA_PT_EEVEE_light",               # цвет, мощность, радиус/размер лампы
    "EEVEE_MATERIAL_PT_context_material",  # слоты материала + Add/Remove
}


def _enable_compat_panels():
    del _compat_panels[:]
    for name in dir(bpy.types):
        cls = getattr(bpy.types, name)
        if not isinstance(cls, type) or not issubclass(cls, bpy.types.Panel):
            continue
        idname = (getattr(cls, "bl_idname", "") or name).upper()
        if ("CYCLES" in idname or "EEVEE" in idname) and name not in _EXTRA_PANELS:
            continue
        engines = getattr(cls, "COMPAT_ENGINES", None)
        if engines is None or ENGINE_ID in engines:
            continue
        engines.add(ENGINE_ID)
        _compat_panels.append(cls)


def _disable_compat_panels():
    for cls in _compat_panels:
        engines = getattr(cls, "COMPAT_ENGINES", None)
        if engines is not None:
            engines.discard(ENGINE_ID)
    del _compat_panels[:]


class SHEEN_RENDER_ENGINE(bpy.types.RenderEngine):
    bl_idname = "SHEEN"
    bl_label = "Sheen (CUDA)"
    bl_use_preview = False
    bl_use_postprocess = False
    # Вьюпорт рисует EEVEE: своего view_draw у нас нет, иначе Rendered пустой.
    bl_use_eevee_viewport = True
    # Материалы — стандартные ноды Blender (Principled), не собственные.
    bl_use_shading_nodes_custom = False

    def render(self, depsgraph):
        scene = depsgraph.scene
        width, height = engine.render_size(scene)
        aa_samples = getattr(scene, "sheen_aa_samples", 8)

        # Ядро отдаёт готовые display-referred цвета (sRGB), и Blender отдаёт
        # пиксели PNG ровно в этих значениях. Любая трансформация вида (AgX,
        # Standard) применила бы к ним вторую — поэтому Raw.
        if scene.view_settings.view_transform != "Raw":
            scene.view_settings.view_transform = "Raw"
            print("[Sheen] View Transform переключён на Raw: цвета движка идут как есть")

        path = engine.temp_output_path()

        ok = False
        try:
            sheen_scene = engine.build_sheen_scene(scene, depsgraph)
            ok = engine.render(sheen_scene, width, height, aa_samples, path)
            if not ok:
                print("[Sheen] рендер не удался — отдаю чёрный кадр")
        finally:
            pixels = [0.0] * (width * height * 4)
            if ok and os.path.exists(path):
                image = bpy.data.images.load(path, check_existing=False)
                try:
                    image.colorspace_settings.name = "sRGB"
                    loaded = list(image.pixels)
                    if len(loaded) == len(pixels):
                        pixels = loaded
                finally:
                    bpy.data.images.remove(image)
                    try:
                        os.remove(path)
                    except OSError:
                        pass

            result = self.begin_result(0, 0, width, height)
            result.layers[0].passes["Combined"].rect.foreach_set(pixels)
            self.end_result(result)


class SHEEN_PT_engine_settings(bpy.types.Panel):
    bl_label = "Sheen Render"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"
    COMPAT_ENGINES = {"SHEEN"}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES

    def draw(self, context):
        layout = self.layout
        scene = context.scene

        col = layout.column()
        col.prop(scene, "sheen_aa_samples")

        width, height = engine.render_size(scene)
        box = layout.box()
        box.label(text=f"Кадр: {width} × {height}", icon="IMAGE_DATA")
        box.label(text="Сохранение и палитра — настройки Output/Color Management",
                  icon="INFO")

        cbox = layout.box()
        cbox.label(text="Каустика (фотонная карта)", icon="LIGHT")
        cbox.prop(scene, "sheen_photons")
        cbox.prop(scene, "sheen_photon_cell")
        cbox.prop(scene, "sheen_caustics")

        layout.separator()
        col = layout.column()
        col.scale_y = 1.6
        col.operator("render.render", text="Render", icon="RENDER_STILL")


class SHEEN_PT_light_settings(bpy.types.Panel):
    bl_label = "Sheen Light"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "data"
    COMPAT_ENGINES = {"SHEEN"}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES and context.light is not None

    def draw(self, context):
        col = self.layout.column()
        col.prop(context.light, "sheen_media")
        if context.light.type == "SPOT":
            col.prop(context.light, "sheen_beam")
            col.prop(context.light, "sheen_gobo")


class SHEEN_PT_material(bpy.types.Panel):
    bl_label = "Sheen Material"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "material"
    COMPAT_ENGINES = {"SHEEN"}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES and context.material is not None

    def draw(self, context):
        col = self.layout.column()
        col.prop(context.material, "sheen_reflect")
        col.prop(context.material, "sheen_mask")


class SHEEN_PT_object_medium(bpy.types.Panel):
    bl_label = "Sheen Medium"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "data"
    COMPAT_ENGINES = {"SHEEN"}

    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES
                and context.object is not None
                and context.object.type == "MESH")

    def draw(self, context):
        col = self.layout.column()
        col.prop(context.object, "sheen_media")
        if context.object.sheen_media:
            col.prop(context.object, "sheen_media_scatter")
            col.prop(context.object, "sheen_media_extinction")
            col.prop(context.object, "sheen_media_samples")


classes = (
    SHEEN_RENDER_ENGINE,
    SHEEN_PT_engine_settings,
    SHEEN_PT_light_settings,
    SHEEN_PT_object_medium,
    SHEEN_PT_material,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    _enable_compat_panels()


def unregister():
    _disable_compat_panels()
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
