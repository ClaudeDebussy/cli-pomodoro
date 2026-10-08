#!/bin/sh
# Installs (or updates) pomo for the current user, without sudo:
#   curl -fsSL https://raw.githubusercontent.com/ClaudeDebussy/cli-pomodoro/main/install.sh | sh
# Or, from a checkout of the repo:  ./install.sh
set -eu

REPO=ClaudeDebussy/cli-pomodoro
PREFIX="$HOME/.local"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# 1. Check that the build tools are installed.
missing=""
for tool in g++ make; do
    have "$tool" || missing="$missing $tool"
done
if [ -n "$missing" ]; then
    echo "pomo needs:$missing"
    if have apt; then echo "Install them with: sudo apt install g++ make libnotify-bin"
    elif have dnf; then echo "Install them with: sudo dnf install gcc-c++ make libnotify"
    elif have pacman; then echo "Install them with: sudo pacman -S gcc make libnotify"
    elif have zypper; then echo "Install them with: sudo zypper install gcc-c++ make libnotify-tools"
    fi
    exit 1
fi
have notify-send || echo "Note: notify-send isn't installed, so desktop notifications need the GNOME extension."

# 2. Get the source: use this checkout if we're in one, otherwise download it.
script_dir=$(cd "$(dirname "$0")" 2>/dev/null && pwd || true)
if [ -n "$script_dir" ] && [ -f "$script_dir/pomo.cpp" ]; then
    src="$script_dir"
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    say "Downloading pomo"
    if curl -fsSL "https://github.com/$REPO/archive/refs/heads/main.tar.gz" | tar -xz -C "$tmp" 2>/dev/null; then
        src="$tmp/cli-pomodoro-main"
    elif have gh && gh repo clone "$REPO" "$tmp/src" -- --depth 1 --quiet; then
        src="$tmp/src"  # private repo: gh uses your GitHub login
    else
        die "couldn't download $REPO (if the repo is private, install gh and run: gh auth login)"
    fi
fi

# 3. Build and install into ~/.local.
say "Building"
make -C "$src" --no-print-directory -B pomo
say "Installing to $PREFIX"
make -C "$src" --no-print-directory install PREFIX="$PREFIX"

# 4. On GNOME, install the extension (minimize/restore, notifications, top bar progress bar).
if have gnome-extensions; then
    say "Installing the GNOME Shell extension"
    make -C "$src" --no-print-directory install-extension
fi

# 5. Make sure ~/.local/bin is on PATH.
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo
       echo "$PREFIX/bin isn't on your PATH yet. Open a new terminal, or add this to ~/.bashrc:"
       echo "  export PATH=\"\$HOME/.local/bin:\$PATH\"" ;;
esac

echo
say "Done. Run: pomo"
have gnome-extensions && echo "If this is the first install, log out and back in so GNOME loads the extension."
exit 0
