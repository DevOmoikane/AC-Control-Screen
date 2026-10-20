#!/usr/bin/env bash
# Render the generated EEZ/LVGL UI on the host, without hardware.
#
# It compiles LVGL 9.4.0 and the generated sources in src/ui against a
# memory-backed display, then writes a PNG of what the panel would show.
# Handy for checking colours and layout after editing the .eez-project.
#
# Usage:
#   ./ac-control-cyberpunk/render-preview.sh [output.png]
#
# Requires: gcc, python3, and the PlatformIO LVGL package
# (.pio/libdeps/cyd_remote/lvgl). Run `pio pkg install -e cyd_remote` once.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${1:-$HERE/preview-render.png}"
BUILD="$ROOT/.pio/host-lvgl"

LVGL="$ROOT/.pio/libdeps/cyd_remote/lvgl"
INC="$ROOT/include"
UI="$ROOT/src/ui"

if [ ! -d "$LVGL" ]; then
  echo "LVGL not found at $LVGL -- run: pio pkg install -e cyd_remote" >&2
  exit 2
fi

mkdir -p "$BUILD"
if [ ! -f "$BUILD/lvgl.stamp" ]; then
  echo "Building LVGL for the host (once, takes a minute)..."
  find "$LVGL/src" -name '*.c' > "$BUILD/srcs.txt"
  ( cd "$BUILD" && gcc -std=gnu99 -O0 -w -DLV_CONF_INCLUDE_SIMPLE \
      -I"$INC" -I"$LVGL" -c $(cat srcs.txt) )
  touch "$BUILD/lvgl.stamp"
fi

cp "$HERE/render_preview.c" "$BUILD/render_preview.c"
( cd "$BUILD" && gcc -std=gnu99 -O0 -w -DLV_CONF_INCLUDE_SIMPLE \
    -I"$INC" -I"$LVGL" -I"$UI" \
    -c render_preview.c "$UI/screens.c" "$UI/styles.c" "$UI/images.c" \
        "$UI/actions.c" "$UI/ui.c" )
( cd "$BUILD" && gcc render_preview.o $(ls *.o | grep -vE 'render_preview|screens|styles|images|actions|ui\.o') \
    screens.o styles.o images.o actions.o ui.o -o render_preview -lm )

( cd "$BUILD" && ./render_preview "$OUT" )
echo "Wrote $OUT"
