// Linux implementation of platform.h: termios for the terminal, and the pomo
// GNOME Shell extension (see extension/) for window control, notifications and
// the top bar progress bar, with notify-send and xterm escape codes as fallbacks.
#include "platform.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

using namespace std;

bool focused = true;

const char *DEFAULT_SOUND_WORK_DONE = "/usr/share/sounds/freedesktop/stereo/complete.oga";
const char *DEFAULT_SOUND_BREAK_DONE = "/usr/share/sounds/freedesktop/stereo/message.oga";
const char *DEFAULT_SOUND_LONG_BREAK = "/usr/share/sounds/freedesktop/stereo/bell.oga";
const char *PROGRESS_INDICATOR_NAME = "top bar";

// ============================================================================
// Terminal
// ============================================================================

static termios original_termios;

static void restore_terminal() {
    // focus reports off, line wrap on, colors reset, cursor shown, leave alt screen
    printf("\033[?1004l\033[?7h\033[0m\033[?25h\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
}

static void on_signal(int) {
    restore_terminal();
    _exit(0);
}

void setup_terminal() {
    tcgetattr(STDIN_FILENO, &original_termios);
    termios raw = original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);  // read keys one at a time, don't echo them
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    // alt screen, hide cursor, report focus changes, no line wrap
    printf("\033[?1049h\033[?25l\033[?1004h\033[?7l");
}

TerminalSize terminal_size() {
    winsize size{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    return {size.ws_col ? size.ws_col : 80, size.ws_row ? size.ws_row : 24};
}

static bool wait_for_input(int timeout_ms) {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    return poll(&input, 1, timeout_ms) > 0;
}

int read_key(int timeout_ms) {
    if (!wait_for_input(timeout_ms)) return -1;
    char key;
    if (read(STDIN_FILENO, &key, 1) != 1) return -2;
    if (key != 27) return key;

    // Esc: either the Esc key on its own, or the start of an escape sequence.
    // Focus reports (ESC [ I and ESC [ O) update `focused`; other sequences,
    // like arrow keys, are ignored.
    char seq[2];
    if (!wait_for_input(20) || read(STDIN_FILENO, seq, 2) != 2 || seq[0] != '[') return 27;
    if (seq[1] == 'I') focused = true;
    if (seq[1] == 'O') focused = false;
    return -1;
}

void set_window_title(const string &title) {
    printf("\033]0;%s\007", title.c_str());
    fflush(stdout);
}

void sleep_ms(int ms) {
    usleep(ms * 1000);
}

string config_path() {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) return string(xdg) + "/pomo/config";
    const char *home = getenv("HOME");
    return string(home ? home : ".") + "/.config/pomo/config";
}

// ============================================================================
// GNOME Shell extension and desktop integration
// ============================================================================

// The GNOME extension finds our terminal window by this tag in its title.
static string window_tag() {
    return "pomo [" + to_string(getpid()) + "]";
}

string window_title() {
    return window_tag();
}

static string extension_command(const string &method, const string &args) {
    return "gdbus call --session -d org.gnome.Shell -o /org/gnome/Shell/Extensions/Pomo "
           "-m org.gnome.Shell.Extensions.Pomo." + method + " -- '" + window_tag() + "' " + args +
           " >/dev/null 2>&1";
}

// Calls a method on the pomo GNOME Shell extension and waits for it.
// Returns false if the extension isn't installed or the call failed.
static bool call_extension(const string &method, const string &args) {
    return system(extension_command(method, args).c_str()) == 0;
}

void play_sound(const string &file) {
    // Try each common player in turn; fall back to the terminal bell.
    string command = "(pw-play '" + file + "' || paplay '" + file + "' || canberra-gtk-play -f '" + file +
                     "' || printf '\\a') >/dev/null 2>&1 &";
    (void)!system(command.c_str());
}

void send_desktop_notification(const string &message, bool sticky) {
    // The extension's notification raises our window when clicked; notify-send's can't on Wayland.
    if (call_extension("Notify", "'" + message + "' " + (sticky ? "true" : "false"))) return;
    string urgency = sticky ? "-u critical " : "";
    string command = "notify-send -a pomo " + urgency + "'Pomodoro' '" + message + "' >/dev/null 2>&1 &";
    (void)!system(command.c_str());
}

void minimize_window() {
    if (!call_extension("Minimize", "")) printf("\033[2t");  // fallback: xterm-style minimize
}

void restore_window() {
    if (!call_extension("Activate", "")) printf("\033[1t");  // fallback: xterm-style un-minimize
}

// Only calls the extension when what the bar shows changes, or every couple of
// seconds so the extension knows pomo is still running.
void show_progress(double fraction, ProgressState state) {
    // Red for work, green for breaks; greyed out while paused or waiting.
    const char *color = state == ProgressState::PAUSED ? "#888888"
                        : state == ProgressState::WORK ? "#dc3c3c"
                                                       : "#50c864";
    static int last_permille = -1;
    static string last_color;
    static auto last_sent = chrono::steady_clock::time_point();
    auto now = chrono::steady_clock::now();
    int permille = (int)(fraction * 1000);
    if (permille == last_permille && color == last_color && now - last_sent < chrono::seconds(2)) return;
    last_permille = permille;
    last_color = color;
    last_sent = now;

    char args[64];
    snprintf(args, sizeof args, "%.4f '%s'", fraction, color);
    // In the background, so a slow D-Bus call never stalls the screen.
    string command = extension_command("SetProgress", args) + " &";
    (void)!system(command.c_str());
}

void hide_progress() {
    call_extension("HideProgress", "");
}
