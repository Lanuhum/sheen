# Sheen Render — операторы (превью / полный рендер / анимация).

import bpy

from . import engine


class SHEEN_OT_AddHeightField(bpy.types.Operator):
    """Добавить HeightField (утёс) из grayscale-изображения.

    Выбирает файл-картинку, грузит его как Image в Blender, создаёт
    grid + displace-модификатор (текстура — выбранное изображение) для
    превью во вьюпорте, и вешает custom-пропсы, по которым мост потом
    строит HF-из-карты для рендера. Одна картинка = и превью, и рендер.
    """
    bl_idname = "sheen.add_heightfield"
    bl_label = "Sheen Add HeightField"
    bl_description = "Add a HeightField from a grayscale image (grid + displace preview)"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.png;*.jpg;*.jpeg;*.tga;*.bmp", options={"HIDDEN"})
    size: bpy.props.FloatProperty(name="Size", default=4.0, min=0.1, description="Side of the HF tile (X/Z)")
    height: bpy.props.FloatProperty(name="Height", default=1.0, min=0.01, description="Max height of the terrain")
    subdivisions: bpy.props.IntProperty(name="Divisions", default=64, min=2, max=512, description="Grid subdivisions for viewport preview")
    strength: bpy.props.FloatProperty(name="Displace Strength", default=1.0, description="Displace modifier scale (strength*height)")

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        import os
        name = os.path.basename(self.filepath)
        img = bpy.data.images.load(self.filepath, check_existing=True)
        img.name = f"HF_{name}"

        bpy.ops.mesh.primitive_grid_add(
            size=self.size,
            x_subdivisions=self.subdivisions,
            y_subdivisions=self.subdivisions,
            location=context.scene.cursor.location,
        )
        obj = context.active_object
        obj.name = f"HeightField_{name}"

        # displace-модификатор: высота = texture draw · strength·height
        bpy.ops.object.modifier_add(type="DISPLACE")
        disp = obj.modifiers["Displace"]
        tex = bpy.data.textures.new(f"HF_tex_{name}", "IMAGE")
        # карта высот — данные, а не картинка для глаз: без sRGB-преобразования
        img.colorspace_settings.name = "Non-Color"
        tex.image = img
        disp.texture = tex
        # UV, а не LOCAL: иначе текстура повторяется по всему тайлу
        disp.texture_coords = "UV"
        disp.strength = self.strength * self.height
        disp.mid_level = 0.0
        disp.direction = "Z"

        # custom-пропсы для моста (одна истина → HF рендерит ту же картинку)
        obj["sheen_hf_image"] = img.name
        obj["sheen_hf_size"] = self.size
        obj["sheen_hf_height"] = self.height

        self.report({"INFO"}, f"Sheen HF: {name} {img.size[0]}x{img.size[1]}")
        return {"FINISHED"}


class SHEEN_OT_RenderPreview(bpy.types.Operator):
    bl_idname = "sheen.render_preview"
    bl_label = "Sheen Preview Render"
    bl_description = "Quick 512px preview using Sheen CUDA engine"

    def execute(self, context):
        scn = context.scene
        ok = engine.render_preview(
            context,
            aa_samples=scn.sheen_aa_samples,
            output_path=scn.sheen_output_path,
        )
        self.report({"INFO"} if ok else {"ERROR"},
                    "Sheen preview done" if ok else "Sheen preview failed")
        return {"FINISHED"} if ok else {"CANCELLED"}


class SHEEN_OT_RenderFull(bpy.types.Operator):
    bl_idname = "sheen.render_full"
    bl_label = "Sheen Full Render"
    bl_description = "Full resolution render using Sheen CUDA engine"

    def execute(self, context):
        scn = context.scene
        ok = engine.render_full(
            context,
            aa_samples=scn.sheen_aa_samples,
            output_path=scn.sheen_output_path,
        )
        self.report({"INFO"} if ok else {"ERROR"},
                    "Sheen render done" if ok else "Sheen render failed")
        return {"FINISHED"} if ok else {"CANCELLED"}


class SHEEN_OT_RenderAnimation(bpy.types.Operator):
    bl_idname = "sheen.render_animation"
    bl_label = "Sheen Animation Render"
    bl_description = "Render frame sequence using Sheen CUDA engine"

    def execute(self, context):
        scn = context.scene
        ok = engine.render_animation(
            context,
            aa_samples=scn.sheen_aa_samples,
            output_path=scn.sheen_output_path,
            frame_start=scn.sheen_frame_start,
            frame_end=scn.sheen_frame_end,
            frame_step=scn.sheen_frame_step,
        )
        self.report({"INFO"} if ok else {"ERROR"},
                    "Sheen animation done" if ok else "Sheen animation failed")
        return {"FINISHED"} if ok else {"CANCELLED"}


classes = (
    SHEEN_OT_AddHeightField,
    SHEEN_OT_RenderPreview,
    SHEEN_OT_RenderFull,
    SHEEN_OT_RenderAnimation,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)