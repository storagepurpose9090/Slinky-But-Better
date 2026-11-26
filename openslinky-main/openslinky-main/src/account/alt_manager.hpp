#pragma once

#include <cstddef>
#include <cstdint>

namespace slinky {
namespace events {
struct JniContext;
}
}

namespace slinky::account {

// Loads the saved cracked-account username from `%USERPROFILE%\.slinky\account.txt`.
// Safe to call more than once.
void initialize() noexcept;

// Username that is applied to the live Minecraft session (empty if not set yet).
const char* get_username() noexcept;

// Applies the saved username to the running Minecraft session. If no username is
// saved yet, reads the current session name and persists it instead.
// Returns true when the session matches the saved username.
bool apply_saved_to_game(events::JniContext* jni) noexcept;

// Validates `name`, saves it, and swaps the live Minecraft Session so the next
// server join uses this cracked account name.
// Returns 0 on success, 7001 invalid name, 7002 game unavailable/session not found,
// 7003 session swap failed, 7004 file write failed.
int set_username(const char* name, events::JniContext* jni) noexcept;

} // namespace slinky::account
