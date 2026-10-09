// Unit tests for pomo.cpp, run against the fake platform in fake_platform.cpp.
// Build and run with: make test
//
// pomo.cpp is a single file with no header, so the tests include it directly
// (with POMO_TESTING set, which leaves out its main).
#define POMO_TESTING
#include "../pomo.cpp"

#include <cstring>
#include <functional>
#include <unistd.h>

#include "fake_platform.h"

// ============================================================================
// A tiny test framework
// ============================================================================

struct TestCase {
    const char *name;
    function<void()> run;
};
vector<TestCase> &all_tests() {
    static vector<TestCase> tests;
    return tests;
}
struct RegisterTest {
    RegisterTest(const char *name, function<void()> run) { all_tests().push_back({name, run}); }
};
#define TEST(name)                                     \
    void test_##name();                                \
    RegisterTest register_##name(#name, test_##name); \
    void test_##name()

int failures = 0;
const char *current_test = "";

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            ++failures;                                                                        \
            fprintf(stderr, "FAIL %s (%s:%d): %s\n", current_test, __FILE__, __LINE__, #condition); \
        }                                                                                      \
    } while (0)

#define CHECK_EQ(actual, expected)                                                             \
    do {                                                                                       \
        auto actual_value = (actual);                                                          \
        auto expected_value = (expected);                                                      \
        if (!(actual_value == expected_value)) {                                               \
            ++failures;                                                                        \
            fprintf(stderr, "FAIL %s (%s:%d): %s == %s\n", current_test, __FILE__, __LINE__, #actual, #expected); \
        }                                                                                      \
    } while (0)

bool contains(const string &haystack, const string &needle) {
    return haystack.find(needle) != string::npos;
}

// pomo's settings and UI state are globals; put them back before each test.
const map<string, string> default_cfg = cfg;
void reset_state() {
    cfg = default_cfg;
    muted = false;
    show_bar = false;
    show_progress_indicator = true;
    fake.reset();
}

// A timer the way main() sets it up.
Timer new_timer() {
    Timer timer;
    timer.long_break_every = stoi(cfg["every"]);
    timer.go_to_work(true);
    return timer;
}

// ============================================================================
// A virtual screen: replays what draw_screen() prints and keeps the text.
// ============================================================================

struct VirtualScreen {
    vector<vector<string>> cells;  // [row][col], one UTF-8 character each

    VirtualScreen(int cols, int rows) : cells(rows, vector<string>(cols, " ")) {}

    void replay(const string &output) {
        int row = 1, col = 1;
        for (size_t i = 0; i < output.size();) {
            if (output[i] == '\033' && i + 1 < output.size() && output[i + 1] == '[') {
                // CSI: parameters, then a final letter
                size_t end = i + 2;
                while (end < output.size() && !isalpha((unsigned char)output[end])) ++end;
                string params = output.substr(i + 2, end - i - 2);
                char command = output[end];
                if (command == 'H') {
                    row = 1, col = 1;
                    sscanf(params.c_str(), "%d;%d", &row, &col);
                } else if (command == 'J') {
                    for (auto &line : cells) fill(line.begin(), line.end(), " ");
                }
                i = end + 1;  // colors (m) and anything else are ignored
            } else if (output[i] == '\033' && i + 1 < output.size() && output[i + 1] == ']') {
                // OSC (window title etc.): skip up to BEL
                while (i < output.size() && output[i] != '\a') ++i;
                ++i;
            } else {
                size_t length = 1;  // one UTF-8 character
                while (i + length < output.size() && (output[i + length] & 0xC0) == 0x80) ++length;
                if (row >= 1 && row <= (int)cells.size() && col >= 1 && col <= (int)cells[0].size())
                    cells[row - 1][col - 1] = output.substr(i, length);
                ++col;
                i += length;
            }
        }
    }

    string line(int row) const {  // 1-based, trailing spaces trimmed
        string text;
        for (const string &cell : cells[row - 1]) text += cell;
        return text.substr(0, text.find_last_not_of(' ') + 1);
    }

    string text() const {
        string all;
        for (int r = 1; r <= (int)cells.size(); ++r) all += line(r) + "\n";
        return all;
    }

    int count_lines_containing(const string &needle) const {
        int count = 0;
        for (int r = 1; r <= (int)cells.size(); ++r) count += contains(line(r), needle);
        return count;
    }
};

// Runs `print` with stdout sent to a temporary file and returns what it printed.
string capture_stdout(const function<void()> &print) {
    fflush(stdout);
    FILE *capture = tmpfile();
    int saved = dup(STDOUT_FILENO);
    dup2(fileno(capture), STDOUT_FILENO);
    print();
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);

    string output;
    rewind(capture);
    char buffer[4096];
    for (size_t n; (n = fread(buffer, 1, sizeof buffer, capture)) > 0;) output.append(buffer, n);
    fclose(capture);
    return output;
}

// The time the drawing tests pretend it is. Only ever moves forward.
Clock::time_point test_clock = Clock::now();

// Draws the timer at the current fake.size, 10 seconds after the last drawing.
// Any size change is noticed 5 seconds earlier, so the "just resized" grace
// period is always over.
VirtualScreen draw(const Timer &timer, const NumberPrompt &prompt = NumberPrompt()) {
    test_clock += chrono::seconds(10);
    Clock::time_point now = test_clock;
    Screen screen = build_screen(timer, prompt, now);
    recently_resized(fake.size, now - chrono::seconds(5));  // notice a size change, 5 s ago
    string output = capture_stdout([&] { draw_screen(screen, now); });
    VirtualScreen virtual_screen(fake.size.cols, fake.size.rows);
    virtual_screen.replay(output);
    return virtual_screen;
}

// ============================================================================
// Configuration
// ============================================================================

TEST(config_file_is_read_and_trimmed) {
    char path[] = "/tmp/pomo-test-config-XXXXXX";
    int fd = mkstemp(path);
    string contents = "# a comment\n\nwork = 50\n  short=10  \nnot a setting\nnotify = none\n";
    CHECK(write(fd, contents.data(), contents.size()) == (ssize_t)contents.size());
    close(fd);
    fake.config_path = path;

    load_config();
    CHECK_EQ(cfg["work"], string("50"));
    CHECK_EQ(cfg["short"], string("10"));
    CHECK_EQ(cfg["notify"], string("none"));
    CHECK_EQ(cfg["long"], string("15"));  // untouched default
    unlink(path);
}

TEST(missing_config_file_keeps_defaults) {
    load_config();
    CHECK(cfg == default_cfg);
}

TEST(validate_config_accepts_whole_numbers) {
    CHECK(validate_config());
    cfg["work"] = "1";
    CHECK(validate_config());
}

TEST(validate_config_rejects_bad_numbers) {
    for (const char *bad : {"0", "-5", "abc", "2.5", ""}) {
        reset_state();
        cfg["work"] = bad;
        // validate_config prints why to stderr; keep the test output clean
        FILE *saved = stderr;
        stderr = fopen("/dev/null", "w");
        bool valid = validate_config();
        fclose(stderr);
        stderr = saved;
        CHECK(!valid);
    }
}

TEST(command_line_options) {
    const char *argv[] = {"pomo", "-w", "50", "-s10", "-l", "20", "-e", "3", "-n", "none", "-m"};
    parse_args(11, (char **)argv);
    CHECK_EQ(cfg["work"], string("50"));
    CHECK_EQ(cfg["short"], string("10"));
    CHECK_EQ(cfg["long"], string("20"));
    CHECK_EQ(cfg["every"], string("3"));
    CHECK_EQ(cfg["notify"], string("none"));
    CHECK_EQ(cfg["minimized"], string("yes"));
}

// ============================================================================
// Timer
// ============================================================================

TEST(starts_with_a_running_pomodoro) {
    Timer timer = new_timer();
    CHECK_EQ(timer.phase, WORK);
    CHECK_EQ(timer.left, 25 * 60.0);
    CHECK(timer.running);
    CHECK(!timer.waiting);
}

TEST(finished_pomodoro_queues_a_short_break) {
    Timer timer = new_timer();
    timer.ran = 25 * 60;
    timer.finish();
    CHECK_EQ(timer.phase, SHORT_BREAK);
    CHECK_EQ(timer.left, 5 * 60.0);
    CHECK(timer.waiting);
    CHECK(!timer.running);
    CHECK_EQ(timer.pomodoros_done, 1);
    CHECK_EQ(fake.sounds, vector<string>{"work_done.wav"});
    CHECK_EQ(timer.history.size(), (size_t)1);
    CHECK(contains(timer.history[0], "Pomodoro #1"));
    CHECK(contains(timer.history[0], " 25:00"));
    CHECK(!contains(timer.history[0], "ended early"));
}

TEST(every_fourth_pomodoro_gets_a_long_break) {
    Timer timer = new_timer();
    for (int i = 0; i < 3; ++i) {
        timer.finish();  // work -> short break
        CHECK_EQ(timer.phase, SHORT_BREAK);
        timer.finish();  // break -> work
        CHECK_EQ(timer.phase, WORK);
    }
    timer.finish();
    CHECK_EQ(timer.phase, LONG_BREAK);
    CHECK_EQ(timer.left, 15 * 60.0);
    CHECK_EQ(fake.sounds.back(), string("long_break.wav"));
}

TEST(finished_break_queues_work) {
    Timer timer = new_timer();
    timer.finish();
    timer.finish();
    CHECK_EQ(timer.phase, WORK);
    CHECK(timer.waiting);
    CHECK_EQ(fake.sounds.back(), string("break_done.wav"));
    CHECK(contains(timer.history[1], "Short break"));
}

TEST(skipping_a_running_timer_logs_it_as_ended_early) {
    Timer timer = new_timer();
    timer.ran = 90;
    timer.skip_to_break();
    CHECK_EQ(timer.phase, SHORT_BREAK);
    CHECK(timer.running);  // b starts the break right away
    CHECK_EQ(timer.history.size(), (size_t)1);
    CHECK(contains(timer.history[0], "1:30  (ended early)"));
}

TEST(skipping_a_waiting_timer_logs_nothing) {
    Timer timer = new_timer();
    timer.finish();  // short break, waiting
    timer.skip_to_work();
    CHECK_EQ(timer.phase, WORK);
    CHECK_EQ(timer.history.size(), (size_t)1);  // just the finished pomodoro
}

TEST(skip_to_the_phase_youre_in_does_nothing) {
    Timer timer = new_timer();
    timer.skip_to_work();
    CHECK_EQ(timer.phase, WORK);
    CHECK(timer.history.empty());
}

// ============================================================================
// Notifications
// ============================================================================

TEST(focused_window_gets_a_sound_but_no_desktop_notification) {
    notify("done", "x.wav");
    CHECK_EQ(fake.sounds, vector<string>{"x.wav"});
    CHECK(fake.notifications.empty());
}

TEST(unfocused_window_gets_a_desktop_notification) {
    focused = false;
    notify("done", "x.wav");
    CHECK_EQ(fake.sounds.size(), (size_t)1);
    CHECK_EQ(fake.notifications, vector<string>{"done"});
    CHECK(!fake.last_notification_sticky);
    CHECK_EQ(fake.restore_calls, 0);
}

TEST(muted_plays_no_sound) {
    muted = true;
    focused = false;
    notify("done", "x.wav");
    CHECK(fake.sounds.empty());
    CHECK_EQ(fake.notifications.size(), (size_t)1);
}

TEST(notify_modes) {
    focused = false;
    cfg["notify"] = "desktop";
    notify("a", "x.wav");
    CHECK(fake.sounds.empty());
    CHECK_EQ(fake.notifications.size(), (size_t)1);

    reset_state();
    focused = false;
    cfg["notify"] = "sound";
    notify("a", "x.wav");
    CHECK_EQ(fake.sounds.size(), (size_t)1);
    CHECK(fake.notifications.empty());

    reset_state();
    focused = false;
    cfg["notify"] = "none";
    notify("a", "x.wav");
    CHECK(fake.sounds.empty());
    CHECK(fake.notifications.empty());
}

TEST(minimized_mode_restores_the_window_with_a_sticky_notification) {
    focused = false;
    cfg["minimized"] = "yes";
    cfg["notify"] = "none";
    notify("done", "x.wav");
    CHECK_EQ(fake.restore_calls, 1);
    CHECK_EQ(fake.notifications.size(), (size_t)1);  // even with notify = none
    CHECK(fake.last_notification_sticky);
}

// ============================================================================
// Keys
// ============================================================================

TEST(space_pauses_and_resumes) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    handle_key(' ', timer, prompt);
    CHECK(!timer.running);
    handle_key(' ', timer, prompt);
    CHECK(timer.running);
}

TEST(space_starts_a_waiting_timer) {
    Timer timer = new_timer();
    timer.finish();
    NumberPrompt prompt;
    handle_key(' ', timer, prompt);
    CHECK(timer.running);
    CHECK(!timer.waiting);
}

TEST(number_keys_add_minutes) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    handle_key('1', timer, prompt);
    handle_key('5', timer, prompt);
    handle_key('0', timer, prompt);
    CHECK_EQ(timer.left, (25 + 1 + 5 + 10) * 60.0);
}

TEST(r_restarts_the_timer) {
    Timer timer = new_timer();
    timer.left = 100;
    timer.ran = 1400;
    NumberPrompt prompt;
    handle_key('r', timer, prompt);
    CHECK_EQ(timer.left, 25 * 60.0);
    CHECK_EQ(timer.ran, 0.0);
}

TEST(toggle_keys) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    handle_key('m', timer, prompt);
    CHECK(muted);
    handle_key('v', timer, prompt);
    CHECK(show_bar);
    handle_key('t', timer, prompt);
    CHECK(!show_progress_indicator);
    CHECK_EQ(fake.hide_progress_calls, 1);
    handle_key('t', timer, prompt);
    CHECK(show_progress_indicator);
}

TEST(q_and_ctrl_c_quit) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    CHECK(handle_key('x', timer, prompt));  // unknown keys are ignored
    CHECK(!handle_key('q', timer, prompt));
    CHECK_EQ(fake.hide_progress_calls, 1);
    CHECK(!handle_key(3, timer, prompt));
}

TEST(s_sets_the_timer_length) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    handle_key('s', timer, prompt);
    CHECK(prompt.active);
    for (int key : {'1', '2', '\r'}) prompt.handle_key(key, timer);
    CHECK(!prompt.active);
    CHECK_EQ(timer.total, 12 * 60.0);
    CHECK_EQ(timer.left, 12 * 60.0);
}

TEST(plus_adds_minutes) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    handle_key('+', timer, prompt);
    for (int key : {(int)'3', (int)'9', 127, (int)'\n'}) prompt.handle_key(key, timer);  // 127 = backspace
    CHECK_EQ(timer.left, (25 + 3) * 60.0);
    CHECK_EQ(timer.total, 25 * 60.0);
}

TEST(prompt_esc_cancels_and_input_is_limited_to_four_digits) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    prompt.open(true);
    for (int key : {'1', '2', '3', '4', '5'}) prompt.handle_key(key, timer);
    CHECK_EQ(prompt.digits, string("1234"));
    prompt.handle_key(27, timer);
    CHECK(!prompt.active);
    CHECK_EQ(timer.left, 25 * 60.0);
}

TEST(key_hints_and_status_line) {
    Timer timer = new_timer();
    NumberPrompt prompt;
    vector<string> hints = key_hints(timer);
    CHECK(contains(hints[0], "space pause"));
    CHECK(contains(hints[1], "b break"));
    CHECK(contains(hints[1], "t top bar"));
    muted = true;
    CHECK(contains(status_line(timer, prompt), "[muted]"));
    prompt.open(false);
    CHECK(contains(status_line(timer, prompt), "Add minutes: _"));
}

// ============================================================================
// Drawing
// ============================================================================

TEST(visible_width_skips_colors_and_counts_characters) {
    CHECK_EQ(visible_width("abc"), 3);
    CHECK_EQ(visible_width(string(BOLD) + "abc" + RESET), 3);
    CHECK_EQ(visible_width("█▀▄"), 3);
    CHECK_EQ(visible_width("Ready — go"), 10);
}

TEST(digit_bitmap_size) {
    vector<vector<bool>> bitmap = digit_bitmap("25:00");
    CHECK_EQ(bitmap.size(), (size_t)FONT_ROWS);
    CHECK_EQ(bitmap[0].size(), (size_t)(4 * 5 + 3 + 4));  // four digits, a colon, four gaps
    CHECK(bitmap[0][0]);    // top of the 2
    CHECK(!bitmap[1][0]);   // the 2's open left side
}

TEST(normal_layout_shows_everything) {
    Timer timer = new_timer();
    timer.finish();
    VirtualScreen screen = draw(timer);
    CHECK(contains(screen.text(), "SHORT BREAK"));
    CHECK(contains(screen.text(), "Ready — press space to start"));
    CHECK(contains(screen.text(), "space start"));
    CHECK(contains(screen.text(), "History"));
    CHECK(contains(screen.text(), "Pomodoro #1"));
    CHECK_EQ(screen.count_lines_containing("█"), FONT_ROWS);
}

TEST(digits_are_centered) {
    Timer timer = new_timer();
    VirtualScreen screen = draw(timer);
    for (int r = 1; r <= fake.size.rows; ++r) {
        string line = screen.line(r);
        if (!contains(line, "█")) continue;
        int left_margin = line.find_first_not_of(' ');
        int width = visible_width(line) - left_margin;
        int right_margin = fake.size.cols - left_margin - width;
        CHECK(abs(left_margin - right_margin) <= 3);  // the last glyph's trailing gap is blank
    }
}

TEST(unfocused_shows_only_the_timer) {
    Timer timer = new_timer();
    focused = false;
    VirtualScreen screen = draw(timer);
    CHECK(!contains(screen.text(), "POMODORO"));
    CHECK(!contains(screen.text(), "Running"));
    CHECK_EQ(screen.count_lines_containing("█"), FONT_ROWS);
}

TEST(unfocused_but_just_resized_shows_everything) {
    Timer timer = new_timer();
    focused = false;
    fake.size = {90, 31};
    test_clock += chrono::seconds(10);
    Clock::time_point now = test_clock;
    recently_resized(fake.size, now);
    Screen s = build_screen(timer, NumberPrompt(), now);
    VirtualScreen screen(fake.size.cols, fake.size.rows);
    screen.replay(capture_stdout([&] { draw_screen(s, now + chrono::milliseconds(500)); }));
    CHECK(contains(screen.text(), "POMODORO #1"));
}

TEST(narrow_window_shows_only_bigger_digits) {
    Timer timer = new_timer();
    fake.size = {50, 20};
    VirtualScreen screen = draw(timer);
    CHECK(!contains(screen.text(), "POMODORO"));
    CHECK(screen.count_lines_containing("█") > FONT_ROWS);  // scaled up
}

TEST(short_window_shows_ellipsis_for_history_that_doesnt_fit) {
    Timer timer = new_timer();
    for (int i = 0; i < 3; ++i) timer.finish();
    fake.size = {80, 18};  // room for exactly one history line
    VirtualScreen screen = draw(timer);
    // The one line under the heading is "...", not an entry.
    int heading = 0;
    for (int r = 1; r <= fake.size.rows; ++r)
        if (contains(screen.line(r), "History")) heading = r;
    CHECK(heading > 0 && heading < fake.size.rows);
    if (heading > 0 && heading < fake.size.rows) CHECK(contains(screen.line(heading + 1), "..."));
    CHECK_EQ(screen.count_lines_containing("Pomodoro #"), 0);
    CHECK_EQ(screen.count_lines_containing("Short break"), 0);
}

TEST(history_that_fits_has_no_ellipsis) {
    Timer timer = new_timer();
    for (int i = 0; i < 3; ++i) timer.finish();
    fake.size = {80, 20};  // room for three history lines
    VirtualScreen screen = draw(timer);
    CHECK_EQ(screen.count_lines_containing("..."), 0);
    CHECK_EQ(screen.count_lines_containing("Pomodoro #"), 2);  // two logged pomodoros
}

TEST(key_hints_hidden_at_12_rows_or_less) {
    Timer timer = new_timer();
    fake.size = {80, 12};
    CHECK(!contains(draw(timer).text(), "q quit"));
    fake.size = {80, 13};
    CHECK(contains(draw(timer).text(), "q quit"));
}

TEST(bar_view_shows_progress_without_digits) {
    Timer timer = new_timer();
    timer.ran = 12.5 * 60;
    timer.left = 12.5 * 60;
    show_bar = true;
    VirtualScreen screen = draw(timer);
    CHECK_EQ(screen.count_lines_containing("░"), FONT_ROWS);
    // half done: about as many filled cells as empty ones
    for (int r = 1; r <= fake.size.rows; ++r) {
        string line = screen.line(r);
        if (!contains(line, "░")) continue;
        int filled = 0, empty = 0;
        for (size_t i = 0; (i = line.find("█", i)) != string::npos; i += 3) ++filled;
        for (size_t i = 0; (i = line.find("░", i)) != string::npos; i += 3) ++empty;
        CHECK(abs(filled - empty) <= 1);
    }
}

// ============================================================================

int main() {
    for (const TestCase &test : all_tests()) {
        current_test = test.name;
        reset_state();
        test.run();
    }
    printf("%zu tests, %d failures\n", all_tests().size(), failures);
    return failures ? 1 : 0;
}
