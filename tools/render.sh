#!/bin/bash
# Sheen: собрать (если нужно), отрисовать сцену и открыть картинку.
set -u
cd "$(dirname "$(readlink -f "$0")")/.." || exit 1

SCENE="${1:-scenes/demo.sheen}"
LOG=/tmp/sheen_render.log

if ! cmake --build build --target scene_render -j"$(nproc)" >"$LOG" 2>&1; then
    notify-send "Sheen: сборка не удалась" "$(tail -n 3 "$LOG")" 2>/dev/null
    echo "build failed, see $LOG"; exit 1
fi

if ! ./build/scene_render "$SCENE" >>"$LOG" 2>&1; then
    notify-send "Sheen: рендер не удался" "$(tail -n 3 "$LOG")" 2>/dev/null
    echo "render failed, see $LOG"; exit 1
fi

OUT=$(grep -oP 'saved: \K.*' "$LOG" | tail -1)
MS=$(grep -oP 'render: \K[0-9.]+' "$LOG" | tail -1)
[ -n "${OUT:-}" ] && xdg-open "$OUT" >/dev/null 2>&1 &
notify-send "Sheen: кадр готов" "${MS} мс, ${OUT}" 2>/dev/null
echo "ok: $OUT ($MS ms)"
