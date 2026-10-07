// pomo - a dead simple pomodoro timer
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

using namespace std;
using Clock = chrono::steady_clock;

map<string, string> cfg = {
    {"work", "25"}, {"short", "5"}, {"long", "15"}, {"every", "4"},
    {"notify", "both"},  // sound | desktop | both | bell | none
    {"minimized", "no"},  // yes = start minimized, sticky alert when a timer ends
    {"sound_work_done", "/usr/share/sounds/freedesktop/stereo/complete.oga"},
    {"sound_break_done", "/usr/share/sounds/freedesktop/stereo/message.oga"},
    {"sound_long_break", "/usr/share/sounds/freedesktop/stereo/bell.oga"},
};

void load_config() {
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    string path = xdg ? string(xdg) + "/pomo/config" : string(home ? home : ".") + "/.config/pomo/config";
    ifstream f(path);
    for (string line; getline(f, line);) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == string::npos) continue;
        auto trim = [](string s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
            return s;
        };
        cfg[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
}

// Window title tag the GNOME extension uses to find our terminal window.
string tag = "pomo [" + to_string(getpid()) + "]";

// Call the pomo GNOME Shell extension (see extension/). Returns false if it isn't installed.
bool ext(const string &method, const string &args) {
    return system(("gdbus call --session -d org.gnome.Shell -o /org/gnome/Shell/Extensions/Pomo "
                   "-m org.gnome.Shell.Extensions.Pomo." + method + " -- '" + tag + "' " + args +
                   " >/dev/null 2>&1").c_str()) == 0;
}

// Whether our terminal has focus, from focus reports (ESC [ I / ESC [ O). Starts focused unless minimized.
bool focused = true;

void notify(const string &msg, const string &sound) {
    const string &n = cfg["notify"];
    if (n == "bell") printf("\a");
    if (n == "sound" || n == "both")
        (void)!system(("(pw-play '" + sound + "' || paplay '" + sound + "' || canberra-gtk-play -f '" + sound +
                       "' || printf '\\a') >/dev/null 2>&1 &").c_str());
    if (focused) return;  // sound always plays; the rest is pointless when you're looking at it
    bool sticky = cfg["minimized"] == "yes";
    if (sticky && !ext("Activate", "")) printf("\033[1t");  // fallback: xterm-style un-minimize
    if (n == "desktop" || n == "both" || sticky) {
        // the extension's notification raises our window when clicked; notify-send's can't on Wayland
        if (!ext("Notify", "'" + msg + "' " + (sticky ? "true" : "false")))
            (void)!system(("notify-send -a pomo " + string(sticky ? "-u critical " : "") + "'Pomodoro' '" + msg +
                           "' >/dev/null 2>&1 &").c_str());
    }
}

// ---- terminal ----
termios orig;
void restore() { printf("\033[?1004l\033[0m\033[?25h\033[?1049l"); fflush(stdout); tcsetattr(0, TCSANOW, &orig); }
void on_signal(int) { restore(); _exit(0); }
void setup_term() {
    tcgetattr(0, &orig);
    termios raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &raw);
    atexit(restore);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("\033[?1049h\033[?25l\033[?1004h");  // alt screen, hide cursor, report focus changes
}

// 5-row block font for 0-9 and ':'
const char *FONT[11][5] = {
    {"█████", "█   █", "█   █", "█   █", "█████"}, {"    █", "    █", "    █", "    █", "    █"},
    {"█████", "    █", "█████", "█    ", "█████"}, {"█████", "    █", "█████", "    █", "█████"},
    {"█   █", "█   █", "█████", "    █", "    █"}, {"█████", "█    ", "█████", "    █", "█████"},
    {"█████", "█    ", "█████", "█   █", "█████"}, {"█████", "    █", "    █", "    █", "    █"},
    {"█████", "█   █", "█████", "█   █", "█████"}, {"█████", "█   █", "█████", "    █", "█████"},
    {"   ", " █ ", "   ", " █ ", "   "},
};

void draw(const string &title, int secs, const string &status, const string &color, const string &keys,
          const vector<string> &hist) {
    winsize w{};
    ioctl(1, TIOCGWINSZ, &w);
    int cols = w.ws_col ? w.ws_col : 80, rows = w.ws_row ? w.ws_row : 24;
    char t[16];
    snprintf(t, sizeof t, "%02d:%02d", secs / 60, secs % 60);
    int width = 0;
    for (char *p = t; *p; ++p) width += (*p == ':' ? 3 : 5) + 1;
    int shown = min((int)hist.size(), max(0, rows - 14));  // history lines that fit
    int top = max(1, (rows - 11 - (shown ? shown + 2 : 0)) / 2);
    auto center = [&](int row, const string &s, int len) {
        printf("\033[%d;%dH%s", row, max(1, (cols - len) / 2 + 1), s.c_str());
    };
    printf("\033[H\033[2J");
    center(top, "\033[1m" + title + "\033[0m", title.size());
    for (int r = 0; r < 5; ++r) {
        string line = color;
        for (char *p = t; *p; ++p) line += string(FONT[*p == ':' ? 10 : *p - '0'][r]) + " ";
        center(top + 2 + r, line + "\033[0m", width);
    }
    center(top + 8, status, status.size());
    center(top + 10, "\033[2m" + keys + "\033[0m", keys.size());
    if (shown) center(top + 12, "\033[1mHistory\033[0m", 7);
    for (int i = 0; i < shown; ++i) {  // newest first
        const string &h = hist[hist.size() - 1 - i];
        center(top + 13 + i, h, h.size());
    }
    fflush(stdout);
}

void usage() {
    puts("usage: pomo [-w min] [-s min] [-l min] [-e n] [-n sound|desktop|both|bell|none] [-m]\nSee man pomo.");
    exit(1);
}

int main(int argc, char **argv) {
    load_config();
    for (int c; (c = getopt(argc, argv, "w:s:l:e:n:mh")) != -1;) {
        switch (c) {
            case 'w': cfg["work"] = optarg; break;
            case 's': cfg["short"] = optarg; break;
            case 'l': cfg["long"] = optarg; break;
            case 'e': cfg["every"] = optarg; break;
            case 'n': cfg["notify"] = optarg; break;
            case 'm': cfg["minimized"] = "yes"; break;
            default: usage();
        }
    }
    for (const char *k : {"work", "short", "long", "every"}) {
        char *end;
        long v = strtol(cfg[k].c_str(), &end, 10);
        if (*end || v < 1) { fprintf(stderr, "pomo: %s must be a whole number of at least 1, got '%s'\n", k, cfg[k].c_str()); return 1; }
    }
    int every = stoi(cfg["every"]);
    setup_term();
    printf("\033]0;%s\007", tag.c_str());  // window title, so the extension can find us
    fflush(stdout);
    if (cfg["minimized"] == "yes") {
        focused = false;
        usleep(200000);  // give the terminal a moment to apply the title
        if (!ext("Minimize", "")) printf("\033[2t");
    }

    enum { WORK, SHORT, LONG } phase = WORK;
    int done = 0;              // completed pomodoros
    vector<string> hist;       // log of finished timers
    double ran = 0;            // seconds actually run in this timer
    string adding;             // digits typed after '+', while entering a custom amount
    bool typing = false;
    bool running = true;       // false = paused or waiting to start
    bool waiting = false;      // break/work queued but not started yet
    double total, left;        // seconds
    auto set_phase = [&](decltype(phase) p, bool start) {
        phase = p;
        total = left = 60.0 * stoi(cfg[p == WORK ? "work" : p == SHORT ? "short" : "long"]);
        ran = 0;
        running = start;
        waiting = !start;
    };
    auto log = [&](bool early) {  // record the current timer before it's replaced
        char buf[80], when[8];
        time_t t = time(nullptr);
        strftime(when, sizeof when, "%H:%M", localtime(&t));
        string name = phase == WORK ? "Pomodoro #" + to_string(done + 1) : phase == SHORT ? "Short break" : "Long break";
        int r = (int)ran;
        snprintf(buf, sizeof buf, "%s  %-13s %3d:%02d%s", when, name.c_str(), r / 60, r % 60, early ? "  (ended early)" : "");
        hist.push_back(buf);
    };
    auto go_break = [&](bool start) {
        ++done;
        bool lng = done % every == 0;
        notify(lng ? "Pomodoro done. Time for a long break." : "Pomodoro done. Take a short break.",
               cfg[lng ? "sound_long_break" : "sound_work_done"]);
        set_phase(lng ? LONG : SHORT, start);
    };
    set_phase(WORK, true);
    auto last = Clock::now();

    for (;;) {
        auto now = Clock::now();
        if (running) {
            double dt = chrono::duration<double>(now - last).count();
            left -= dt;
            ran += dt;
        }
        last = now;

        if (left <= 0) {
            log(false);
            if (phase == WORK) go_break(false);
            else {
                notify("Break over. Back to work.", cfg["sound_break_done"]);
                set_phase(WORK, false);
            }
        }

        string title = phase == WORK ? "POMODORO #" + to_string(done + 1)
                     : phase == SHORT ? "SHORT BREAK" : "LONG BREAK";
        string color = phase == WORK ? "\033[31m" : "\033[32m";
        string status = typing ? "Add minutes: " + adding + "_   (Enter to add, Esc to cancel)"
                      : waiting ? "Ready — press space to start" : running ? "Running" : "Paused";
        if (!typing) status += "   (" + to_string(done) + " done, long break every " + to_string(every) + ")";
        if (!running) color = "\033[2m" + color;
        string keys = string("[space] ") + (running ? "pause" : waiting ? "start" : "resume") +
                      "  [1] +1m  [5] +5m  [0] +10m  [+] +n min  [r] restart  " +
                      (phase == WORK ? "[b] start break" : "[w] start work") + "  [q] quit";
        draw(title, (int)(left + 0.999), status, color, keys, hist);

        pollfd p{0, POLLIN, 0};
        if (poll(&p, 1, 200) > 0) {
            char k;
            if (read(0, &k, 1) != 1) break;
            if (k == 27) {  // escape sequence? focus reports are ESC [ I and ESC [ O
                char seq[2];
                pollfd q{0, POLLIN, 0};
                if (poll(&q, 1, 20) > 0 && read(0, seq, 2) == 2 && seq[0] == '[') {
                    if (seq[1] == 'I') focused = true;
                    if (seq[1] == 'O') focused = false;
                    continue;  // ignore other sequences (arrow keys etc.)
                }
            }
            if (typing) {
                if (isdigit((unsigned char)k) && adding.size() < 4) adding += k;
                else if ((k == 127 || k == 8) && !adding.empty()) adding.pop_back();
                else if (k == '\n' || k == '\r') { if (!adding.empty()) left += 60.0 * stoi(adding); typing = false; }
                else if (k == 27) typing = false;
                continue;
            }
            switch (k) {
                case '+': case '=': typing = true; adding.clear(); break;
                case '1': left += 60; break;
                case ' ': case '\n': case 'p': running = !running; waiting = false; break;
                case '5': left += 300; break;
                case '0': left += 600; break;
                case 'r': left = total; break;
                case 'b': if (phase == WORK) { if (!waiting) log(true); go_break(true); } break;
                case 'w': if (phase != WORK) { if (!waiting) log(true); set_phase(WORK, true); } break;
                case 'q': return 0;
            }
        }
    }
}
