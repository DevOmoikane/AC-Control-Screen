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
LVGL_BUILD="$BUILD/lvgl"
APP_BUILD="$BUILD/preview"

LVGL="$ROOT/.pio/libdeps/cyd_remote/lvgl"
INC="$ROOT/include"
UI="$ROOT/src/ui"

if [ ! -d "$LVGL" ]; then
  echo "LVGL not found at $LVGL -- run: pio pkg install -e cyd_remote" >&2
  exit 2
fi

mkdir -p "$LVGL_BUILD" "$APP_BUILD"
# The LVGL build depends on lv_conf.h, so the stamp has to follow it.
STAMP="$LVGL_BUILD/lvgl.stamp"
if [ ! -f "$STAMP" ] || [ "$INC/lv_conf.h" -nt "$STAMP" ]; then
  echo "Building LVGL for the host (once, takes a minute)..."
  rm -f "$LVGL_BUILD"/*.o
  find "$LVGL/src" -name '*.c' > "$LVGL_BUILD/srcs.txt"
  ( cd "$LVGL_BUILD" && gcc -std=gnu99 -O0 -w -DLV_CONF_INCLUDE_SIMPLE \
      -I"$INC" -I"$LVGL" -c $(cat srcs.txt) )
  touch "$STAMP"
fi

cp "$HERE/render_preview.c" "$APP_BUILD/render_preview.c"
( cd "$APP_BUILD" && gcc -std=gnu99 -O0 -w -DLV_CONF_INCLUDE_SIMPLE \
    -I"$INC" -I"$LVGL" -I"$UI" \
    -c render_preview.c "$UI/screens.c" "$UI/styles.c" "$UI/images.c" \
        "$UI/actions.c" "$UI/ui.c" )
( cd "$APP_BUILD" && gcc *.o "$LVGL_BUILD"/*.o -o render_preview -lm )

( cd "$APP_BUILD" && ./render_preview preview.ppm )
python3 - "$APP_BUILD/preview.ppm" "$OUT" <<'PY'
import sys
from PIL import Image

image = Image.open(sys.argv[1])
image.resize((image.width * 2, image.height * 2), Image.NEAREST).save(sys.argv[2])
PY
echo "Wrote $OUT"
