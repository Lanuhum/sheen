# Sheen Render — Blender 4.5 Addon
# Быстрый biased-рендерер на CUDA (GTX 1080 Ti / Pascal / CUDA 12.9)
# Стиль: Blender Internal (2.4x) — не физически корректный, а быстрый и художественный

bl_info = {
    "name": "Sheen Render",
    "author": "MultiTool",
    "version": (1, 0, 0),
    "blender": (4, 5, 0),
    "location": "3D View > N-Panel > Sheen Render",
    "description": "Быстрый biased-рендерер на CUDA. Нативный модуль (import sheen), бинарный контракт, без файлов-прокладок.",
    "category": "Render",
    "wiki_url": "",
    "tracker_url": "",
}

# Порядок: engine (без регистрации, только функции) → properties → operators → панель
from . import engine, properties, operators, render_panel, render_engine

def register():
    properties.register()
    operators.register()
    render_panel.register()
    render_engine.register()

def unregister():
    render_engine.unregister()
    render_panel.unregister()
    operators.unregister()
    properties.unregister()
