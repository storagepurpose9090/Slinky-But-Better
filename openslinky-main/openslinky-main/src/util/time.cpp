#include "util/time.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace slinky::time {

double g_cached_time_sec = 0.0;

namespace {
double g_qpc_freq = 0.0;
} // namespace

double now_seconds() noexcept {
    if (g_qpc_freq == 0.0) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        g_qpc_freq = static_cast<double>(static_cast<int32_t>(freq.LowPart));
        if (g_qpc_freq <= 0.0) {
            g_qpc_freq = static_cast<double>(freq.QuadPart);
        }
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / g_qpc_freq;
}

} // namespace slinky::time
