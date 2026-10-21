cmake_minimum_required(VERSION 3.25)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- CUDA 12.9 + Pascal (GTX 1080 Ti) ---
set(SHEEN_CUDA_VERSION "12.9" CACHE STRING "CUDA toolkit version")
set(SHEEN_ARCH 61 CACHE STRING "CUDA compute capability (Pascal = 61)")

# Ищем CUDA 12.9 (Mageia 10 может ставить в /usr/local/cuda-12.9 или /opt/cuda)
find_path(CUDA12_ROOT cuda.h
    PATHS /usr /usr/local/cuda-12.9 /opt/cuda-12.9 /usr/local/cuda /opt/cuda
    NO_DEFAULT_PATH
)

# CUDA 12.9 на Mageia 10: nvcc в /usr/bin, заголовки в /usr/include
# Проверяем, что nvcc существует
if(NOT EXISTS "${CUDA12_ROOT}/bin/nvcc")
    # Ищем nvcc в PATH
    find_program(NVCC_EXECUTABLE nvcc)
    if(NVCC_EXECUTABLE)
        get_filename_component(CUDA12_BIN_DIR "${NVCC_EXECUTABLE}" DIRECTORY)
        get_filename_component(CUDA12_ROOT "${CUDA12_BIN_DIR}/.." ABSOLUTE)
        message(STATUS "Found nvcc at ${NVCC_EXECUTABLE}, CUDA12_ROOT set to ${CUDA12_ROOT}")
    endif()
endif()
if(NOT CUDA12_ROOT)
    message(WARNING "CUDA 12.9 not found — trying default /usr/local/cuda")
    set(CUDA12_ROOT "/usr/local/cuda")
endif()

# Use CUDACXX from environment if set, otherwise default to nvcc
# (путь к nvcc; реальный компилятор может быть заменён ниже на clang)
if(NOT CUDACXX)
    set(CMAKE_CUDA_COMPILER "${CUDA12_ROOT}/bin/nvcc")
else()
    set(CMAKE_CUDA_COMPILER "${CUDACXX}")
endif()

if(CMAKE_CUDA_COMPILER MATCHES "clang")
    set(SHEEN_USE_CLANG_CUDA TRUE)
else()
    set(SHEEN_USE_CLANG_CUDA FALSE)
endif()
set(CMAKE_CUDA_ARCHITECTURES ${SHEEN_ARCH})

# Флаги для Pascal + CUDA 12.9
# GTX 1080 Ti = Pascal (sm_61), CUDA 12.9 требует compute_61 или sm_61
# Архитектура задаётся CMAKE_CUDA_ARCHITECTURES (выше), поэтому
# -arch/-gencode здесь не дублируем — иначе nvcc получает их дважды.
set(SHEEN_CUDA_FLAGS
    -D_FORCE_INLINES
    --cudart=shared
    -Wno-deprecated-gpu-targets
)

if(NOT CUDACXX AND NOT SHEEN_USE_CLANG_CUDA)
    # nvcc 12.9 не умеет GCC 15 (Mageia 10): падает на libstdc++/type_traits.
    # clang 20 имеет нативную поддержку CUDA и работает с новым libstdc++.
    find_program(CLANG_CUDA_CANDIDATE clang++)
    file(GLOB SHEEN_LIBDEVICE_CANDIDATES
        "${CUDA12_ROOT}/lib*/nvvm/libdevice/libdevice*.bc"
        "${CUDA12_ROOT}/lib/libdevice.bc")
    if(CLANG_CUDA_CANDIDATE AND SHEEN_LIBDEVICE_CANDIDATES)
        set(CMAKE_CUDA_COMPILER "${CLANG_CUDA_CANDIDATE}")
        set(SHEEN_USE_CLANG_CUDA TRUE)
        message(STATUS "nvcc несовместим с GCC 15 — беру clang++ как CUDA-компилятор")
    endif()
endif()
if(SHEEN_USE_CLANG_CUDA)
    # clang++ + CUDA 12.9: nvcc-флаги, которые clang не понимает, убираем
    list(REMOVE_ITEM SHEEN_CUDA_FLAGS --cudart=shared)
    list(APPEND SHEEN_CUDA_FLAGS
        --cuda-path=${CUDA12_ROOT}
        -Wno-unknown-cuda-version
    )
endif()

# CMAKE_CUDA_FLAGS передаётся cmake в тест идентификации компилятора ОДНИМ
# токеном (список с ';' склеивается), поэтому здесь — только один флаг.
# clang обязан линковать старым драйвером: в Mageia нет clang-linker-wrapper,
# без него новый offload-драйвер падает на этапе линковки.
if(SHEEN_USE_CLANG_CUDA)
    set(CMAKE_CUDA_FLAGS "--no-offload-new-driver")
endif()

project(sheen LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(SHEEN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Project headers: types.h, intersect.h, render_kernel.h, bvh.h, gpu_compat.h")

# CUDA runtime
find_library(SHEEN_CUDART cudart
    PATHS "${CUDA12_ROOT}/lib64" "${CUDA12_ROOT}/lib" /usr/lib64
    REQUIRED
)

# --- Targets ---
foreach(tgt smoke hostmix)
    add_executable(${tgt} src/${tgt}.cu)
    target_compile_options(${tgt} PRIVATE ${SHEEN_CUDA_FLAGS})
    target_link_libraries(${tgt} PRIVATE "${SHEEN_CUDART}")
endforeach()

add_executable(sizes tests/sizes_main.cpp)
set_source_files_properties(tests/sizes_main.cpp PROPERTIES LANGUAGE CUDA)
target_compile_options(sizes PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(sizes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sizes PRIVATE "${SHEEN_CUDART}")

add_executable(sphere_test tests/sphere_test_main.cpp render_kernel.cu)
set_source_files_properties(tests/sphere_test_main.cpp PROPERTIES LANGUAGE CUDA)
target_compile_options(sphere_test PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(sphere_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(sphere_test PRIVATE "${SHEEN_CUDART}")

add_executable(scene_render tools/render_scene.cu api/scene_io.cpp render_kernel.cu)
set_source_files_properties(tools/render_scene.cu api/scene_io.cpp PROPERTIES LANGUAGE CUDA)
target_compile_options(scene_render PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(scene_render PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(scene_render PRIVATE "${SHEEN_CUDART}")

add_executable(mesh_test tests/mesh_test_main.cpp render_kernel.cu)
set_source_files_properties(tests/mesh_test_main.cpp PROPERTIES LANGUAGE CUDA)
target_compile_options(mesh_test PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(mesh_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(mesh_test PRIVATE "${SHEEN_CUDART}")

add_executable(bench tools/bench.cu)
set_source_files_properties(tools/bench.cu PROPERTIES LANGUAGE CUDA)
target_compile_options(bench PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bench PRIVATE "${SHEEN_CUDART}")

# --- C-API: общая библиотека для аддона Blender ---
add_library(sheen_c_api SHARED api/sheen_c_api.cu render_kernel.cu)
target_compile_options(sheen_c_api PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(sheen_c_api PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(sheen_c_api PRIVATE "${SHEEN_CUDART}")

# --- Проверка бинарного контракта C-API ---
add_executable(c_api_test tools/c_api_test.cu api/scene_io.cpp)
set_source_files_properties(tools/c_api_test.cu api/scene_io.cpp PROPERTIES LANGUAGE CUDA)
target_compile_options(c_api_test PRIVATE ${SHEEN_CUDA_FLAGS})
target_include_directories(c_api_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(c_api_test PRIVATE "${SHEEN_CUDART}" sheen_c_api)

# --- Нативный Python-модуль для Blender (import sheen) ---
# Раньше собирался руками, поэтому аддон искал .so по каталогам сборки.
# Теперь это обычная цель CMake: libsheen_c_api.so + sheen.so кладутся рядом
# с engine.py, а install раскладывает их в аддон Blender.
#
# Python должен совпадать с тем, что встроен в Blender (сейчас 3.13):
# модуль грузится внутрь Blender, и чужой ABI там не заведётся.
find_package(Python3 REQUIRED COMPONENTS Development)
add_library(sheen MODULE api/sheen_py_module.cpp)
set_target_properties(sheen PROPERTIES PREFIX "" OUTPUT_NAME "sheen")
target_include_directories(sheen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sheen PRIVATE sheen_c_api Python3::Python)
set_target_properties(sheen PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN")

# --- Установка ---
# Аддон ставится одной командой: cmake --install build
set(SHEEN_BLENDER_VERSION "4.5" CACHE STRING "Blender version for addon install")
if(NOT SHEEN_ADDONS_DIR)
    set(SHEEN_ADDONS_DIR "$ENV{HOME}/.config/blender/${SHEEN_BLENDER_VERSION}/scripts/addons"
        CACHE PATH "Blender addons directory")
endif()
set(SHEEN_ADDON_DIR "${SHEEN_ADDONS_DIR}/sheen_render")

install(TARGETS sheen_c_api sheen LIBRARY DESTINATION "${SHEEN_ADDON_DIR}")
install(DIRECTORY addon_sheen/sheen_render/
        DESTINATION "${SHEEN_ADDON_DIR}"
        FILES_MATCHING PATTERN "*.py")

install(TARGETS scene_render sphere_test mesh_test bench
    RUNTIME DESTINATION bin
)
install(TARGETS sheen_c_api
    LIBRARY DESTINATION lib
)

message(STATUS "Sheen — CUDA ${SHEEN_CUDA_VERSION}, arch sm_${SHEEN_ARCH} (Pascal/GTX 1080 Ti)")
message(STATUS "CUDA compiler: ${CMAKE_CUDA_COMPILER}")
message(STATUS "Addon install dir: ${SHEEN_ADDON_DIR}")

