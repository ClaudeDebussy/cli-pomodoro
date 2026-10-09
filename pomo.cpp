// pomo - a dead simple pomodoro timer
//
// The file is laid out top to bottom as:
//   1. configuration (defaults, config file, command line)
//   2. notifications
//   3. drawing the screen
//   4. the timer state and the main loop
//
// Everything that depends on the operating system (the terminal, sounds,
// notifications, window control) is in platform.h, implemented by
// platform_linux.cpp and platform_windows.cpp.
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "platform.h"

using namespace std;
using Clock = chrono::steady_clock;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// 1. Configuration
// ============================================================================

// Every setting is a string; numbers are validated in validate_config().
map<string, string> cfg = {
    {"work", "25"},   // minutes per pomodoro
    {"short", "5"},   // minutes per short break
    {"long", "15"},   // minutes per long break
    {"every", "4"},   // a long break after every this many pomodoros
    {"notify", "both"},   // sound | desktop | both | bell | none
    {"minimized", "no"},  // yes = start minimized, sticky alert when a timer ends
    {"topbar", "yes"},    // yes = progress bar in the GNOME top bar / on the Windows taskbar button
    {"sound_work_done", DEFAULT_SOUND_WORK_DONE},
    {"sound_break_done", DEFAULT_SOUND_BREAK_DONE},
    {"sound_long_break", DEFAULT_SOUND_LONG_BREAK},
};

string trim(string s) {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    return s;
}

// Reads "key = value" lines. Blank lines and lines starting with # are ignored.
void load_config() {
    ifstream file(config_path());
    for (string line; getline(file, line);) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = trim(line.substr(0, eq));
        string value = trim(line.substr(eq + 1));
        cfg[key] = value;
    }
}

void usage() {
    puts("usage: pomo [-w min] [-s min] [-l min] [-e n] [-n sound|desktop|both|bell|none] [-m]\nSee man pomo.");
    exit(1);
}

// Command line options override the config file. Options that take a value
// accept it either attached (-w50) or as the next argument (-w 50).
void parse_args(int argc, char **argv) {
    const map<char, string> value_options = {
        {'w', "work"}, {'s', "short"}, {'l', "long"}, {'e', "every"}, {'n', "notify"},
    };
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg.size() < 2 || arg[0] != '-') usage();
        char option = arg[1];
        if (option == 'm' && arg.size() == 2) {
            cfg["minimized"] = "yes";
        } else if (value_options.count(option)) {
            string value = arg.substr(2);
            if (value.empty()) {
                if (i + 1 >= argc) usage();
                value = argv[++i];
            }
            cfg[value_options.at(option)] = value;
        } else {
            usage();
        }
    }
}

// Returns false (after printing why) if a numeric setting isn't a whole number >= 1.
bool validate_config() {
    for (const char *key : {"work", "short", "long", "every"}) {
        const string &value = cfg[key];
        char *end;
        long number = strtol(value.c_str(), &end, 10);
        if (*end != '\0' || number < 1) {
            fprintf(stderr, "pomo: %s must be a whole number of at least 1, got '%s'\n", key, value.c_str());
            return false;
        }
    }
    return true;
}

// ============================================================================
// 2. Notifications
// ============================================================================

// Global UI state that both the notifications and the drawing code need.
// (`focused`, whether our window has focus, is kept up to date by the platform code.)
bool muted = false;         // toggled with m
bool show_bar = false;      // toggled with v: progress bar instead of digits
bool show_progress_indicator = true;  // toggled with t: progress in the top bar / taskbar

// Called when a timer runs out. Sounds always play (unless muted); the
// window-raising and desktop notification only happen when we're not focused.
void notify(const string &message, const string &sound) {
    const string &mode = cfg["notify"];
    bool wants_sound = mode == "sound" || mode == "both";
    bool wants_desktop = mode == "desktop" || mode == "both";

    if (mode == "bell" && !muted) printf("\a");
    if (wants_sound && !muted) play_sound(sound);

    if (focused) return;  // the user is already looking at us

    bool sticky = cfg["minimized"] == "yes";
    if (sticky) restore_window();
    if (wants_desktop || sticky) send_desktop_notification(message, sticky);
}

// ============================================================================
// 3. Drawing
// ============================================================================

const char *RESET = "\033[0m";
const char *BOLD = "\033[1m";
const char *DIM = "\033[2m";

// Windows narrower than this show only the timer, centered and scaled up.
const int NARROW_WIDTH = 62;
// After the window size changes, show everything for this long (dragging a
// window edge takes focus away, which would otherwise hide it all).
const auto RESIZE_GRACE = chrono::milliseconds(1500);

// Normal layout, as row offsets from the top of the block:
//   0       title
//   2..6    digits or progress bar (5 rows)
//   8       status line
//   11..12  key hints
//   15      "History" heading
//   16..    history entries
const int FONT_ROWS = 5;
const int ROW_TIMER = 2;
const int ROW_STATUS = 8;
const int ROW_KEYS = 11;
const int ROW_HISTORY = 15;
const int LAYOUT_HEIGHT = 13;  // title through key hints

// 5-row block font for 0-9 (index 0-9) and ':' (index 10).
// Digits are 5 cells wide, the colon 3.
const char *FONT[11][FONT_ROWS] = {
    {"█████", "█   █", "█   █", "█   █", "█████"}, {"    █", "    █", "    █", "    █", "    █"},
    {"█████", "    █", "█████", "█    ", "█████"}, {"█████", "    █", "█████", "    █", "█████"},
    {"█   █", "█   █", "█████", "    █", "    █"}, {"█████", "█    ", "█████", "    █", "█████"},
    {"█████", "█    ", "█████", "█   █", "█████"}, {"█████", "    █", "    █", "    █", "    █"},
    {"█████", "█   █", "█████", "█   █", "█████"}, {"█████", "█   █", "█████", "    █", "█████"},
    {"   ", " █ ", "   ", " █ ", "   "},
};

const char *glyph_row(char c, int row) {
    int index = c == ':' ? 10 : c - '0';
    return FONT[index][row];
}

// Everything draw_screen() needs to know, prepared by the main loop.
struct Screen {
    string title;
    string time;            // "MM:SS"
    string status;          // may contain color codes
    string color;           // color code for the digits / bar
    vector<string> keys;    // key hint lines
    vector<string> history; // oldest first
    double progress;        // 0..1, for the bar view
};

// Number of terminal cells a string takes up: skips color codes and counts
// each UTF-8 character once.
int visible_width(const string &s) {
    int width = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033') {
            while (i < s.size() && s[i] != 'm') ++i;
        } else if ((s[i] & 0xC0) != 0x80) {  // not a UTF-8 continuation byte
            ++width;
        }
    }
    return width;
}

void print_centered(int row, const string &text, int cols) {
    int col = max(1, (cols - visible_width(text)) / 2 + 1);
    printf("\033[%d;%dH%s", row, col, text.c_str());
}

// The time in the block font, one string per row, at its normal size.
vector<string> render_digits(const string &time) {
    vector<string> rows(FONT_ROWS);
    for (int r = 0; r < FONT_ROWS; ++r)
        for (char c : time) rows[r] += string(glyph_row(c, r)) + " ";
    return rows;
}

// The time in the block font as a grid of on/off pixels, without the
// trailing gap after the last glyph.
vector<vector<bool>> digit_bitmap(const string &time) {
    vector<vector<bool>> bitmap(FONT_ROWS);
    for (int r = 0; r < FONT_ROWS; ++r) {
        for (char c : time) {
            const string row = glyph_row(c, r);
            // each cell is either ' ' (1 byte) or '█' (3 bytes in UTF-8)
            for (size_t b = 0; b < row.size(); b += row[b] == ' ' ? 1 : 3) bitmap[r].push_back(row[b] != ' ');
            bitmap[r].push_back(false);  // gap between glyphs
        }
        bitmap[r].pop_back();
    }
    return bitmap;
}

void draw_digits(const Screen &s, int top, int cols) {
    for (const string &row : render_digits(s.time)) {
        print_centered(top++, s.color + row + RESET, cols);
    }
}

// Digits scaled up to fill a small window. Each terminal row holds two pixel
// rows using half blocks (▀ ▄ █), so the height can be scaled more finely.
void draw_scaled_digits(const Screen &s, int cols, int rows) {
    vector<vector<bool>> bitmap = digit_bitmap(s.time);
    int native_width = bitmap[0].size();
    const double max_stretch = 1.3;  // how much taller than the font's own proportions we allow

    // Fill the width, then as much height as fits, then shrink the width back
    // if the height was what limited us.
    int width = cols - 4;
    int half_rows = min(2 * (rows - 2), (int)lround(max_stretch * 2.0 * width * FONT_ROWS / native_width));
    width = min(width, (int)lround(max_stretch * half_rows / 2.0 * native_width / FONT_ROWS));
    if (width < native_width || half_rows < 2 * FONT_ROWS) {  // never shrink below the normal size
        width = native_width;
        half_rows = 2 * FONT_ROWS;
    }

    // Nearest-neighbor lookup from the scaled grid back into the bitmap.
    auto pixel = [&](int x, int half_row) {
        return (bool)bitmap[half_row * FONT_ROWS / half_rows][x * native_width / width];
    };

    int term_rows = (half_rows + 1) / 2;
    int top = max(1, (rows - term_rows) / 2 + 1);
    for (int r = 0; r < term_rows; ++r) {
        string line = s.color;
        for (int x = 0; x < width; ++x) {
            bool upper = pixel(x, 2 * r);
            bool lower = 2 * r + 1 < half_rows && pixel(x, 2 * r + 1);
            line += upper && lower ? "█" : upper ? "▀" : lower ? "▄" : " ";
        }
        print_centered(top + r, line + RESET, cols);
    }
}

// A progress bar the same 5 rows tall as the digits, with 1/8-cell precision.
void draw_progress_bar(const Screen &s, int top, int cols, bool narrow) {
    static const char *EIGHTHS[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    int width = narrow ? max(1, cols - 4) : min(60, cols - 8);
    double progress = max(0.0, min(1.0, s.progress));
    int eighths = (int)(progress * width * 8);
    int full_cells = eighths / 8, partial = eighths % 8;

    string line = s.color;
    for (int i = 0; i < full_cells; ++i) line += "█";
    line += EIGHTHS[partial];
    line += string(RESET) + DIM;
    for (int i = full_cells + (partial > 0); i < width; ++i) line += "░";
    line += RESET;

    for (int r = 0; r < FONT_ROWS; ++r) print_centered(top + r, line, cols);
}

// True for a moment after the terminal changes size.
bool recently_resized(TerminalSize size, Clock::time_point now) {
    static TerminalSize last = size;
    static Clock::time_point resized_at = now - chrono::seconds(10);
    if (size.cols != last.cols || size.rows != last.rows) {
        last = size;
        resized_at = now;
    }
    return now - resized_at < RESIZE_GRACE;
}

void draw_screen(const Screen &s, Clock::time_point now) {
    TerminalSize size = terminal_size();
    int cols = size.cols, rows = size.rows;

    bool narrow = cols < NARROW_WIDTH;
    bool resizing = recently_resized(size, now);  // call every frame so it sees every size change
    // Unfocused or narrow: just the timer. Everything else is "extras".
    bool show_extras = !narrow && (focused || resizing);

    // Vertically center the whole layout, including as much history as fits.
    // The layout is the same whether or not extras are shown, so the timer
    // doesn't jump around when focus changes.
    int history_room = max(0, rows - 17);
    int history_shown = min((int)s.history.size(), history_room);
    // If some entries don't fit, the last visible line becomes "..." instead.
    bool history_truncated = history_shown < (int)s.history.size() && history_room > 0;
    if (history_truncated) history_shown = history_room - 1;
    int history_lines = history_shown + (history_truncated ? 1 : 0);
    int history_height = history_lines ? history_lines + 2 : 0;
    bool show_keys = rows > 12;
    int top = max(1, (rows - LAYOUT_HEIGHT - history_height) / 2);
    if (narrow) top = max(-1, (rows - FONT_ROWS) / 2 - 1);  // center just the timer

    printf("\033[H\033[2J");  // home and clear

    if (show_bar) draw_progress_bar(s, top + ROW_TIMER, cols, narrow);
    else if (narrow) draw_scaled_digits(s, cols, rows);
    else draw_digits(s, top + ROW_TIMER, cols);

    if (show_extras) {
        print_centered(top, BOLD + s.title + RESET, cols);
        print_centered(top + ROW_STATUS, s.status, cols);
        if (show_keys)
            for (size_t i = 0; i < s.keys.size(); ++i) print_centered(top + ROW_KEYS + i, DIM + s.keys[i] + RESET, cols);
        if (history_lines) print_centered(top + ROW_HISTORY, string(BOLD) + "History" + RESET, cols);
        for (int i = 0; i < history_shown; ++i) {  // newest first
            print_centered(top + ROW_HISTORY + 1 + i, s.history[s.history.size() - 1 - i], cols);
        }
        if (history_truncated) print_centered(top + ROW_HISTORY + 1 + history_shown, DIM + string("...") + RESET, cols);
    }
    fflush(stdout);
}

// ============================================================================
// 4. Timer and main loop
// ============================================================================

enum Phase { WORK, SHORT_BREAK, LONG_BREAK };

struct Timer {
    Phase phase = WORK;
    int pomodoros_done = 0;
    int long_break_every = 4;
    double total = 0;        // seconds this timer was set to
    double left = 0;         // seconds remaining
    double ran = 0;          // seconds actually run (for the history log)
    bool running = true;     // false = paused or waiting to start
    bool waiting = false;    // finished; the next timer is queued but not started
    vector<string> history;  // log of finished timers, oldest first

    void start_phase(Phase p, bool start_now) {
        phase = p;
        const char *setting = p == WORK ? "work" : p == SHORT_BREAK ? "short" : "long";
        total = left = 60.0 * stoi(cfg[setting]);
        ran = 0;
        running = start_now;
        waiting = !start_now;
    }

    string phase_name() const {
        if (phase == WORK) return "Pomodoro #" + to_string(pomodoros_done + 1);
        return phase == SHORT_BREAK ? "Short break" : "Long break";
    }

    // Adds the current timer to the history before it's replaced.
    void log(bool ended_early) {
        char when[8];
        time_t now = time(nullptr);
        strftime(when, sizeof when, "%H:%M", localtime(&now));
        int seconds = (int)ran;
        char entry[80];
        snprintf(entry, sizeof entry, "%s  %-13s %3d:%02d%s", when, phase_name().c_str(), seconds / 60,
                 seconds % 60, ended_early ? "  (ended early)" : "");
        history.push_back(entry);
    }

    // Finishes a pomodoro and moves to the right kind of break.
    void go_to_break(bool start_now) {
        ++pomodoros_done;
        bool long_break = pomodoros_done % long_break_every == 0;
        if (long_break) notify("Pomodoro done. Time for a long break.", cfg["sound_long_break"]);
        else notify("Pomodoro done. Take a short break.", cfg["sound_work_done"]);
        start_phase(long_break ? LONG_BREAK : SHORT_BREAK, start_now);
    }

    void go_to_work(bool start_now) { start_phase(WORK, start_now); }

    // Called when the time runs out: log it, alert, and queue the next timer.
    void finish() {
        log(false);
        if (phase == WORK) {
            go_to_break(false);
        } else {
            notify("Break over. Back to work.", cfg["sound_break_done"]);
            go_to_work(false);
        }
    }

    // Skips ahead with b / w. A timer that was actually running is logged as ended early.
    void skip_to_break() {
        if (phase != WORK) return;
        if (!waiting) log(true);
        go_to_break(true);
    }

    void skip_to_work() {
        if (phase == WORK) return;
        if (!waiting) log(true);
        go_to_work(true);
    }
};

// The prompt shown after pressing + or s, while the user types a number.
struct NumberPrompt {
    bool active = false;
    bool sets_length = false;  // s: set the timer to n minutes; +: add n minutes
    string digits;

    void open(bool set_length) {
        active = true;
        sets_length = set_length;
        digits.clear();
    }

    string text() const {
        return string(sets_length ? "Set minutes: " : "Add minutes: ") + digits + "_   (Enter to " +
               (sets_length ? "set" : "add") + ", Esc to cancel)";
    }

    // Handles one key while the prompt is open.
    void handle_key(int key, Timer &timer) {
        bool is_enter = key == '\n' || key == '\r';
        bool is_backspace = key == 127 || key == 8;
        if (isdigit(key) && digits.size() < 4) {
            digits += (char)key;
        } else if (is_backspace && !digits.empty()) {
            digits.pop_back();
        } else if (is_enter) {
            if (!digits.empty()) {
                double seconds = 60.0 * stoi(digits);
                if (sets_length) timer.total = timer.left = seconds;
                else timer.left += seconds;
            }
            active = false;
        } else if (key == 27) {  // Esc
            active = false;
        }
    }
};

string status_line(const Timer &timer, const NumberPrompt &prompt) {
    if (prompt.active) return prompt.text();
    string status = timer.waiting ? "Ready — press space to start" : timer.running ? "Running" : "Paused";
    if (muted) status += "   [muted]";
    status += "   (" + to_string(timer.pomodoros_done) + " done, long break every " +
              to_string(timer.long_break_every) + ")";
    return status;
}

vector<string> key_hints(const Timer &timer) {
    string space_action = timer.running ? "pause" : timer.waiting ? "start" : "resume";
    string switch_key = timer.phase == WORK ? "b break" : "w work";
    return {
        "space " + space_action + "     1/5/0 +1/5/10m     + add n min     s set n min",
        switch_key + "    m " + (muted ? "unmute" : "mute") + "    v " + (show_bar ? "digits" : "bar") +
            "    t " + PROGRESS_INDICATOR_NAME + "    r restart    q quit",
    };
}

string rgb(int r, int g, int b) {
    char code[32];
    snprintf(code, sizeof code, "\033[38;2;%d;%d;%dm", r, g, b);
    return code;
}

Screen build_screen(const Timer &timer, const NumberPrompt &prompt, Clock::time_point now) {
    Screen s;
    bool work = timer.phase == WORK;
    s.title = work ? "POMODORO #" + to_string(timer.pomodoros_done + 1)
                   : timer.phase == SHORT_BREAK ? "SHORT BREAK" : "LONG BREAK";

    int seconds_left = (int)(timer.left + 0.999);  // round up so 0:00 only shows when it's over
    char time[16];
    snprintf(time, sizeof time, "%02d:%02d", seconds_left / 60, seconds_left % 60);
    s.time = time;

    s.status = status_line(timer, prompt);
    s.color = work ? "\033[31m" : "\033[32m";  // red for work, green for breaks
    if (timer.waiting) {
        // Pulse slowly so it's obvious pomo is waiting for space.
        const double period = 2.5;  // seconds
        double t = chrono::duration<double>(now.time_since_epoch()).count();
        double brightness = 0.6 + 0.4 * cos(t * 2 * M_PI / period);
        auto scaled = [&](int v) { return (int)(v * brightness); };
        s.color = work ? rgb(scaled(220), scaled(60), scaled(60)) : rgb(scaled(80), scaled(200), scaled(100));
        s.status = rgb(scaled(230), scaled(230), scaled(230)) + s.status + RESET;
    } else if (!timer.running) {
        s.color = DIM + s.color;  // paused
    }

    s.keys = key_hints(timer);
    s.history = timer.history;
    s.progress = timer.ran / max(1.0, timer.ran + timer.left);
    return s;
}

// Handles one key press outside the number prompt. Returns false to quit.
bool handle_key(int key, Timer &timer, NumberPrompt &prompt) {
    switch (key) {
        case '+': case '=': prompt.open(false); break;
        case 's': prompt.open(true); break;
        case '1': timer.left += 60; break;
        case '5': timer.left += 5 * 60; break;
        case '0': timer.left += 10 * 60; break;
        case ' ': case '\n': case '\r': case 'p':
            timer.running = !timer.running;
            timer.waiting = false;
            break;
        case 'r':
            timer.left = timer.total;
            timer.ran = 0;
            break;
        case 'b': timer.skip_to_break(); break;
        case 'w': timer.skip_to_work(); break;
        case 'm': muted = !muted; break;
        case 'v': show_bar = !show_bar; break;
        case 't':
            show_progress_indicator = !show_progress_indicator;
            if (!show_progress_indicator) hide_progress();
            break;
        case 'q':
        case 3:  // Ctrl-C (on Windows it arrives as a key, not a signal)
            if (show_progress_indicator) hide_progress();
            return false;
    }
    return true;
}

void start_minimized() {
    focused = false;
    sleep_ms(200);  // give the terminal a moment to apply the window title
    minimize_window();
}

#ifndef POMO_TESTING  // the tests (tests/) provide their own main
int main(int argc, char **argv) {
    load_config();
    parse_args(argc, argv);
    if (!validate_config()) return 1;

    setup_terminal();
    set_window_title(window_title());
    if (cfg["minimized"] == "yes") start_minimized();
    show_progress_indicator = cfg["topbar"] == "yes";

    Timer timer;
    timer.long_break_every = stoi(cfg["every"]);
    timer.go_to_work(true);
    NumberPrompt prompt;
    auto last_tick = Clock::now();

    for (;;) {
        auto now = Clock::now();
        if (timer.running) {
            double elapsed = chrono::duration<double>(now - last_tick).count();
            timer.left -= elapsed;
            timer.ran += elapsed;
        }
        last_tick = now;
        if (timer.left <= 0) timer.finish();

        draw_screen(build_screen(timer, prompt, now), now);
        if (show_progress_indicator) {
            ProgressState state = !timer.running ? ProgressState::PAUSED
                                  : timer.phase == WORK ? ProgressState::WORK
                                                        : ProgressState::BREAK;
            show_progress(timer.ran / max(1.0, timer.ran + timer.left), state);
        }

        // Wait for a key, redrawing at least every 200 ms (50 ms while pulsing).
        int key = read_key(timer.waiting ? 50 : 200);
        if (key == -1) continue;  // no key (or just a focus change)
        if (key == -2) break;     // input closed

        if (prompt.active) prompt.handle_key(key, timer);
        else if (!handle_key(key, timer, prompt)) return 0;
    }
}
#endif
