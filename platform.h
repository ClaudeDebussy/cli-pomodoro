// Everything pomo needs from the operating system. pomo.cpp only talks to the
// OS through these; platform_linux.cpp and platform_windows.cpp implement them.
#pragma once
#include <string>

// Does our terminal window have focus? Kept up to date by the platform code
// (from focus reports in the input), read by pomo.cpp.
extern bool focused;

// ---- Terminal --------------------------------------------------------------

// Switches the terminal into raw mode (keys one at a time, no echo) and turns
// on the escape codes pomo uses. Restores everything when the program exits.
void setup_terminal();

struct TerminalSize {
    int cols, rows;
};
TerminalSize terminal_size();

// Waits up to timeout_ms for a key. Returns the key's character (Enter is
// '\r' or '\n', Backspace 8 or 127, Esc 27), or -1 if no key arrived in time,
// or -2 if the input was closed.
int read_key(int timeout_ms);

void set_window_title(const std::string &title);

// The title pomo gives its window. On Linux it includes the process id, so the
// GNOME extension can find our window.
std::string window_title();

void sleep_ms(int ms);

// ---- Configuration ---------------------------------------------------------

std::string config_path();

// Default sound files for the three events.
extern const char *DEFAULT_SOUND_WORK_DONE;
extern const char *DEFAULT_SOUND_BREAK_DONE;
extern const char *DEFAULT_SOUND_LONG_BREAK;

// ---- Desktop integration ---------------------------------------------------

// Plays a sound file without waiting for it to finish. Falls back to the
// terminal bell if it can't be played.
void play_sound(const std::string &file);

// Shows a desktop notification. A sticky one stays until it's dismissed.
void send_desktop_notification(const std::string &message, bool sticky);

void minimize_window();
void restore_window();

// The timer's progress outside the terminal: the GNOME top bar on Linux, the
// taskbar button on Windows.
enum class ProgressState { WORK, BREAK, PAUSED };
extern const char *PROGRESS_INDICATOR_NAME;  // what the key hint calls it, e.g. "top bar"
void show_progress(double fraction, ProgressState state);
void hide_progress();
