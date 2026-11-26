#pragma once

#include <cmath>
#include <cstdint>
#include "util/math.hpp"
#include "util/time.hpp"

namespace slinky::random {

// Global xoshiro128+ state (dword_180314510..0x31451C)
extern uint32_t g_rng_state[4];

// Seeds g_rng_state using high-resolution performance counter, system time, and process/thread IDs - matches sub_18002C000
void seed_rng() noexcept;

inline uint32_t rotl32(uint32_t x, int k) noexcept {
    return (x << k) | (x >> (32 - k));
}

// 32-bit xoshiro128+ step - matches sub_18002C070
inline uint32_t next_u32() noexcept {
    const uint32_t result = g_rng_state[0] + g_rng_state[3];
    const uint32_t t = g_rng_state[1] << 9;

    g_rng_state[2] ^= g_rng_state[0];
    g_rng_state[3] ^= g_rng_state[1];
    g_rng_state[1] ^= g_rng_state[2];
    g_rng_state[0] ^= g_rng_state[3];

    g_rng_state[2] ^= t;
    g_rng_state[3] = rotl32(g_rng_state[3], 11);
    return result;
}

// Uniform float in [min_val, max_val) with 24-bit mantissa precision - matches sub_18002C0D0
inline float uniform_range(math::Range2f range) noexcept {
    constexpr float kInv24 = 0.000000059604644775390625f; // 2^-24
    const float unit = static_cast<float>(next_u32() >> 8) * kInv24;
    return unit * (range.max - range.min) + range.min;
}

inline float uniform_float(float min_val, float max_val) noexcept {
    return uniform_range({min_val, max_val});
}

inline int uniform_int(int min_inclusive, int max_inclusive) noexcept {
    if (max_inclusive <= min_inclusive) {
        return min_inclusive;
    }
    const uint32_t span = static_cast<uint32_t>(max_inclusive - min_inclusive + 1);
    return min_inclusive + static_cast<int>(next_u32() % span);
}

// Bernoulli trial returning true with probability `chance` in [0, 1] - matches sub_18002C150
inline bool roll_chance(float chance) noexcept {
    return chance > uniform_float(0.0f, 1.0f);
}

// Smooth 1D interpolated drift noise - matches sub_18002C200 / sub_18002C3C0 / sub_18002C560 / sub_18002C720
struct DriftNoise {
    double timer = 0.0;
    float prev_unit = 0.0f;
    float next_unit = 0.0f;

    void reseed(bool reset_both) noexcept {
        do {
            if (reset_both) {
                prev_unit = uniform_float(0.0f, 1.0f);
            }
            next_unit = uniform_float(0.0f, 1.0f);
        } while (std::fabs(prev_unit - next_unit) < 0.2f);
    }

    // Wall-clock driven drift in [range.min, range.max] over `period_sec` - matches sub_18002C200
    float sample_timed(math::Range2f range, double period_sec) noexcept {
        if (time::g_cached_time_sec > timer + period_sec) {
            timer = time::g_cached_time_sec;
            if (prev_unit == 0.0f && next_unit == 0.0f) {
                reseed(true);
            } else {
                prev_unit = next_unit;
                reseed(false);
            }
        }
        const double alpha = period_sec > 0.0 ? (time::g_cached_time_sec - timer) / period_sec : 0.0;
        const float unit = static_cast<float>(static_cast<double>(next_unit - prev_unit) * alpha + prev_unit);
        return (range.max - range.min) * unit + range.min;
    }

    // Delta-time accumulated drift - matches sub_18002C560
    float sample_dt(math::Range2f range, double period_sec, double dt) noexcept {
        timer += dt;
        if (timer > period_sec) {
            timer = 0.0;
            if (prev_unit == 0.0f && next_unit == 0.0f) {
                reseed(true);
            } else {
                prev_unit = next_unit;
                reseed(false);
            }
        }
        const double alpha = period_sec > 0.0 ? timer / period_sec : 0.0;
        const float unit = static_cast<float>(static_cast<double>(next_unit - prev_unit) * alpha + prev_unit);
        return (range.max - range.min) * unit + range.min;
    }

    // Bernoulli trial modulated by timed drift - matches sub_18002C720
    bool sample_chance_timed(math::Range2f chance_range, double period_sec) noexcept {
        const float threshold = uniform_float(0.0f, 1.0f);
        return sample_timed(chance_range, period_sec) > threshold;
    }
};

// Marsaglia polar standard normal N(0, 1) generator - matches sub_18002C7F0
struct NormalGenerator {
    float cached_value = 0.0f;
    bool has_cached = false;

    float next_standard_normal() noexcept {
        if (has_cached) {
            has_cached = false;
            return cached_value;
        }
        float u = 0.0f;
        float v = 0.0f;
        float s = 0.0f;
        do {
            u = uniform_float(0.0f, 1.0f) * 2.0f - 1.0f;
            v = uniform_float(0.0f, 1.0f) * 2.0f - 1.0f;
            s = u * u + v * v;
        } while (s >= 1.0f || s == 0.0f);

        const float mul = std::sqrt(-2.0f * std::log(s) / s);
        cached_value = v * mul;
        has_cached = true;
        return u * mul;
    }
};

} // namespace slinky::random
