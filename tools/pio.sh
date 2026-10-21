#!/usr/bin/env bash
# Flash and monitor the boards of this project without hardcoding a serial
# device path. The device is found by USB VID:PID, which is the same on
# Linux, macOS and Windows.
#
#   tools/pio.sh ports                 list the connected serial devices
#   tools/pio.sh upload ir_node        build and flash the IR node
#   tools/pio.sh monitor ir_node       open the serial monitor
#   tools/pio.sh run cyd_remote        build without flashing
#
# Override the device with --port, for example:
#
#   tools/pio.sh upload ir_node --port /dev/cu.usbmodem101
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${AC_PYTHON:-python3}"
BOARD=""
PORT=""
ACTION=""

usage() {
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --port)
            PORT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            ACTION="$ACTION $1"
            shift
            ;;
    esac
done

set -- $ACTION
ACTION="${1:-}"
BOARD="${2:-}"

case "$ACTION" in
    ports)
        exec "$PYTHON" "$ROOT/tools/acports.py" list
        ;;
    upload|monitor|run)
        ;;
    *)
        usage
        exit 2
        ;;
esac

if [ -z "$BOARD" ]; then
    echo "error: no environment given (ir_node or cyd_remote)" >&2
    exit 2
fi

if [ -z "$PORT" ]; then
    if ! PORT="$("$PYTHON" "$ROOT/tools/acports.py" resolve "$BOARD")"; then
        echo "error: could not find the $BOARD board" >&2
        exit 1
    fi
fi

PIO="$("$PYTHON" "$ROOT/tools/acports.py" pio)" || {
    echo "error: pio not found" >&2
    exit 1
}

echo "using $BOARD on $PORT"

case "$ACTION" in
    upload)
        exec "$PIO" run -e "$BOARD" -t upload --upload-port "$PORT"
        ;;
    run)
        exec "$PIO" run -e "$BOARD"
        ;;
    monitor)
        exec "$PIO" device monitor --port "$PORT" --baud 115200
        ;;
esac
