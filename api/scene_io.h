#pragma once
#include <string>
#include "scene.h"

// ---------------------------------------------------------
// API сцены: текстовый файл -> Scene.
// Формат описан в scenes/README.md; недоступное просто
// комментируется символом '#' и загрузчик это пропускает.
// ---------------------------------------------------------
struct SceneSettings {
    int width = 800;
    int height = 600;
    int aa = 4;
    int photons = 0;   // фотонов на источник (0 — проход выключен)
    float photon_cell = 0.25f;  // размер ячейки карты фотонов
    float caustics = 0.0f;      // вклад фотонной карты (0 — выключен)
    std::string output = "scene_output.png";
};

bool load_scene_file(const std::string& path, Scene& scene, SceneSettings& settings);
