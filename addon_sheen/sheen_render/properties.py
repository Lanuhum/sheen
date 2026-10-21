# Sheen Render — свойства сцены.
# Только то, что реально использует ядро: разрешение, AA, выходной путь.
# Caustics/volumetrics/denoiser отсутствуют в CUDA-ядре — не выдумываем.

import bpy
from bpy.props import BoolProperty, FloatProperty, IntProperty, PointerProperty, StringProperty, EnumProperty

sheen_scene_props = {
    "sheen_aa_samples": IntProperty(
        name="AA Samples",
        default=8,
        min=1,
        max=64,
        description="Anti-aliasing samples",
    ),
    "sheen_photons": IntProperty(
        name="Photons per Light",
        default=0,
        min=0,
        max=2000000,
        description="Фотонов на источник (0 — каустика выключена)",
    ),
    "sheen_photon_cell": FloatProperty(
        name="Photon Cell",
        default=0.25,
        min=0.01,
        max=5.0,
        description="Размер ячейки карты фотонов",
    ),
    "sheen_caustics": FloatProperty(
        name="Caustics",
        default=0.0,
        min=0.0,
        max=20.0,
        description="Вклад фотонной карты (0 — выключен)",
    ),
    "sheen_render_mode": EnumProperty(
        name="Render Mode",
        items=[
            ("PREVIEW", "Preview", "Quick 512px preview"),
            ("FULL", "Full", "Full resolution"),
            ("ANIMATION", "Animation", "Render sequence"),
        ],
        default="PREVIEW",
    ),
    "sheen_output_path": StringProperty(
        name="Output Path",
        default="//sheen/",
        subtype="DIR_PATH",
        description="Directory for output images (a full .png path is accepted too)",
    ),
    "sheen_frame_start": IntProperty(name="Start Frame", default=1, min=1, max=10000),
    "sheen_frame_end": IntProperty(name="End Frame", default=250, min=1, max=10000),
    "sheen_frame_step": IntProperty(name="Step", default=1, min=1, max=10),
}


def register_material_props():
    """Зеркальность материала: 0 — нет, 1 — зеркало."""
    bpy.types.Material.sheen_reflect = FloatProperty(
        name="Reflect",
        default=0.0,
        min=0.0,
        max=1.0,
        subtype="FACTOR",
        description="Доля зеркального отражения (глубина 1)",
    )
    """Карта свойств: R — пропускание, G — (IOR-1), B — шероховатость."""
    bpy.types.Material.sheen_mask = PointerProperty(
        name="Mask",
        type=bpy.types.Image,
        description="Карта свойств: R — пропускание, G — (IOR−1), B — шероховатость",
    )


def unregister_material_props():
    if hasattr(bpy.types.Material, "sheen_mask"):
        del bpy.types.Material.sheen_mask
    if hasattr(bpy.types.Material, "sheen_reflect"):
        del bpy.types.Material.sheen_reflect


def register_light_props():
    bpy.types.Light.sheen_gobo = PointerProperty(
        name="Gobo",
        type=bpy.types.Image,
        description="Текстура, проецируемая прожектором (gobo)",
    )
    """Видимый поток света у прожектора. По умолчанию 0 — выключен."""
    bpy.types.Light.sheen_beam = FloatProperty(
        name="Light Beam",
        default=0.0,
        min=0.0,
        max=1.0,
        subtype="FACTOR",
        description="Яркость видимого конуса света (0 — не рисовать)",
    )


def register_object_props():
    """Контейнер рассеивающей среды: объект-меш с sheen_media."""
    bpy.types.Object.sheen_media = BoolProperty(
        name="Scattering Medium",
        default=False,
        description="Объект задаёт объём рассеивающей среды (не рендерится)",
    )
    bpy.types.Object.sheen_media_scatter = FloatProperty(
        name="Scatter", default=0.07, min=0.0, max=10.0,
        description="Коэффициент рассеяния",
    )
    bpy.types.Object.sheen_media_extinction = FloatProperty(
        name="Extinction", default=0.01, min=0.0, max=10.0,
        description="Коэффициент ослабления",
    )
    bpy.types.Object.sheen_media_samples = IntProperty(
        name="Steps", default=16, min=1, max=64,
        description="Шагов вдоль луча (цена линейна)",
    )


def unregister_object_props():
    for name in ("sheen_media", "sheen_media_scatter",
                 "sheen_media_extinction", "sheen_media_samples"):
        if hasattr(bpy.types.Object, name):
            delattr(bpy.types.Object, name)


def unregister_light_props():
    for name in ("sheen_beam", "sheen_gobo"):
        if hasattr(bpy.types.Light, name):
            delattr(bpy.types.Light, name)


def register_scene_props():
    """Register Sheen properties on bpy.types.Scene"""
    for name, prop in sheen_scene_props.items():
        setattr(bpy.types.Scene, name, prop)


def unregister_scene_props():
    """Remove Sheen properties from bpy.types.Scene"""
    for name in sheen_scene_props.keys():
        if hasattr(bpy.types.Scene, name):
            delattr(bpy.types.Scene, name)


def register():
    register_scene_props()
    register_light_props()
    register_object_props()
    register_material_props()


def unregister():
    unregister_material_props()
    unregister_object_props()
    unregister_light_props()
    unregister_scene_props()