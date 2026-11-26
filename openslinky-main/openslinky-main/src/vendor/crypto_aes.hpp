#pragma once

#include <cstddef>
#include <cstdint>

// NOTE: Isolated vendor cryptographic routines (Requirement 3 & 4 of TASK.md).
// Restored so readers can inspect the original licensing/telemetry AES-128 implementation.
// NEVER #include or call any symbol from `slinky::vendor` in the active client (`libslinky.dll`).

namespace slinky::vendor {

constexpr size_t kAesBlockSize = 16;
constexpr size_t kAes128KeySize = 16;
constexpr size_t kAes128ExpandedKeySize = 176;

struct Aes128Context {
    uint8_t round_keys[kAes128ExpandedKeySize]{};
    uint8_t iv[kAesBlockSize]{};
};

// Expands a 16-byte AES-128 key into 11 16-byte round keys (176 bytes) - matches sub_18006F3C0
void aes128_expand_key(uint8_t round_keys[kAes128ExpandedKeySize], const uint8_t key[kAes128KeySize]) noexcept;

// Initializes context with key and IV
void aes128_init_ctx_iv(
    Aes128Context* ctx,
    const uint8_t key[kAes128KeySize],
    const uint8_t iv[kAesBlockSize]) noexcept;

// Encrypts a single 16-byte block in-place - matches sub_18006F4D0
void aes128_encrypt_block(const uint8_t round_keys[kAes128ExpandedKeySize], uint8_t state[kAesBlockSize]) noexcept;

// Decrypts a single 16-byte block in-place - matches sub_18006FB70
void aes128_decrypt_block(const uint8_t round_keys[kAes128ExpandedKeySize], uint8_t state[kAesBlockSize]) noexcept;

// CBC buffer encryption/decryption in-place (`length` must be a multiple of 16)
void aes128_cbc_encrypt_buffer(Aes128Context* ctx, uint8_t* buffer, size_t length) noexcept;
void aes128_cbc_decrypt_buffer(Aes128Context* ctx, uint8_t* buffer, size_t length) noexcept;

} // namespace slinky::vendor
