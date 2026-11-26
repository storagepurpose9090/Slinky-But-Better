#pragma once

#include <cstdint>

namespace slinky::time {

// High-resolution monotonic clock in seconds via QueryPerformanceCounter - matches sub_18006E500
double now_seconds() noexcept;

// Cached per-tick/per-frame timestamp updated via update_cached_time() - matches qword_180314520 / sub_18002C050
extern double g_cached_time_sec;

inline void update_cached_time() noexcept {
    g_cached_time_sec = now_seconds();
}

struct CooldownTimer {
    double last_trigger_sec = 0.0;

    bool has_elapsed(double duration_sec, double cur_time = g_cached_time_sec) const noexcept {
        return (cur_time - last_trigger_sec) >= duration_sec;
    }

    void reset(double cur_time = g_cached_time_sec) noexcept {
        last_trigger_sec = cur_time;
    }
};

} // namespace slinky::time
