#include "util/random.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>

namespace slinky::random {

uint32_t g_rng_state[4] = {
    0x6A09E667u,
    0xBB67AE85u,
    0x3C6EF372u,
    0xA54FF53Au,
};

namespace {
uint64_t splitmix64(uint64_t& x) noexcept {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
} // namespace

void seed_rng() noexcept {
    LARGE_INTEGER qpc{};
    FILETIME ft{};
    QueryPerformanceCounter(&qpc);
    GetSystemTimeAsFileTime(&ft);

    uint64_t seed = static_cast<uint64_t>(qpc.QuadPart);
    seed ^= (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    seed ^= static_cast<uint64_t>(__rdtsc());
    seed ^= (static_cast<uint64_t>(GetCurrentProcessId()) << 32) | GetCurrentThreadId();

    const uint64_t s0 = splitmix64(seed);
    const uint64_t s1 = splitmix64(seed);

    g_rng_state[0] = static_cast<uint32_t>(s0);
    g_rng_state[1] = static_cast<uint32_t>(s0 >> 32);
    g_rng_state[2] = static_cast<uint32_t>(s1);
    g_rng_state[3] = static_cast<uint32_t>(s1 >> 32);

    if ((g_rng_state[0] | g_rng_state[1] | g_rng_state[2] | g_rng_state[3]) == 0) {
        g_rng_state[0] = 1u;
    }
}

} // namespace slinky::random
