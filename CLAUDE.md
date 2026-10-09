# pomo

A terminal pomodoro timer for Linux and Windows, in C++17.

## Layout

- `pomo.cpp`: everything that's the same on every OS: config, timer, keys, drawing.
- `platform.h`: what pomo needs from the OS. Implemented by `platform_linux.cpp` and `platform_windows.cpp`.
- `extension/`: the GNOME Shell extension (minimize/restore, notifications, top bar progress bar), called over D-Bus.
- `tests/`: unit tests against a fake platform, and end-to-end tests that drive the real binary in a pty.

## Workflow

- Run `make test` before every commit. If a change is meant to alter behavior, update the matching test in the same commit.
- Commit and push after each change.
- No Co-Authored-By or other attribution lines in commits.
- Don't rewrite git history or force-push.
- Keep the README and man page (`pomo.1`) in step with user-visible changes.
- The Windows build can't be compiled locally (no MinGW); CI builds it on every push. Check the run with `gh run list`.
- Code is written for clarity over conciseness.
