#!/usr/bin/env python3
"""Host test for the recorder's cancel key and its handling of a capture
that never finishes.

Runs the recorder on a pseudo terminal, so the key reading is really
exercised. Needs a POSIX system.

    python3 tests/test_ir_record_cancel.py
"""

import os
import pty
import select
import shutil
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

failures = 0


def check(what, ok):
    global failures
    print("%-58s %s" % (what, "PASS" if ok else "FAIL"))
    if not ok:
        failures += 1


# A capture whose raw block never ends, so it never finishes.
TRUNCATED_LOG = """[ir rx] capture 1: 4 edges
[ir rx] decode: header 4350/4400 us, 48 bits, bytes 4D B2 FC 03 06 F9
[ir rx] raw (level:width us):
[ir rx]  H:4350  L:4400  H:560  L:560
"""

FAKE_SERIAL = '''import os


class SerialException(Exception):
    pass


class Serial(object):
    def __init__(self, port, baud=9600, timeout=None):
        self.text = open(os.environ["FAKE_LOG"]).read()
        self.pos = 0

    @property
    def in_waiting(self):
        return len(self.text) - self.pos

    def read(self, size=1):
        chunk = self.text[self.pos:self.pos + size]
        self.pos += len(chunk)
        return chunk.encode("utf-8")

    def close(self):
        pass
'''


def run_recorder(log_text, triggers, timeout=30):
    """Run the recorder on a pty and send the keys.

    A trigger is (how many times the text has to appear, the text, the key).
    Returns what the recorder printed and what it wrote.
    """
    directory = tempfile.mkdtemp()
    try:
        log_path = os.path.join(directory, "log.txt")
        with open(log_path, "w") as handle:
            handle.write(log_text)
        fake = os.path.join(directory, "fake")
        os.mkdir(fake)
        with open(os.path.join(fake, "serial.py"), "w") as handle:
            handle.write(FAKE_SERIAL)

        out_path = os.path.join(directory, "captures.txt")
        environment = dict(os.environ)
        environment["PYTHONPATH"] = fake
        environment["FAKE_LOG"] = log_path

        pid, fd = pty.fork()
        if pid == 0:
            os.execvpe(
                sys.executable,
                [sys.executable, os.path.join(ROOT, "tools", "ir_record.py"),
                 "--port", "/dev/fake", "--out", out_path, "--timeout", "4"],
                environment,
            )
            os._exit(1)

        output = ""
        sent = [False] * len(triggers)
        deadline = time.time() + timeout
        while time.time() < deadline:
            ready, _, _ = select.select([fd], [], [], 0.2)
            if ready:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                output += chunk.decode("utf-8", "replace")
            for index, (count, text, key) in enumerate(triggers):
                if sent[index]:
                    continue
                if output.count(text) >= count:
                    sent[index] = True
                    try:
                        os.write(fd, key.encode("utf-8"))
                    except OSError:
                        break
            if "wrote " in output and all(sent):
                break
        try:
            os.close(fd)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except OSError:
            pass

        captures = ""
        if os.path.exists(out_path):
            with open(out_path) as handle:
                captures = handle.read()
        return output, captures
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main():
    # 1. "c" and Enter cancels the wait, and the recorder goes back to the prompt.
    output, captures = run_recorder(
        "",
        [
            (1, "capture> ", "22\n"),
            (1, "waiting...", "c\n"),
            (2, "capture> ", "q\n"),
        ],
    )
    check("the recorder starts", "capture>" in output)
    check("the state is confirmed", "state set to cool_auto_22" in output)
    check("the wait can be cancelled", "cancelled" in output)
    check("the recorder goes back to the prompt", output.count("capture>") >= 2)
    check("nothing was recorded", "wrote 0 capture(s)" in output)

    # 2. Ctrl+C cancels the wait as well, and does not end the session.
    output, captures = run_recorder(
        "",
        [
            (1, "capture> ", "22\n"),
            (1, "waiting...", "\x03"),
            (2, "capture> ", "q\n"),
        ],
    )
    check("Ctrl+C cancels the wait", "cancelled" in output)
    check("Ctrl+C does not end the session", output.count("capture>") >= 2)

    # 2. A capture that never finishes is kept with the pulses that arrived.
    output, captures = run_recorder(
        TRUNCATED_LOG,
        [
            (1, "capture> ", "22\n"),
            (2, "capture> ", "q\n"),
        ],
    )
    check("the unfinished capture is kept", "incomplete=1" in captures)
    check("the pulses that arrived are written", "H:4350 L:4400 H:560 L:560" in captures)
    check("the bytes that arrived are written", "4D B2 FC 03 06 F9" in captures)
    check("the report says it is incomplete", "kept the 4 pulse(s)" in output)

    print("\n%s (%d failure%s)" % ("FAILED" if failures else "ALL PASS", failures,
                                  "" if failures == 1 else "s"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
