// Windows implementation of platform.h, for Windows Terminal (and the classic
// console on Windows 10 and later): the console API for raw input, escape
// codes for drawing, PlaySound for sounds, a PowerShell toast for
// notifications, and the taskbar button's progress bar for the timer.
#include "platform.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <windows.h>
#include <mmsystem.h>

using namespace std;

bool focused = true;

const char *DEFAULT_SOUND_WORK_DONE = "C:\\Windows\\Media\\tada.wav";
const char *DEFAULT_SOUND_BREAK_DONE = "C:\\Windows\\Media\\chimes.wav";
const char *DEFAULT_SOUND_LONG_BREAK = "C:\\Windows\\Media\\chord.wav";
const char *PROGRESS_INDICATOR_NAME = "taskbar";

// ============================================================================
// Terminal
// ============================================================================

static HANDLE console_in, console_out;
static DWORD original_in_mode, original_out_mode;
static UINT original_output_codepage;

static void restore_terminal() {
    // progress bar off, focus reports off, line wrap on, colors reset, cursor shown, leave alt screen
    printf("\033]9;4;0;0\a\033[?1004l\033[?7h\033[0m\033[?25h\033[?1049l");
    fflush(stdout);
    SetConsoleMode(console_in, original_in_mode);
    SetConsoleMode(console_out, original_out_mode);
    SetConsoleOutputCP(original_output_codepage);
}

// Closing the window or tab, logging off, or shutting down.
static BOOL WINAPI on_console_event(DWORD) {
    restore_terminal();
    ExitProcess(0);
}

void setup_terminal() {
    console_in = GetStdHandle(STD_INPUT_HANDLE);
    console_out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(console_in, &original_in_mode);
    GetConsoleMode(console_out, &original_out_mode);
    original_output_codepage = GetConsoleOutputCP();

    // Escape codes for colors and cursor movement, and UTF-8 for the block characters.
    DWORD out_mode = ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(console_out, out_mode)) {
        fprintf(stderr, "pomo: this console doesn't support escape codes (needs Windows 10 or later)\n");
        exit(1);
    }
    SetConsoleOutputCP(CP_UTF8);

    // Keys one at a time, no echo, Ctrl-C as a key press rather than a signal,
    // and keys and focus reports as escape sequences, like a Linux terminal.
    SetConsoleMode(console_in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT);

    // Buffer each frame and write it in one go, so drawing doesn't flicker.
    setvbuf(stdout, nullptr, _IOFBF, 1 << 16);

    atexit(restore_terminal);
    SetConsoleCtrlHandler(on_console_event, TRUE);
    // alt screen, hide cursor, report focus changes, no line wrap
    printf("\033[?1049h\033[?25l\033[?1004h\033[?7l");
}

TerminalSize terminal_size() {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(console_out, &info)) return {80, 24};
    return {info.srWindow.Right - info.srWindow.Left + 1, info.srWindow.Bottom - info.srWindow.Top + 1};
}

// Characters typed but not handed out by read_key yet.
static deque<int> pending_chars;

// Waits up to timeout_ms for console input and moves any typed characters
// into pending_chars. Returns false if nothing happened in time.
static bool read_console_input(int timeout_ms) {
    if (WaitForSingleObject(console_in, timeout_ms) != WAIT_OBJECT_0) return false;
    INPUT_RECORD records[64];
    DWORD count = 0;
    if (!ReadConsoleInputW(console_in, records, 64, &count)) return false;
    for (DWORD i = 0; i < count; ++i) {
        const INPUT_RECORD &record = records[i];
        if (record.EventType == FOCUS_EVENT) {
            focused = record.Event.FocusEvent.bSetFocus;  // the classic console reports focus this way
        } else if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
            WCHAR c = record.Event.KeyEvent.uChar.UnicodeChar;
            if (c == 0 || c > 127) continue;  // shift, arrow keys without VT input, non-ASCII
            for (WORD n = 0; n < record.Event.KeyEvent.wRepeatCount; ++n) pending_chars.push_back(c);
        }
    }
    return true;
}

// The next typed character, or -1 if none arrives within timeout_ms.
static int next_char(int timeout_ms) {
    if (pending_chars.empty() && !read_console_input(timeout_ms)) return -1;
    if (pending_chars.empty()) return -1;  // something else happened, like a focus change
    int c = pending_chars.front();
    pending_chars.pop_front();
    return c;
}

int read_key(int timeout_ms) {
    int key = next_char(timeout_ms);
    if (key != 27) return key;

    // Esc: either the Esc key on its own, or the start of an escape sequence.
    // Focus reports (ESC [ I and ESC [ O) update `focused`; other sequences,
    // like arrow keys, are ignored.
    if (next_char(20) != '[') return 27;
    int c = next_char(20);
    if (c == 'I') focused = true;
    if (c == 'O') focused = false;
    // Skip the rest of a longer sequence, like ESC [ 1 ; 5 A for Ctrl+Up.
    while (c != -1 && !(c >= 0x40 && c <= 0x7E)) c = next_char(20);
    return -1;
}

void set_window_title(const string &title) {
    printf("\033]0;%s\007", title.c_str());
    fflush(stdout);
}

string window_title() {
    return "pomo";
}

void sleep_ms(int ms) {
    Sleep(ms);
}

string config_path() {
    const char *appdata = getenv("APPDATA");
    return string(appdata ? appdata : ".") + "\\pomo\\config";
}

// ============================================================================
// Desktop integration
// ============================================================================

void play_sound(const string &file) {
    if (!PlaySoundA(file.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) printf("\a");
}

// Runs a command line without opening a window and without waiting for it.
static void run_hidden(string command_line) {
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (CreateProcessA(nullptr, &command_line[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                       &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

// Shows a Windows toast notification through PowerShell, which every Windows
// 10 and 11 has. The toast is sent under PowerShell's app id, since pomo
// isn't registered as an app. `message` must not contain single quotes.
void send_desktop_notification(const string &message, bool sticky) {
    string script =
        "$m = [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime];"
        "$x = $m::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02);"
        "$t = $x.GetElementsByTagName('text');"
        "$null = $t.Item(0).AppendChild($x.CreateTextNode('Pomodoro'));"
        "$null = $t.Item(1).AppendChild($x.CreateTextNode('" + message + "'));" +
        (sticky ? "$x.DocumentElement.SetAttribute('duration', 'long');" : "") +
        "$toast = [Windows.UI.Notifications.ToastNotification]::new($x);"
        "$m::CreateToastNotifier('{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\WindowsPowerShell\\v1.0\\powershell.exe').Show($toast)";
    run_hidden("powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + "\"");
}

// In Windows Terminal, GetConsoleWindow() returns a stand-in window, and
// Windows Terminal applies minimize and restore requests on it to its own
// window (which holds all its tabs).
void minimize_window() {
    ShowWindow(GetConsoleWindow(), SW_MINIMIZE);
}

void restore_window() {
    HWND window = GetConsoleWindow();
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}

// The progress bar on the terminal's taskbar button, set with the ConEmu /
// Windows Terminal escape code ESC ] 9 ; 4 ; state ; percent BEL.
// States: 1 = normal (green), 2 = error (red), 4 = paused (yellow).
static int shown_state = 0, shown_percent = -1;  // what the taskbar shows now (state 0 = nothing)

void show_progress(double fraction, ProgressState state) {
    int code = state == ProgressState::WORK ? 2 : state == ProgressState::BREAK ? 1 : 4;
    int percent = (int)(fraction * 100);
    if (code == shown_state && percent == shown_percent) return;
    shown_state = code;
    shown_percent = percent;
    printf("\033]9;4;%d;%d\a", code, percent);
    fflush(stdout);
}

void hide_progress() {
    shown_state = 0;
    printf("\033]9;4;0;0\a");
    fflush(stdout);
}
