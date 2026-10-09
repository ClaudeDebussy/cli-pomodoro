// A stand-in for platform_linux.cpp / platform_windows.cpp that records what
// pomo asks the OS to do, so the tests can check it without a real terminal,
// speakers or desktop.
#include "fake_platform.h"

using namespace std;

bool focused = true;

const char *DEFAULT_SOUND_WORK_DONE = "work_done.wav";
const char *DEFAULT_SOUND_BREAK_DONE = "break_done.wav";
const char *DEFAULT_SOUND_LONG_BREAK = "long_break.wav";
const char *PROGRESS_INDICATOR_NAME = "top bar";

FakePlatform fake;

void FakePlatform::reset() {
    *this = FakePlatform();
    focused = true;
}

void setup_terminal() {}
TerminalSize terminal_size() { return fake.size; }

int read_key(int) {
    if (fake.keys.empty()) return -2;  // no more input: as if it was closed
    int key = fake.keys.front();
    fake.keys.pop_front();
    return key;
}

void set_window_title(const string &title) { fake.window_title = title; }
string window_title() { return "pomo [test]"; }
void sleep_ms(int) {}
string config_path() { return fake.config_path; }

void play_sound(const string &file) { fake.sounds.push_back(file); }
void send_desktop_notification(const string &message, bool sticky) {
    fake.notifications.push_back(message);
    fake.last_notification_sticky = sticky;
}
void minimize_window() { ++fake.minimize_calls; }
void restore_window() { ++fake.restore_calls; }

void show_progress(double fraction, ProgressState state) {
    fake.progress_shown = true;
    fake.progress_fraction = fraction;
    fake.progress_state = state;
}
void hide_progress() {
    fake.progress_shown = false;
    ++fake.hide_progress_calls;
}
