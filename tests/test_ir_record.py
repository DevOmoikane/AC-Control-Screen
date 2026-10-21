#!/usr/bin/env python3
"""Host test for the recorder's log parsing.

Feeds the IR node's log through the parser and checks what it makes of it.
No hardware and no pyserial needed.

    python3 tests/test_ir_record.py
"""

import os
import shutil
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import ir_record  # noqa: E402

failures = 0


def check(what, ok):
    global failures
    print("%-58s %s" % (what, "PASS" if ok else "FAIL"))
    if not ok:
        failures += 1


class FakeStream(object):
    """A serial port that hands out a fixed text."""

    def __init__(self, text):
        self.text = text
        self.read_at = 0

    @property
    def in_waiting(self):
        return len(self.text)

    def read(self, size):
        chunk = self.text[self.read_at:self.read_at + size]
        self.read_at += len(chunk)
        return chunk.encode("utf-8")


CAPTURE_LOG = """[ir rx] capture 1: 16 edges
[ir rx] decode: header 4350/4400 us, 48 bits, bytes 4D B2 FC 03 06 F9
[ir rx] raw (level:width us):
[ir rx]  H:4350  L:4400  H:560  L:560  H:560  L:1690  H:560  L:560
[ir rx]  H:560  L:1690  H:560  L:560  H:560  L:560  H:560  L:1690
[ir rx] pulse width histogram (level:width us x count):
[ir rx]   H:4350   x 1
[ir rx]   H:560    x 48
[ir rx]   L:560    x 18
[ir rx]   L:1690   x 30
[ir rx] long pulses (>= 1500 us): H:4350 L:4400
[ir rx] 16 edges, 4 distinct widths, 2 long pulses
radio channel 5 (want 5), remote paired
"""

NO_HEADER_LOG = """[ir rx] capture 2: 4 edges
[ir rx] decode: no Midea header in this burst
[ir rx] raw (level:width us):
[ir rx]  H:400  L:500  H:380  L:1500
[ir rx] pulse width histogram (level:width us x count):
[ir rx]   H:390    x 2
[ir rx] 4 edges, 2 distinct widths, 0 long pulses
"""

TWO_CAPTURES = CAPTURE_LOG + CAPTURE_LOG


def captures_of(text):
    watcher = ir_record.LogWatcher(FakeStream(""))
    watcher._feed(text)
    return watcher.pending


def test_state_parsing():
    state = ir_record.ExpectedState()
    state.mode = "cool"
    state.fan = "high"
    state.temp_half = 42  # 21.0 C
    state.named = True

    label, _ = ir_record.parse_state("22", state)
    check("a temperature alone is understood", label == "cool_high_22")

    label, _ = ir_record.parse_state("fan med", state)
    check("a fan alone is understood", label == "cool_med_22")

    label, _ = ir_record.parse_state("mode auto", state)
    check("a mode alone is understood", label == "auto_med_22")

    label, _ = ir_record.parse_state("cool high 25", state)
    check("all three at once are understood", label == "cool_high_25")

    label, _ = ir_record.parse_state("", state)
    check("enter keeps the state", label == "cool_high_25")

    label, _ = ir_record.parse_state("off", state)
    check("off is understood", label == "off")

    label, _ = ir_record.parse_state("on", state)
    check("on is understood", label == "cool_high_25")

    label, _ = ir_record.parse_state("led", state)
    check("led is understood", label == "led")

    label, _ = ir_record.parse_state("turbo", state)
    check("turbo is understood", label == "turbo")

    label, _ = ir_record.parse_state("none", state)
    check("none is understood", label == "none")

    label, _ = ir_record.parse_state("22c", state)
    check("a temperature with a unit is understood", label == "cool_high_22")

    # A bare "auto" is a mode, "fan auto" is the fan.
    label, _ = ir_record.parse_state("auto", state)
    check("a bare auto is the mode", label == "auto_high_22")
    label, _ = ir_record.parse_state("fan auto", state)
    check("fan auto is the fan", label == "auto_auto_22")

    # Mistakes have to be refused.
    for text in ("banana", "cool hot 22", "temp 40", "fan 9"):
        try:
            ir_record.parse_state(text, state)
            check("'%s' is refused" % text, False)
        except ValueError:
            check("'%s' is refused" % text, True)

    # The first capture has to name a state.
    fresh = ir_record.ExpectedState()
    try:
        ir_record.parse_state("", fresh)
        check("the first capture needs a state", False)
    except ValueError:
        check("the first capture needs a state", True)


def test_half_degrees():
    state = ir_record.ExpectedState()
    state.named = True

    label, description = ir_record.parse_state("21.5", state)
    check("a half degree is understood", label == "cool_auto_21.5")
    check("the half degree is in the description", "21.5" in description)

    label, _ = ir_record.parse_state("22.5", state)
    check("another half degree is understood", label == "cool_auto_22.5")

    label, _ = ir_record.parse_state("21.5c", state)
    check("a half degree with a unit is understood", label == "cool_auto_21.5")

    label, _ = ir_record.parse_state("23", state)
    check("a whole degree after a half is understood", label == "cool_auto_23")

    for text in ("21.2", "16", "30.5"):
        try:
            ir_record.parse_state(text, state)
            check("'%s' is refused" % text, False)
        except ValueError:
            check("'%s' is refused" % text, True)


def test_fan_levels():
    state = ir_record.ExpectedState()
    state.named = True

    for level in range(1, 8):
        label, _ = ir_record.parse_state("fan %d" % level, state)
        check("fan level %d is understood" % level, label == "cool_f%d_24" % level)

    label, _ = ir_record.parse_state("fan max", state)
    check("max is the top point", label == "cool_f7_24")

    label, _ = ir_record.parse_state("7", state)
    check("a bare number is the fan level", label == "cool_f7_24")

    try:
        ir_record.parse_state("fan 9", state)
        check("fan 9 is refused", False)
    except ValueError:
        check("fan 9 is refused", True)


def test_named_frames():
    state = ir_record.ExpectedState()
    state.named = True
    for name in ("swing", "timer", "vane", "vaneangle", "soft", "silent",
                 "selfclean", "m", "up", "down", "mode", "power", "fan"):
        label, description = ir_record.parse_state(name, state)
        check("%s is a named frame" % name, label == name)
        check("%s says what it is" % name, "button" in description)


def test_incomplete_capture():
    # A capture whose raw block never ends. The next capture has to finish
    # it, otherwise the stream stays blocked.
    truncated = """[ir rx] capture 1: 4 edges
[ir rx] decode: header 4350/4400 us, 48 bits, bytes 4D B2 FC 03 06 F9
[ir rx] raw (level:width us):
[ir rx]  H:4350  L:4400  H:560  L:560
"""
    watcher = ir_record.LogWatcher(FakeStream(""))
    watcher._feed(truncated)
    check("a capture that did not finish is not complete", not watcher.pending)
    partial = watcher.partial()
    check("the unfinished capture is kept", partial is not None)
    check("the pulses that arrived are kept", partial is not None and len(partial.edges) == 4)
    check("the bytes that arrived are kept",
          partial is not None and partial.bytes == "4D B2 FC 03 06 F9")
    check("the raw lines are kept", partial is not None and len(partial.raw_lines) == 1)

    # A new capture finishes the one before it.
    watcher._feed(CAPTURE_LOG)
    check("the next capture is complete", len(watcher.pending) == 2)
    if len(watcher.pending) == 2:
        check("the unfinished one comes first", len(watcher.pending[0].edges) == 4)
        check("the new one is whole", len(watcher.pending[1].edges) == 16)


def test_partial_is_exposed():
    watcher = ir_record.LogWatcher(FakeStream(""))
    check("there is no partial capture at first", watcher.partial() is None)
    watcher._feed(CAPTURE_LOG)
    check("a finished capture is not partial", watcher.partial() is None)


def test_leftovers_are_dropped():
    # A frame that arrived before the prompt belongs to an earlier press and
    # has to be dropped, so the next capture starts clean.
    watcher = ir_record.LogWatcher(FakeStream(""))
    watcher._feed(CAPTURE_LOG)
    check("the frame is waiting", len(watcher.pending) == 1)
    since = watcher.pending[0].received_at + 1.0  # the prompt comes later
    capture, cancelled, dropped = ir_record.wait_for_capture(watcher, timeout=0.2, since=since)
    check("the leftover is dropped", capture is None)
    check("nothing is pending afterwards", not watcher.pending)


def test_the_same_button_twice_gives_two_captures():
    # A frame that carries the same bytes as the capture before it must not
    # be thrown away: pressing the same button twice gives two captures.
    watcher = ir_record.LogWatcher(FakeStream(""))
    watcher._feed(CAPTURE_LOG)
    first = watcher.pending.pop(0)
    watcher._feed(CAPTURE_LOG)
    second = watcher.pending.pop(0)
    check("the two frames are the same", first.bytes == second.bytes)
    # The second frame arrives after the prompt was shown, so it is kept.
    watcher.pending.append(second)
    capture, cancelled, dropped = ir_record.wait_for_capture(watcher, timeout=0.2, since=0.0)
    check("the second press is taken", capture is not None and capture.bytes == first.bytes)
    check("nothing was dropped", dropped == 0)


def test_repeats_are_collected():
    # The remote sends the same frame a few times. The quiet period waits
    # until the repeats stop.
    class RepeatingStream(FakeStream):
        def __init__(self, text, times):
            FakeStream.__init__(self, text)
            self.times = times

        @property
        def in_waiting(self):
            remaining = len(self.text) - self.read_at
            if remaining == 0 and self.times > 0:
                return len(self.text)
            return remaining

        def read(self, size=1):
            chunk = FakeStream.read(self, size)
            if not chunk and self.times > 0:
                self.times -= 1
                self.read_at = 0
                chunk = FakeStream.read(self, size)
            return chunk

    watcher = ir_record.LogWatcher(RepeatingStream(CAPTURE_LOG, 3))
    watcher.start()
    try:
        capture, cancelled, dropped = ir_record.wait_for_capture(watcher, timeout=2.0)
        check("the first frame is taken", capture is not None)
        repeats, other = ir_record.settle_captures(watcher, capture, quiet=0.2, limit=3.0)
        check("the repeats are collected", repeats >= 2)
        check("no other press is handed back", other is None)
    finally:
        watcher.stop()


def test_session_log():
    # Everything the device says is written down, not only the frames.
    import tempfile

    directory = tempfile.mkdtemp()
    path = os.path.join(directory, "session.log")
    with open(path, "w") as handle:
        watcher = ir_record.LogWatcher(FakeStream(""), handle)
        watcher._feed(CAPTURE_LOG)
        watcher._feed("radio channel 5 (want 5), remote paired, ir interrupts 12, ir pulses 10\n")
    with open(path) as handle:
        text = handle.read()
    check("the frames are in the session log", "[ir rx] capture 1: 16 edges" in text)
    check("the status line is in the session log", "ir interrupts 12" in text)
    check("every line has a time", text.splitlines()[0][:2].isdigit())
    shutil.rmtree(directory, ignore_errors=True)


def main():
    captures = captures_of(CAPTURE_LOG)
    check("one capture is found", len(captures) == 1)
    if captures:
        capture = captures[0]
        check("the capture number is read", capture.number == 1)
        check("the bytes are read", capture.bytes == "4D B2 FC 03 06 F9")
        check("the bit count is read", capture.bits == 48)
        check("the header is read", capture.header == "4350/4400")
        check("all pulses are read", len(capture.edges) == 16)
        check("the first pulse is the header mark", capture.edges[0] == (1, 4350))
        check("the second pulse is the header space", capture.edges[1] == (0, 4400))
        check("the summary holds the bytes", "4D B2 FC 03 06 F9" in capture.summary())

    captures = captures_of(NO_HEADER_LOG)
    check("a capture without a header is found", len(captures) == 1)
    if captures:
        capture = captures[0]
        check("it has no bytes", capture.bytes == "")
        check("the pulses are still read", len(capture.edges) == 4)
        check("the summary says there is no header", "no Midea header" in capture.summary())

    captures = captures_of(TWO_CAPTURES)
    check("two captures are found", len(captures) == 2)
    if len(captures) == 2:
        check("the second capture has its own number", captures[1].number == 1)

    test_state_parsing()
    test_half_degrees()
    test_fan_levels()
    test_named_frames()
    test_incomplete_capture()
    test_partial_is_exposed()
    test_leftovers_are_dropped()
    test_the_same_button_twice_gives_two_captures()
    test_repeats_are_collected()
    test_session_log()

    print("\n%s (%d failure%s)" % ("FAILED" if failures else "ALL PASS", failures,
                                  "" if failures == 1 else "s"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
