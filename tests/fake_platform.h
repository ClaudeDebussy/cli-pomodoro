// The fake platform's recorded state, which the tests set up and inspect.
#pragma once
#include <deque>
#include <string>
#include <vector>

#include "../platform.h"

struct FakePlatform {
    // Set by the tests
    TerminalSize size{80, 30};
    std::deque<int> keys;     // what read_key() returns, in order
    std::string config_path = "/nonexistent/pomo/config";

    // Recorded from pomo
    std::vector<std::string> sounds;
    std::vector<std::string> notifications;
    bool last_notification_sticky = false;
    int minimize_calls = 0;
    int restore_calls = 0;
    bool progress_shown = false;
    double progress_fraction = 0;
    ProgressState progress_state = ProgressState::PAUSED;
    int hide_progress_calls = 0;
    std::string window_title;

    // Back to a fresh state, including the global `focused`.
    void reset();
};

extern FakePlatform fake;
