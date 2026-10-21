#!/usr/bin/env bash
# Record the frames of the original AC remote.
#
# Finds a python interpreter that has pyserial (the system python, or the one
# PlatformIO installed) and hands over to tools/ir_record.py.
#
#   tools/ir_record.sh
#   tools/ir_record.sh --port /dev/cu.usbmodem101
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${AC_PYTHON:-python3}"

INTERPRETER="$("$PYTHON" "$ROOT/tools/acports.py" python)" || {
    echo "error: no python with pyserial found." >&2
    echo "       install it with: python3 -m pip install pyserial" >&2
    exit 1
}

exec "$INTERPRETER" "$ROOT/tools/ir_record.py" "$@"
