#!/usr/bin/env bash
# Regenerate the LVGL sources from the EEZ Studio project without opening the
# EEZ Studio GUI.
#
# EEZ Studio is an Electron app; it supports a hidden-window build mode:
#
#   eez-studio --build-project <path-to.eez-project>
#
# The project's build destination is ../src/ui, so the generated files land
# straight in the PlatformIO source tree.
#
# Usage:
#   ./build-eez.sh /path/to/eez-studio        # an AppImage, a .deb's binary, ...
#   EEZ_STUDIO=/path/to/eez-studio ./build-eez.sh
#
# Notes:
#  * EEZ Studio needs a display. A headless X server (Xvfb) or a real
#    session works; --ozone-platform=headless crashes the GPU process.
#  * Any build error is reported by EEZ Studio on stdout and makes it exit
#    non-zero.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT="$HERE/AC_Control_Cyberpunk.eez-project"

EEZ_STUDIO="${1:-${EEZ_STUDIO:-}}"
if [ -z "$EEZ_STUDIO" ]; then
  echo "usage: $0 <path-to-eez-studio>" >&2
  exit 2
fi
if [ ! -e "$EEZ_STUDIO" ]; then
  echo "eez-studio not found: $EEZ_STUDIO" >&2
  exit 2
fi

case "$EEZ_STUDIO" in
  *.AppImage)
    chmod +x "$EEZ_STUDIO"
    "$EEZ_STUDIO" --appimage-extract >/dev/null 2>&1 || true
    EEZ_STUDIO="$HERE/squashfs-root/EEZ Studio"
    export APPDIR="$HERE/squashfs-root"
    export PATH="$APPDIR:$PATH"
    export LD_LIBRARY_PATH="$APPDIR/usr/lib:${LD_LIBRARY_PATH:-}"
    ;;
esac

echo "Building $PROJECT ..."
"$EEZ_STUDIO" --build-project "$PROJECT" \
  --no-sandbox \
  --disable-dev-shm-usage \
  --enable-logging=stderr 2>&1 |
  grep -vE "ERROR:|WARNING:|INFO:|VERBOSE" || true

echo "Done. Generated files are in $HERE/../src/ui"
