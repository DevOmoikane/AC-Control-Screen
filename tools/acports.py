#!/usr/bin/env python3
"""Resolve a board to its serial device by USB VID:PID.

The USB vendor and product ids are a property of the hardware, so the same
lookup works on Linux, macOS and Windows. The device *paths* are not stable
across operating systems (/dev/ttyACM0 vs /dev/cu.usbmodem101, COM3 vs
/dev/ttyUSB0), which is why platformio.ini never hardcodes one.

Usage:
    acports.py list                 list the connected serial devices
    acports.py resolve <board>      print the device path for a board
    acports.py pio                  print the path of the pio executable
    acports.py python               print a python interpreter that has pyserial
"""

import json
import os
import re
import shutil
import subprocess
import sys

# USB ids of the boards in this project.
#   ir_node    : ESP32-C3 Super Mini, native USB (USB Serial/JTAG)
#   cyd_remote : ESP32-2432S028, USB-UART bridge chip (varies by revision)
BOARD_HWIDS = {
    "ir_node": ["303A:1001"],
    "cyd_remote": ["1A86:7523", "10C4:EA60", "0403:6001"],
}

BOARD_DESCRIPTION = {
    "ir_node": "ESP32-C3 Super Mini (IR node, native USB)",
    "cyd_remote": "ESP32-2432S028 (CYD, USB-UART bridge)",
}

_VIDPID = re.compile(r"([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})")


def normalize_hwid(raw):
    """Turn pyserial's hwid string into "303A:1001", or "" if there is none."""
    if not raw:
        return ""
    match = _VIDPID.search(raw.replace("0x", ""))
    if not match:
        return ""
    return "%s:%s" % (match.group(1).upper(), match.group(2).upper())


def pio_executable():
    """Path of the pio executable, or None."""
    found = shutil.which("pio")
    if found:
        return found
    for candidate in (
        os.path.expanduser("~/.platformio/penv/bin/pio"),
        os.path.expanduser("~/.platformio/penv/bin/pio.exe"),
    ):
        if os.path.isfile(candidate):
            return candidate
    return None


def _ports_from_pio():
    """Ask PlatformIO for the serial devices."""
    pio = pio_executable()
    if not pio:
        return []
    try:
        result = subprocess.run(
            [pio, "device", "list", "--json-output"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=60,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    try:
        data = json.loads(result.stdout.decode("utf-8", "replace"))
    except ValueError:
        return []
    if not isinstance(data, list):
        return []
    ports = []
    for item in data:
        if not isinstance(item, dict):
            continue
        ports.append(
            {
                "port": item.get("port", ""),
                "description": item.get("description", ""),
                "hwid": normalize_hwid(item.get("hwid", "")),
            }
        )
    return ports


def _ports_from_pyserial():
    """Ask pyserial for the serial devices."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    ports = []
    for info in list_ports.comports():
        hwid = ""
        if info.vid is not None and info.pid is not None:
            hwid = "%04X:%04X" % (info.vid, info.pid)
        ports.append(
            {
                "port": info.device,
                "description": info.description or "",
                "hwid": hwid,
            }
        )
    return ports


def _ports_from_dev():
    """Last resort: list the device nodes, without USB ids."""
    ports = []
    for directory in ("/dev",):
        try:
            names = sorted(os.listdir(directory))
        except OSError:
            continue
        for name in names:
            if name.startswith("cu.") or name.startswith("ttyUSB") or name.startswith("ttyACM"):
                ports.append({"port": os.path.join(directory, name), "description": "", "hwid": ""})
    return ports


def list_ports():
    """All serial devices, with their USB VID:PID when it is known."""
    ports = _ports_from_pio()
    if not ports:
        ports = _ports_from_pyserial()
    if not ports:
        ports = _ports_from_dev()
    return ports


def resolve(board, ports=None):
    """The device of the given board, or None when it is not connected."""
    if board not in BOARD_HWIDS:
        raise SystemExit(
            "unknown board '%s' (known boards: %s)" % (board, ", ".join(sorted(BOARD_HWIDS)))
        )
    if ports is None:
        ports = list_ports()
    for wanted in BOARD_HWIDS[board]:
        for port in ports:
            if port["hwid"] == wanted:
                return port
    return None


def python_with_pyserial():
    """An interpreter that can import pyserial, or None."""
    candidates = []
    if os.environ.get("AC_PYTHON"):
        candidates.append(os.environ["AC_PYTHON"])
    candidates.append(sys.executable)
    for name in ("python3", "python"):
        found = shutil.which(name)
        if found:
            candidates.append(found)
    candidates.append(os.path.expanduser("~/.platformio/penv/bin/python"))
    seen = set()
    for candidate in candidates:
        if not candidate or candidate in seen:
            continue
        seen.add(candidate)
        try:
            result = subprocess.run(
                [candidate, "-c", "import serial"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=30,
            )
        except (OSError, subprocess.SubprocessError):
            continue
        if result.returncode == 0:
            return candidate
    return None


def _format_list(ports):
    if not ports:
        print("no serial devices found")
        return
    width = max(len(p["port"]) for p in ports)
    for port in ports:
        print(
            "%-*s  %-9s  %s"
            % (width, port["port"], port["hwid"] or "????:????", port["description"])
        )


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    command = argv[1]
    if command in ("list", "ports"):
        _format_list(list_ports())
        return 0
    if command == "resolve":
        if len(argv) < 3:
            print("usage: acports.py resolve <board>", file=sys.stderr)
            return 2
        board = argv[2]
        port = resolve(board)
        if not port:
            print(
                "no device found for '%s' (expected USB %s)"
                % (board, " or ".join(BOARD_HWIDS[board])),
                file=sys.stderr,
            )
            _format_list(list_ports())
            return 1
        print(port["port"])
        return 0
    if command == "pio":
        pio = pio_executable()
        if not pio:
            print("pio not found", file=sys.stderr)
            return 1
        print(pio)
        return 0
    if command == "python":
        interpreter = python_with_pyserial()
        if not interpreter:
            print("no python with pyserial found", file=sys.stderr)
            return 1
        print(interpreter)
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
