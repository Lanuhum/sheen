# Sheen Render — панель управления (UI).
# Только реальные параметры ядра: режим, AA, выход, анимация.

import bpy

from . import engine


# ======================================================================
# Panel: Sheen Render (N-Panel)
# ======================================================================

class SHEEN_PT_RenderPanel(bpy.types.Panel):
    bl_label = "Sheen Render"
    bl_idname = "SHEEN_PT_render_panel"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Sheen"

    def draw(self, context):
        layout = self.layout
        scn = context.scene

        # --- Quality ---
        box = layout.box()
        box.label(text="Quality", icon="MODIFIER")
        box.column().prop(scn, "sheen_aa_samples")

        # --- Caustics ---
        box = layout.box()
        box.label(text="Каустика (фотонная карта)", icon="LIGHT_SUN")
        box.prop(scn, "sheen_photons")
        box.prop(scn, "sheen_photon_cell")
        box.prop(scn, "sheen_caustics")

        # --- Output ---
        box = layout.box()
        box.label(text="Output", icon="IMAGE_DATA")
        col = box.column()
        row = col.row(align=True)
        row.prop(scn.render, "resolution_x")
        row.prop(scn.render, "resolution_y")
        col.prop(scn.render, "resolution_percentage")
        col.prop(scn, "sheen_render_mode")
        col.prop(scn, "sheen_output_path")
        if scn.sheen_render_mode == "ANIMATION":
            col.prop(scn, "sheen_frame_start")
            col.prop(scn, "sheen_frame_end")
            col.prop(scn, "sheen_frame_step")

        layout.separator()

        # --- Actions ---
        row = layout.row()
        row.scale_y = 1.8
        row.operator("sheen.render_preview", text="Preview", icon="RENDER_STILL")

        row = layout.row()
        row.scale_y = 1.8
        row.operator("sheen.render_full", text="Full Render", icon="RENDER_STILL")

        if scn.sheen_render_mode == "ANIMATION":
            row = layout.row()
            row.scale_y = 1.8
            row.operator("sheen.render_animation", text="Render Animation",
                        icon="RENDER_ANIMATION")


# ======================================================================
# Registration
# ======================================================================

classes = (
    SHEEN_PT_RenderPanel,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)