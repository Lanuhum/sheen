cmake_minimum_required(VERSION 3.25)

set(SHEEN_BACKEND "clang" CACHE STRING "CUDA compiler backend: clang or nvcc")
set(SHEEN_ARCH 61 CACHE STRING "CUDA compute capability without the dot")

if(SHEEN_BACKEND STREQUAL "nvcc")
    set(SHEEN_CUDA_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/shim -Wno-deprecated-gpu-targets")
else()
    set(CMAKE_CUDA_COMPILER "/usr/bin/clang++" CACHE FILEPATH "clang++ cannot use the system GCC 15 headers through nvcc" FORCE)
    set(SHEEN_CUDA_FLAGS "--cuda-path=/usr --no-offload-new-driver -Wno-unknown-cuda-version")
endif()

set(CMAKE_CUDA_FLAGS "${SHEEN_CUDA_FLAGS}")

project(sheen LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES ${SHEEN_ARCH})

set(SHEEN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Project headers: types.h, intersect.h, render_kernel.h, bvh.h, gpu_compat.h")

find_library(SHEEN_CUDART cudart PATHS /usr/lib64 REQUIRED)

foreach(tgt smoke hostmix)
    add_executable(${tgt} src/${tgt}.cu)
    target_link_libraries(${tgt} PRIVATE "${SHEEN_CUDART}")
endforeach()

add_executable(sizes tests/sizes_main.cpp)
set_source_files_properties(tests/sizes_main.cpp PROPERTIES LANGUAGE CUDA)
target_include_directories(sizes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sizes PRIVATE "${SHEEN_CUDART}")

add_executable(sphere_test tests/sphere_test_main.cpp render_kernel.cu)
set_source_files_properties(tests/sphere_test_main.cpp PROPERTIES LANGUAGE CUDA)
target_include_directories(sphere_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(sphere_test PRIVATE "${SHEEN_CUDART}")
