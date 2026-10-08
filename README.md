# pomo

A terminal pomodoro timer for Linux.

`pomo` runs a 25-minute work timer and then queues a 5-minute break that waits for you to start it. Every 4th pomodoro, the break it queues is a 15-minute long break instead. Each kind of event plays its own quiet sound and sends a desktop notification. Sounds always play unless you mute them with `m` (or turn them off with `-n desktop` or `-n none`); desktop notifications are skipped while the pomo window has focus, since you can already see it.

## Install

One command, no sudo. It builds pomo, installs it into `~/.local`, and on GNOME installs the extension too. Run it again to update:

```
curl -fsSL https://raw.githubusercontent.com/ClaudeDebussy/cli-pomodoro/main/install.sh | sh
```

Or from a clone of the repo:

```
./install.sh               # same thing, from this checkout
sudo make install          # or: system-wide, binary + man page into /usr/local
```

Needs a C++17 compiler. Sounds use `pw-play`, `paplay` or `canberra-gtk-play`, whichever is installed. Notifications use `notify-send`.

## Usage

```
pomo                 # start a 25 minute pomodoro
pomo -w 50 -s 10     # 50 min work, 10 min short break
pomo -n sound        # notify with sound only
pomo -m              # start minimized, pop back up when a timer ends
```

| Option | Meaning | Default |
|---|---|---|
| `-w N` | work minutes | 25 |
| `-s N` | short break minutes | 5 |
| `-l N` | long break minutes | 15 |
| `-e N` | long break every N pomodoros | 4 |
| `-n MODE` | `sound`, `desktop`, `both`, `bell`, `none` | both |
| `-m` | start minimized | off |

### Keys

| Key | Action |
|---|---|
| space / Enter / `p` | pause, resume, or start a waiting timer |
| `1` / `5` / `0` | add 1 / 5 / 10 minutes |
| `+` | add any number of minutes (type it, then Enter) |
| `s` | set the timer to any number of minutes (e.g. `s 7` Enter) |
| `m` | mute / unmute sounds |
| `v` | switch between digits and a progress bar (no numbers) |
| `t` | show/hide a progress bar in the GNOME top bar (needs the extension); click it to minimize or restore pomo |
| `r` | restart the current timer |
| `b` | start the break now |
| `w` | start work now |
| `q` | quit |

A history of finished timers is shown under the timer, newest first. While the window is unfocused, only the timer (or bar) is shown, in the same spot. In a window narrower than 62 columns, only the timer is shown, centered and scaled up to fill the window. When a timer is waiting for you to press space, it slowly fades in and out.

## Configuration

Put defaults in `~/.config/pomo/config`:

```
work = 25
short = 5
long = 15
every = 4
notify = both
minimized = no
topbar = yes
```

See `man pomo` for every setting, including the sound for each event.

## Minimize and restore (GNOME on Wayland)

Wayland doesn't let terminal programs minimize or raise their own window. For `-m` to work, and for clicking a notification to bring the timer back, install the bundled GNOME Shell extension:

```
make install-extension     # then log out and back in
gnome-extensions enable pomo@ajchurchill
```

The extension finds pomo's window by its title, `pomo [PID]`, so the pomo tab has to be the visible tab in its window. Without the extension, pomo sends the standard xterm minimize/restore codes, which only some terminals honor.
