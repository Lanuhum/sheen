#!/usr/bin/env bash
# Быстрая проверка без CUDA-toolkit: раскладка структур (sizes) + синтаксис всех заголовков.
# Полная сборка и запуск на GPU — только локально с nvcc: cmake -B build -DSHEEN_BACKEND=nvcc && cmake --build build
set -e
cd "$(dirname "$0")/.."
STUB=-Ishim/cuda_stub
g++ -std=c++17 -I. $STUB -Wno-unused-variable tests/sizes_main.cpp -o /tmp/sheen_sizes
/tmp/sheen_sizes | tail -1
for h in gpu_compat.h types.h intersect.h bvh.h scene.h render_kernel.h; do
  printf '#include "types.h"\n#include "%s"\n' "$h" > /tmp/sheen_chk.cpp
  g++ -std=c++17 -fsyntax-only -I. $STUB -Wno-unused-variable /tmp/sheen_chk.cpp
  echo "syntax OK: $h"
done
echo "host_check: ALL OK"
