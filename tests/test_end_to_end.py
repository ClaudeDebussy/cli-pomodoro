#!/usr/bin/env python3
"""End-to-end tests: run the real pomo binary in a pseudo-terminal, press keys,
and check what it draws and that it leaves the terminal the way it found it.

Usage: python3 tests/test_end_to_end.py ./pomo
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import tempfile
import termios
import time

POMO = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./pomo")


class Pomo:
    """pomo running in a pseudo-terminal of the given size."""

    def __init__(self, *args, cols=80, rows=30, config=""):
        self.config_home = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.config_home, "pomo"))
        with open(os.path.join(self.config_home, "pomo", "config"), "w") as f:
            f.write(config)

        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            # Keep it away from the desktop: no sounds, notifications or GNOME extension.
            os.environ["XDG_CONFIG_HOME"] = self.config_home
            os.environ["PATH"] = "/nonexistent"
            os.environ.pop("DBUS_SESSION_BUS_ADDRESS", None)
            os.execv(POMO, [POMO, "-n", "none", *args])
        self.resize(cols, rows)
        self.output = b""
        self.read(0.5)

    def resize(self, cols, rows):
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def read(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    self.output += os.read(self.fd, 65536)
                except OSError:  # pomo exited
                    return

    def press(self, keys, wait=0.4):
        os.write(self.fd, keys.encode())
        self.read(wait)

    def last_frame(self):
        """The text of the most recent full redraw, with escape codes removed."""
        frame = self.output.split(b"\x1b[H\x1b[2J")[-1].decode(errors="replace")
        frame = re.sub(r"\x1b\[[0-9]+;[0-9]+H", "\n", frame)  # pomo moves to each row it draws
        frame = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", frame)
        return re.sub(r"\x1b\][^\x07]*\x07", "", frame)

    def wait_for_exit(self, seconds=2):
        end = time.time() + seconds
        while time.time() < end:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                return os.waitstatus_to_exitcode(status)
            self.read(0.05)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        return None


failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print("FAIL", message)


def test_draws_the_timer_and_quits_cleanly():
    pomo = Pomo()
    frame = pomo.last_frame()
    check("POMODORO #1" in frame, "title is shown")
    check("Running" in frame, "status is shown")
    check("█" in frame, "digits are drawn")
    pomo.press("q")
    check(pomo.wait_for_exit() == 0, "q exits with status 0")
    check(pomo.output.endswith(b"\x1b[?1049l"), "leaves the alternate screen on exit")


def test_space_pauses():
    pomo = Pomo()
    pomo.press(" ")
    check("Paused" in pomo.last_frame(), "space pauses")
    pomo.press(" ")
    check("Running" in pomo.last_frame(), "space resumes")
    pomo.press("q")
    pomo.wait_for_exit()


def test_s_sets_the_length():
    pomo = Pomo()
    pomo.press(" s7\r")
    check("Paused" in pomo.last_frame(), "prompt closes after Enter")
    # 07:00 in the block font: compare against a fresh pomo started with -w 7
    seven = Pomo("-w", "7")
    seven.press(" ")
    digits = lambda frame: [line for line in frame.splitlines() if "█" in line]
    check(digits(pomo.last_frame()) == digits(seven.last_frame()), "timer shows 07:00")
    for p in (pomo, seven):
        p.press("q")
        p.wait_for_exit()


def test_config_file_is_used():
    pomo = Pomo(config="work = 3\n")
    pomo.press(" ")
    three = Pomo("-w", "3")
    three.press(" ")
    digits = lambda frame: [line for line in frame.splitlines() if "█" in line]
    check(digits(pomo.last_frame()) == digits(three.last_frame()), "work = 3 from the config file")
    for p in (pomo, three):
        p.press("q")
        p.wait_for_exit()


def test_bad_option_prints_usage():
    pomo = Pomo("-x")
    check(pomo.wait_for_exit() == 1, "unknown option exits with status 1")
    check(b"usage: pomo" in pomo.output, "prints usage")


def test_resize_redraws_at_the_new_size():
    pomo = Pomo(cols=80, rows=30)
    pomo.resize(50, 20)
    pomo.read(0.5)
    frame = pomo.last_frame()
    check("POMODORO" not in frame, "narrow window hides the title")
    check(sum("█" in line or "▀" in line or "▄" in line for line in frame.splitlines()) > 5,
          "narrow window scales the digits up")
    pomo.press("q")
    pomo.wait_for_exit()


if __name__ == "__main__":
    tests = [value for name, value in list(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
    print(f"{len(tests)} end-to-end tests, {failures} failures")
    sys.exit(1 if failures else 0)
