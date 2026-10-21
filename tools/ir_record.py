#!/usr/bin/env python3
"""Record the frames the original AC remote sends.

Semi automated: name a button, press that button on the remote, and the
frame is written down. The script watches the IR node's serial log, so it
records exactly what the node sees.

Usage:
    tools/ir_record.py                       find the IR node by USB id
    tools/ir_record.py --port /dev/cu.usbmodem101
    tools/ir_record.py --out captures/remote.txt
    tools/ir_record.py --settle 2.0          seconds to wait for repeats

The recording is written after every capture, so nothing is lost if the
script is interrupted.
"""

import argparse
import os
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acports  # noqa: E402

CAPTURE_RE = re.compile(r"^\[ir rx\] capture (\d+): (\d+) edges")
DECODE_RE = re.compile(r"^\[ir rx\] decode: (.*)$")
RAW_HEADER = "[ir rx] raw (level:width us):"
EDGE_TOKEN = re.compile(r"^[HL]:(\d+)$")

try:
    import select

    HAVE_SELECT = True
except ImportError:  # not a POSIX system
    HAVE_SELECT = False


def open_serial(port, baud):
    """Open the serial port of the IR node."""
    try:
        import serial
    except ImportError:
        print(
            "pyserial is missing. Install it with:\n"
            "    python3 -m pip install pyserial\n"
            "or use tools/ir_record.sh, which finds an interpreter that has it.",
            file=sys.stderr,
        )
        sys.exit(1)
    try:
        return serial.Serial(port, baud, timeout=0.2)
    except Exception as error:
        print("cannot open %s: %s" % (port, error), file=sys.stderr)
        sys.exit(1)


class Capture(object):
    def __init__(self, number):
        self.number = number
        self.edges = []
        self.raw_lines = []
        self.decode = ""
        self.bytes = ""
        self.received_at = time.time()

    @property
    def bits(self):
        match = re.search(r"(\d+) bits", self.decode)
        return int(match.group(1)) if match else 0

    @property
    def header(self):
        match = re.search(r"header (\d+)/(\d+) us", self.decode)
        return "%s/%s" % (match.group(1), match.group(2)) if match else "-"

    def summary(self):
        if not self.bytes:
            return "no Midea header (%d pulses)" % len(self.edges)
        return "%s  (%d bits, header %s us)" % (self.bytes, self.bits, self.header)


def extract_bytes(decode_text):
    marker = "bytes "
    index = decode_text.find(marker)
    if index < 0:
        return ""
    return decode_text[index + len(marker):].strip()


class LogWatcher(object):
    """Turns the IR node's log into captures.

    The port is read by a thread of its own, so the log is complete and the
    arrival times are the real ones, not the times the script happened to
    look. Every line that arrives is written to the session log, so the
    whole session can be looked at afterwards, including the lines that
    have nothing to do with the infrared frames.
    """

    def __init__(self, stream, session_log=None):
        self.stream = stream
        self.session_log = session_log
        self.pending = []
        self.current = None
        self.in_raw = False
        self.buffer = ""
        self.last_line_at = time.time()
        self.last_status = ""
        self.lines = 0
        self.error = None
        self.lock = threading.Lock()
        self.running = False

    def start(self):
        self.running = True
        thread = threading.Thread(target=self._reader)
        thread.daemon = True
        thread.start()
        return thread

    def stop(self):
        self.running = False

    def _reader(self):
        while self.running:
            try:
                waiting = getattr(self.stream, "in_waiting", 0)
                data = self.stream.read(waiting if waiting else 1)
            except Exception as error:  # the device went away
                self.error = error
                break
            if not data:
                continue
            with self.lock:
                self._feed(data.decode("utf-8", "replace"))

    def _feed(self, text):
        self.buffer += text
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            self._handle_line(line)

    def _handle_line(self, line):
        line = line.rstrip("\r")
        self.last_line_at = time.time()
        self.lines += 1
        if self.session_log is not None:
            stamp = time.strftime("%H:%M:%S")
            self.session_log.write("%s %s\n" % (stamp, line))
            self.session_log.flush()
        if "ir interrupts" in line:
            # The status line of the IR node. It says how much the receiver
            # saw, which is what tells a broken receiver from a broken
            # script.
            self.last_status = line
        match = CAPTURE_RE.match(line)
        if match:
            if self.current is not None:
                # A new capture started while the previous one was still
                # being read. The previous one never finished, so it is kept
                # as it is: what did arrive is worth looking at, and the
                # stream must not be blocked by it.
                self.pending.append(self.current)
            self.current = Capture(int(match.group(1)))
            self.in_raw = False
            return
        if self.current is None:
            return
        match = DECODE_RE.match(line)
        if match:
            self.current.decode = match.group(1).strip()
            self.current.bytes = extract_bytes(self.current.decode)
            return
        if line.startswith(RAW_HEADER):
            self.in_raw = True
            return
        if not self.in_raw:
            return
        tokens = line[len("[ir rx]"):].split()
        if tokens and all(EDGE_TOKEN.match(token) for token in tokens):
            self.current.raw_lines.append(line)
            for token in tokens:
                self.current.edges.append(
                    (1 if token[0] == "H" else 0, int(EDGE_TOKEN.match(token).group(1)))
                )
            return
        # The raw block ended, so the capture is complete.
        self.in_raw = False
        self.current.received_at = time.time()
        self.pending.append(self.current)
        self.current = None

    def put_back(self, capture):
        """Hand a capture back, so it is the next one handed out."""
        with self.lock:
            self.pending.insert(0, capture)

    def poll(self):
        """The next complete capture, if there is one.

        Returns None when nothing new has come in. The port is read by the
        reader thread, so a press that arrives while the user is still
        typing is not missed.
        """
        with self.lock:
            if self.pending:
                return self.pending.pop(0)
        return None

    def partial(self):
        """The capture that is still being read, if there is one.

        It holds what has arrived so far, which is what to look at when a
        capture does not finish.
        """
        with self.lock:
            return self.current

    def next_capture(self, timeout):
        deadline = time.time() + timeout
        while True:
            capture = self.poll()
            if capture is not None:
                return capture
            if time.time() >= deadline:
                return None
            time.sleep(0.05)

def prompt(text):
    try:
        return input(text)
    except EOFError:
        return "q"


class KeyReader(object):
    """Reads a line from the terminal without waiting for ever.

    The terminal is left in its normal mode, so the prompt keeps working
    as usual. In that mode the terminal only hands over a line once it is
    complete, so this never blocks half a line.
    """

    def __enter__(self):
        self.enabled = sys.stdin.isatty()
        return self

    def __exit__(self, *args):
        pass

    def read(self, timeout):
        """The line that was typed, or None when there was none."""
        if not self.enabled or not HAVE_SELECT:
            time.sleep(timeout)
            return None
        try:
            ready, _, _ = select.select([sys.stdin], [], [], timeout)
        except (OSError, ValueError):
            return None
        if not ready:
            return None
        try:
            return sys.stdin.readline()
        except (OSError, ValueError):
            return None


def settle_captures(watcher, capture, quiet, limit):
    """Collect the repeats of one button press.

    The window is a quiet period: it restarts whenever a frame arrives, so
    every repeat of the press is collected and not only the first ones. The
    overall time is capped, so a chatty remote cannot hold the script up.

    A frame with other bytes belongs to another press and is handed back.
    Returns (repeats, other).
    """
    repeats = 1
    stop_at = time.time() + limit
    quiet_until = time.time() + quiet
    while time.time() < stop_at:
        extra = watcher.poll()
        if extra is None:
            if time.time() >= quiet_until:
                break
            time.sleep(0.02)
            continue
        quiet_until = time.time() + quiet
        if extra.bytes and extra.bytes == capture.bytes:
            repeats += 1
            continue
        return repeats, extra
    return repeats, None


def wait_for_capture(watcher, timeout, since=0.0):
    """Wait for a capture, with a way out.

    Returns (capture, cancelled, dropped). The capture is None when the wait
    ended without one. 'c' and Enter, or Ctrl+C, cancels the wait, and the
    data that did arrive is kept so it can be looked at.

    Frames that arrived before the prompt was shown are leftovers of an
    earlier press and are dropped. Frames that arrive later are never
    dropped, not even when they carry the same bytes as the capture that
    was recorded last: pressing the same button twice has to give two
    captures.
    """
    deadline = time.time() + timeout
    next_hint = time.time() + 10
    dropped = 0
    with KeyReader() as keys:
        while True:
            try:
                while True:
                    capture = watcher.poll()
                    if capture is None:
                        break
                    if capture.received_at <= since:
                        dropped += 1
                        continue  # leftover of an earlier press
                    return capture, False, dropped
                now = time.time()
                if now >= deadline:
                    return None, False, dropped
                if now >= next_hint:
                    next_hint = now + 10
                    print("  still waiting, %d s left (c and Enter, or Ctrl+C, to cancel)"
                          % int(deadline - now))
                line = keys.read(0.2)
                if line is not None:
                    typed = line.strip().lower()
                    if typed in ("c", "cancel", "q", "quit"):
                        return None, True, dropped
                    if typed:
                        print("  press c and Enter to cancel")
            except KeyboardInterrupt:
                # Ctrl+C arrives as a signal. It cancels this capture, not
                # the whole session.
                return None, True, dropped


MODES = ("cool", "auto", "heat", "dry", "fan")

# The fan of the remote has seven points plus automatic. The codec only
# knows auto, low, medium and high, so the numbered points are recorded as
# f1 to f7 and the report says what the codec does not know yet.
FAN_LEVELS = {
    "auto": "auto",
    "low": "low",
    "med": "med",
    "medium": "med",
    "high": "high",
    "max": "f7",
}
for _level in range(1, 8):
    FAN_LEVELS[str(_level)] = "f%d" % _level

# Buttons of the remote that send a frame of their own, with no state in it.
NAMED_FRAMES = (
    "m",
    "soft",
    "vane",
    "vaneangle",
    "selfclean",
    "swing",
    "silent",
    "timer",
    "up",
    "down",
    "mode",
    "power",
    "fan",
)

TEMPERATURE_RE = re.compile(r"^(\d{1,2})(?:\.(\d))?$")

MIN_TEMP_HALF = 34  # 17.0 C
MAX_TEMP_HALF = 60  # 30.0 C


def parse_temperature(token):
    """A temperature token to half degrees, or None when it is not one."""
    text = token.rstrip("°cC")
    match = TEMPERATURE_RE.match(text)
    if not match:
        return None
    half = match.group(2)
    if half is not None and half != "5":
        return None
    return int(match.group(1)) * 2 + (1 if half == "5" else 0)


class ExpectedState(object):
    """The state a capture is expected to carry.

    The remote sends complete packets, so the state is what names a capture.
    The state is kept between captures, which lets the user type only what
    changes ("22" after a temperature button press).
    """

    def __init__(self):
        self.mode = "cool"
        self.fan = "auto"
        self.temp_half = 48  # 24.0 C
        self.power = "on"
        self.named = False  # False until the user names a state at least once

    def temp_text(self):
        if self.temp_half % 2:
            return "%d.5" % (self.temp_half // 2)
        return "%d" % (self.temp_half // 2)

    def canonical(self):
        if self.power == "off":
            return "off"
        return "%s_%s_%s" % (self.mode, self.fan, self.temp_text())

    def describe(self):
        if self.power == "off":
            return "off"
        return "%s, fan %s, %s C" % (self.mode, self.fan, self.temp_text())

    def hint(self):
        return "  mode %-4s  fan %-4s  temp %-4s power %s" % (
            self.mode,
            self.fan,
            self.temp_text(),
            self.power,
        )


def tokens_of(text):
    return [token for token in re.split(r"[\s,]+", text) if token]


def parse_state(text, state):
    """Work out which state the user means.

    Returns (label, description). Raises ValueError with a message the user
    can act on when the text cannot be understood.
    """
    lowered = text.strip().lower()
    if not lowered:
        if not state.named:
            raise ValueError("name the state of the first capture")
        return state.canonical(), "kept " + state.describe()
    if lowered in ("none", "?"):
        return "none", "not a state frame"
    if lowered in ("off", "power off"):
        state.power = "off"
        state.named = True
        return state.canonical(), state.describe()
    if lowered in ("on", "power on"):
        state.power = "on"
        state.named = True
        return state.canonical(), state.describe()
    if lowered in ("led", "display", "led toggle"):
        return "led", "LED toggle frame"
    if lowered in ("turbo", "turbo toggle"):
        return "turbo", "Turbo toggle frame"
    if len(tokens_of(lowered)) == 1 and tokens_of(lowered)[0] in NAMED_FRAMES:
        name = tokens_of(lowered)[0]
        return name, "%s button" % name

    tokens = tokens_of(lowered)
    index = 0
    changed = False

    def value_of(name):
        nonlocal index
        if index >= len(tokens):
            raise ValueError("%s needs a value" % name)
        value = tokens[index]
        index += 1
        return value

    while index < len(tokens):
        token = tokens[index]
        index += 1
        if token in ("mode", "m"):
            value = value_of("mode")
            if value not in MODES:
                raise ValueError("unknown mode '%s'" % value)
            state.mode = value
            changed = True
        elif token in ("fan", "f"):
            value = value_of("fan")
            if value not in FAN_LEVELS:
                raise ValueError(
                    "unknown fan '%s' (auto, 1 to 7, low, med, high)" % value
                )
            state.fan = FAN_LEVELS[value]
            changed = True
        elif token in ("temp", "temperature", "t"):
            value = value_of("temperature")
            half = parse_temperature(value)
            if half is None:
                raise ValueError("'%s' is not a temperature" % value)
            state.temp_half = half
            changed = True
        elif token in ("power", "p"):
            value = value_of("power")
            if value not in ("on", "off"):
                raise ValueError("power is 'on' or 'off'")
            state.power = value
            changed = True
        elif token in MODES and token != "fan":
            # "auto" is both a mode and a fan. On its own it means the mode.
            state.mode = token
            changed = True
        elif token in FAN_LEVELS:
            state.fan = FAN_LEVELS[token]
            changed = True
        else:
            half = parse_temperature(token)
            if half is None:
                raise ValueError("do not understand '%s'" % token)
            state.temp_half = half
            changed = True

    if not changed:
        raise ValueError("nothing to change")
    if not MIN_TEMP_HALF <= state.temp_half <= MAX_TEMP_HALF:
        raise ValueError(
            "temperature %s C is out of range (17 to 30)" % state.temp_text()
        )
    state.named = True
    return state.canonical(), state.describe()


def resolve_port(explicit):
    if explicit:
        return explicit
    port = acports.resolve("ir_node")
    if not port:
        print("no IR node found. Connect it, or pass --port.", file=sys.stderr)
        for candidate in acports.list_ports():
            print("  %s  %s" % (candidate["port"], candidate["hwid"] or "?"), file=sys.stderr)
        sys.exit(1)
    return port["port"]


def default_output():
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return os.path.join("captures", "remote-%s.txt" % stamp)


def write_file(path, captures, header_lines):
    directory = os.path.dirname(os.path.abspath(path))
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w") as handle:
        handle.write("# AC remote captures\n")
        for line in header_lines:
            handle.write("# %s\n" % line)
        handle.write("#\n")
        handle.write("# format: <state> | <bytes> | <pulses> | <key>=<value>\n")
        for capture in captures:
            pulses = " ".join(
                "%s:%d" % ("H" if level else "L", width) for level, width in capture["edges"]
            )
            handle.write(
                "%s | %s | %s | capture=%d | repeats=%d | at=%s | note=%s"
                % (
                    capture["label"],
                    capture["bytes"],
                    pulses,
                    capture["number"],
                    capture["repeats"],
                    capture["timestamp"],
                    capture["note"],
                )
            )
            if capture.get("incomplete"):
                handle.write(" | incomplete=1 | pulses=%d" % len(capture["edges"]))
            handle.write("\n")


def header_for(port):
    return [
        "recorded %s" % time.strftime("%Y-%m-%dT%H:%M:%S"),
        "device %s" % port,
    ]


def build_entry(label, note, capture, repeats, incomplete=False):
    """One line of the capture file."""
    return {
        "label": label,
        "note": note,
        "bytes": capture.bytes,
        "edges": capture.edges,
        "number": capture.number,
        "repeats": repeats,
        "incomplete": incomplete,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="serial device of the IR node")
    parser.add_argument("--out", help="file to write the captures to")
    parser.add_argument(
        "--log",
        help="file to write the whole session to (default: next to the captures)",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--settle",
        type=float,
        default=1.5,
        help="seconds of quiet after the last repeat of a button press",
    )
    parser.add_argument(
        "--settle-max",
        type=float,
        default=10.0,
        help="seconds to wait for the repeats of one button press at most",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="seconds to wait for a button press before giving up",
    )
    args = parser.parse_args(argv)

    port = resolve_port(args.port)
    output = args.out or default_output()
    log_path = args.log or (os.path.splitext(output)[0] + "-session.log")
    print("IR node on %s" % port)
    print("captures go to %s" % output)
    print("session log goes to %s" % log_path)
    stream = open_serial(port, args.baud)
    try:
        session_log = open(log_path, "w")
    except OSError as error:
        print("cannot write the session log %s: %s" % (log_path, error), file=sys.stderr)
        session_log = None

    watcher = LogWatcher(stream, session_log)
    watcher.start()
    captures = []
    waiting = None  # a frame that arrived before its state was named
    last_bytes = ""  # the bytes of the capture that was recorded last
    state = ExpectedState()

    try:
        while True:
            if watcher.error is not None:
                # The device went away, most likely because it was reset or
                # the cable was moved.
                print("  the connection to the IR node was lost: %s" % watcher.error)
                print("  the session log keeps what came in until then")
                watcher.stop()
                break
            # Clear what the previous press left behind. Everything that
            # arrived before this prompt belongs to an earlier press, so the
            # next capture starts clean. What arrives later is never
            # dropped, so pressing the same button twice still gives two
            # captures.
            prompt_started_at = time.time()
            dropped = 0
            waiting = None
            while True:
                arrived = watcher.poll()
                if arrived is None:
                    break
                if arrived.received_at <= prompt_started_at:
                    dropped += 1
                    continue
                waiting = arrived
                break
            if dropped:
                print("dropped %d leftover frame(s) of the previous capture" % dropped)
            if waiting is not None:
                print("a frame is already waiting: %s" % waiting.summary())
            print("State for the next capture. Only type what changes, enter keeps everything.")
            print(state.hint())
            answer = prompt("capture> ")
            answer = answer.strip()
            if answer == "q":
                break
            if answer == "l":
                for index, capture in enumerate(captures, 1):
                    print("  %2d. %-16s %s" % (index, capture["label"], capture["bytes"] or "-"))
                continue
            if answer == "u":
                if captures:
                    removed = captures.pop()
                    print("removed %s" % removed["label"])
                    write_file(output, captures, header_for(port))
                else:
                    print("nothing to remove")
                continue
            try:
                label, description = parse_state(answer, state)
            except ValueError as error:
                print("  %s" % error)
                continue
            print("  state set to %s (%s)" % (label, description))
            entered_at = time.time()

            if waiting is not None:
                capture = waiting
                waiting = None
                print("  using the frame that was already waiting")
            else:
                capture = None
                cancelled = False
                dropped = 0
                try:
                    print("  waiting... press the button on the remote now")
                    capture, cancelled, dropped = wait_for_capture(
                        watcher, args.timeout, prompt_started_at
                    )
                except KeyboardInterrupt:
                    # Ctrl+C cancels this capture, not the whole session.
                    cancelled = True
                if dropped:
                    print("dropped %d leftover frame(s) of the previous capture" % dropped)
                if capture is None:
                    # Nothing complete came in. What did arrive is kept, so
                    # the packet can be looked at.
                    partial = watcher.partial()
                    if partial is None:
                        print("  nothing received%s" % (" (cancelled)" if cancelled else ""))
                        print("  the device last said something %.0f s ago"
                              % (time.time() - watcher.last_line_at))
                        if watcher.last_status:
                            print("  %s" % watcher.last_status)
                        continue
                    entry = build_entry(label, description, partial, repeats=0, incomplete=True)
                    captures.append(entry)
                    print(
                        "  %s, kept the %d pulse(s) that arrived: %s"
                        % ("cancelled" if cancelled else "timed out", len(partial.edges),
                           partial.summary())
                    )
                    write_file(output, captures, header_for(port))
                    continue
            repeats, other = settle_captures(watcher, capture, args.settle, args.settle_max)
            if other is not None:
                # Another press: it belongs to the next capture.
                watcher.put_back(other)
                waiting = None

            entry = build_entry(label, description, capture, repeats)
            captures.append(entry)
            if capture.received_at <= entered_at:
                print("  the frame arrived while you were naming the state")
            if last_bytes and capture.bytes and capture.bytes == last_bytes:
                print("  this is the same frame as the capture before it")
            last_bytes = capture.bytes
            print("  recorded %s: %s  [%d repeat(s)]" % (label, capture.summary(), repeats))
            write_file(output, captures, header_for(port))
    except KeyboardInterrupt:
        print("")
    finally:
        try:
            stream.close()
        except Exception:
            pass
        if session_log is not None:
            try:
                session_log.close()
            except Exception:
                pass

    if captures:
        write_file(output, captures, header_for(port))
    print("wrote %d capture(s) to %s" % (len(captures), output))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
