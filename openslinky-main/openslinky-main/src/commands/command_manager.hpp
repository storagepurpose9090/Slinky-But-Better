#pragma once

#include <cstddef>
#include <cstdint>
#include "jni.h"
#include "events/events.hpp"

namespace slinky {

namespace minecraft {
// Forward declarations implemented in src/minecraft/mc_wrappers.cpp & game_helpers.cpp
jstring packet_chat_get_message(events::JniContext* jni, jobject chat_packet) noexcept;
void packet_chat_set_message(events::JniContext* jni, jobject chat_packet, jstring message) noexcept;
jstring packet_tab_complete_get_message(events::JniContext* jni, jobject tab_packet) noexcept;
void print_client_chat(events::JniContext* jni, const char* fmt, ...) noexcept;
}

namespace commands {

enum class CommandArgType : uint32_t {
    None    = 0,
    String  = 1, // "string"
    Integer = 2, // "integer"
    Boolean = 3, // "yes/no" (y/t -> true, n/f -> false)
};

enum CommandArgFlags : uint32_t {
    kCommandArgRequired = 0,
    kCommandArgOptional = 1u << 0,
};

struct CommandArgValue {
    union {
        struct {
            const char* data;
            size_t length;
        } str;
        uint64_t integer;
        bool boolean;
    };
};
static_assert(sizeof(CommandArgValue) == 16, "CommandArgValue must be 16 bytes");

struct CommandArgSpec {
    const char* name = nullptr;                        // +0x00 (0)
    CommandArgType type = CommandArgType::None;        // +0x08 (8)
    uint32_t flags = kCommandArgRequired;              // +0x0C (12)
    CommandArgValue default_value{};                   // +0x10..+0x1F (16..31)
};
static_assert(sizeof(CommandArgSpec) == 32, "CommandArgSpec must be 32 bytes");

using CommandHandlerFn = void (*)(events::GameContext* ctx, const CommandArgValue* args);

// Exact 128-byte (0x80) command specification node - matches off_18016F730 & off_18016DAC8
struct CommandSpec {
    const char* name = nullptr;                 // +0x00 (0)
    const char* description = nullptr;          // +0x08 (8)
    CommandArgSpec args[3]{};                   // +0x10..+0x6F (16..111)
    CommandHandlerFn handler = nullptr;         // +0x70 (112)
    const CommandSpec* subcommands = nullptr;   // +0x78 (120) - null-terminated CommandSpec[]
};
static_assert(sizeof(CommandSpec) == 128, "CommandSpec must be 128 bytes");

// Simple non-owning string tokenizer - matches sub_1800305C0 .. sub_1800306A0
struct StringViewTokenizer {
    const char* data = nullptr;
    size_t length = 0;
    size_t cursor = 0;

    static StringViewTokenizer from_span(const char* ptr, size_t len) noexcept;
    static StringViewTokenizer from_cstr(const char* cstr) noexcept;
    const char* next_token(char delimiter, size_t* out_token_len) noexcept;
    bool has_remaining() const noexcept { return cursor < length; }
};
static_assert(sizeof(StringViewTokenizer) == 24, "StringViewTokenizer must be 24 bytes");

// Initializes the root `!` command table (`!f`, `!gui`) - matches sub_1800304C0
int initialize_commands(const CommandSpec& friends_cmd, const CommandSpec& gui_cmd) noexcept;

// Frees the root command table - matches sub_1800305A0
void shutdown_commands() noexcept;

// Intercepts outgoing C01PacketChatMessage starting with `!` (handles `!!` escape) - matches sub_1800306C0
bool handle_outgoing_chat_packet(events::GameContext* ctx, jobject chat_packet) noexcept;

// Suppresses C14PacketTabComplete when input starts with `!` - matches sub_180030DD0
bool should_cancel_tab_complete_packet(events::GameContext* ctx, jobject tab_complete_packet) noexcept;

} // namespace commands
} // namespace slinky
