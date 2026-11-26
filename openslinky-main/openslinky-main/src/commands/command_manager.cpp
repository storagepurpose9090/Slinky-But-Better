#include "commands/command_manager.hpp"

#include <cstdlib>
#include <cstring>
#include "util/containers.hpp"

namespace slinky::commands {

namespace {

CommandSpec* g_root_subcommands = nullptr;

const CommandSpec kRootCommandSpec{
    "root",
    "All available commands",
    {},
    nullptr,
    nullptr,
};

constexpr const char* kArgTypeNames[3] = {
    "string",
    "integer",
    "yes/no",
};

} // namespace

StringViewTokenizer StringViewTokenizer::from_span(const char* ptr, size_t len) noexcept {
    return StringViewTokenizer{ptr, len, 0};
}

StringViewTokenizer StringViewTokenizer::from_cstr(const char* cstr) noexcept {
    return StringViewTokenizer{cstr, cstr != nullptr ? std::strlen(cstr) : 0, 0};
}

const char* StringViewTokenizer::next_token(char delimiter, size_t* out_token_len) noexcept {
    if (cursor == length) {
        *out_token_len = 0;
        return nullptr;
    }
    const size_t start = cursor;
    size_t end = cursor;
    while (end < length && data[end] != delimiter) {
        cursor = ++end;
    }
    *out_token_len = end - start;
    while (cursor < length && data[cursor] == delimiter) {
        ++cursor;
    }
    return data + start;
}

int initialize_commands(const CommandSpec& friends_cmd, const CommandSpec& gui_cmd) noexcept {
    auto* table = static_cast<CommandSpec*>(util::zero_alloc(3, sizeof(CommandSpec)));
    if (table == nullptr) {
        return 1005;
    }
    table[0] = friends_cmd;
    table[1] = gui_cmd;
    // table[2] remains zero-initialized sentinel
    g_root_subcommands = table;
    return 0;
}

void shutdown_commands() noexcept {
    std::free(g_root_subcommands);
    g_root_subcommands = nullptr;
}

bool handle_outgoing_chat_packet(events::GameContext* ctx, jobject chat_packet) noexcept {
    JNIEnv* env = ctx->jni->env;
    jstring jmsg = minecraft::packet_chat_get_message(ctx->jni, chat_packet);
    if (jmsg == nullptr) {
        return false;
    }
    const jsize char_len = env->GetStringLength(jmsg);
    if (char_len <= 0) {
        return false;
    }

    const jsize extract_chars = char_len < 100 ? char_len : 100;
    const jsize utf_len = env->GetStringUTFLength(jmsg);
    char raw_buf[416]{};
    env->GetStringUTFRegion(jmsg, 0, extract_chars, raw_buf);
    if (utf_len > 0 && static_cast<size_t>(utf_len) < sizeof(raw_buf)) {
        raw_buf[utf_len] = '\0';
    } else {
        raw_buf[sizeof(raw_buf) - 1] = '\0';
    }

    if (raw_buf[0] != '!') {
        return false;
    }

    const char* cmd_body = raw_buf + 1;
    // Escape `!!foo` -> rewrite outgoing packet to `!foo` and let Minecraft send it
    if (char_len >= 2 && cmd_body[0] == '!') {
        jstring escaped = env->NewStringUTF(cmd_body);
        minecraft::packet_chat_set_message(ctx->jni, chat_packet, escaped);
        return false;
    }

    char prefix_storage[256]{};
    util::StringBuilder prefix_sb{};
    prefix_sb.init(prefix_storage, sizeof(prefix_storage));
    prefix_sb.append_char('!');

    const size_t body_len = std::strlen(cmd_body);
    const CommandSpec* current_list = g_root_subcommands;
    const CommandSpec* parent_spec = &kRootCommandSpec;
    CommandSpec root_copy = kRootCommandSpec;
    root_copy.subcommands = g_root_subcommands;
    parent_spec = &root_copy;

    size_t pos = 0;
    const CommandSpec* matched_leaf = nullptr;

    if (current_list != nullptr) {
        while (pos < body_len) {
            size_t tok_end = pos;
            while (tok_end < body_len && cmd_body[tok_end] != ' ') {
                ++tok_end;
            }
            size_t next_pos = tok_end;
            while (next_pos < body_len && cmd_body[next_pos] == ' ') {
                ++next_pos;
            }

            if (current_list[0].name == nullptr) {
                break;
            }

            const size_t tok_len = tok_end - pos;
            const CommandSpec* found = nullptr;
            for (const CommandSpec* it = current_list; it->name != nullptr; ++it) {
                if (std::strlen(it->name) == tok_len &&
                    std::strncmp(it->name, cmd_body + pos, tok_len) == 0) {
                    found = it;
                    break;
                }
            }
            if (found == nullptr) {
                break;
            }

            prefix_sb.append_fmt("%s ", found->name);
            pos = next_pos;
            parent_spec = found;
            current_list = found->subcommands;
            if (current_list == nullptr) {
                matched_leaf = found;
                break;
            }
        }
    }

    // Subcommand help listing when command is incomplete or unknown
    if (matched_leaf == nullptr) {
        prefix_sb.terminate();
        minecraft::print_client_chat(ctx->jni, "\xC2\xA7" "7%s:", parent_spec->description);
        if (parent_spec->subcommands != nullptr) {
            for (const CommandSpec* sub = parent_spec->subcommands; sub->name != nullptr; ++sub) {
                minecraft::print_client_chat(
                    ctx->jni,
                    "\xC2\xA7" "f  %s%s \xC2\xA7" "7- %s",
                    prefix_sb.buffer,
                    sub->name,
                    sub->description);
            }
        }
        if (parent_spec == &root_copy) {
            minecraft::print_client_chat(
                ctx->jni,
                "\xC2\xA7" "f  !!<message> \xC2\xA7" "7- Send \xC2\xA7" "o!message \xC2\xA7" "7in chat");
        }
        return true;
    }

    // Parse up to 3 typed arguments for `matched_leaf`
    CommandArgValue parsed_args[3]{};

    auto print_usage = [&]() noexcept {
        prefix_sb.terminate();
        char usage_storage[256]{};
        util::StringBuilder usage_sb{};
        usage_sb.init(usage_storage, sizeof(usage_storage));
        usage_sb.append_fmt("\xC2\xA7" "c%s", prefix_sb.buffer);
        for (size_t a = 0; a < 3; ++a) {
            const CommandArgSpec& spec = matched_leaf->args[a];
            if (spec.name == nullptr) {
                break;
            }
            const bool optional = (spec.flags & kCommandArgOptional) != 0;
            const char open_br = optional ? '[' : '<';
            const char close_br = optional ? ']' : '>';
            const uint32_t type_idx = static_cast<uint32_t>(spec.type);
            const char* type_label = (type_idx >= 1 && type_idx <= 3) ? kArgTypeNames[type_idx - 1] : "string";
            usage_sb.append_fmt("%c\xC2\xA7" "o%s\xC2\xA7" "c: %s%c ", open_br, spec.name, type_label, close_br);
        }
        usage_sb.terminate();
        minecraft::print_client_chat(ctx->jni, "%s", usage_sb.buffer);
    };

    for (size_t arg_idx = 0; arg_idx < 3; ++arg_idx) {
        const CommandArgSpec& spec = matched_leaf->args[arg_idx];
        if (spec.name == nullptr) {
            break;
        }

        if (pos >= body_len) {
            if ((spec.flags & kCommandArgOptional) == 0) {
                minecraft::print_client_chat(
                    ctx->jni,
                    "\xC2\xA7" "cMissing argument \xC2\xA7" "o%s\xC2\xA7" "c.",
                    spec.name);
                print_usage();
                return true;
            }
            parsed_args[arg_idx] = spec.default_value;
            continue;
        }

        size_t tok_end = pos;
        while (tok_end < body_len && cmd_body[tok_end] != ' ') {
            ++tok_end;
        }
        const char* tok_ptr = cmd_body + pos;
        const size_t tok_len = tok_end - pos;

        size_t next_pos = tok_end;
        while (next_pos < body_len && cmd_body[next_pos] == ' ') {
            ++next_pos;
        }
        pos = next_pos;

        if (spec.type == CommandArgType::String) {
            parsed_args[arg_idx].str.data = tok_ptr;
            parsed_args[arg_idx].str.length = tok_len;
        } else if (spec.type == CommandArgType::Integer) {
            uint64_t val = 0;
            bool valid = true;
            for (size_t k = 0; k < tok_len; ++k) {
                const auto ch = static_cast<uint8_t>(tok_ptr[k]);
                if (ch < '0' || ch > '9') {
                    valid = false;
                    break;
                }
                const uint64_t next_val = val * 10 + (ch - '0');
                if (next_val < val) {
                    valid = false;
                    break;
                }
                val = next_val;
            }
            if (!valid) {
                minecraft::print_client_chat(
                    ctx->jni,
                    "\xC2\xA7" "cArgument \xC2\xA7" "o%s\xC2\xA7" "c must be a valid integer.",
                    spec.name);
                print_usage();
                return true;
            }
            parsed_args[arg_idx].integer = val;
        } else if (spec.type == CommandArgType::Boolean) {
            const char first = tok_len > 0 ? tok_ptr[0] : '\0';
            if (first == 'f' || first == 'F' || first == 'n' || first == 'N') {
                parsed_args[arg_idx].boolean = false;
            } else if (first == 't' || first == 'T' || first == 'y' || first == 'Y') {
                parsed_args[arg_idx].boolean = true;
            } else {
                minecraft::print_client_chat(
                    ctx->jni,
                    "\xC2\xA7" "cArgument \xC2\xA7" "o%s\xC2\xA7" "c must be a boolean (y/n).",
                    spec.name);
                print_usage();
                return true;
            }
        }
    }

    if (pos < body_len) {
        minecraft::print_client_chat(ctx->jni, "\xC2\xA7" "cToo many arguments provided.");
        print_usage();
        return true;
    }

    if (matched_leaf->handler != nullptr) {
        matched_leaf->handler(ctx, parsed_args);
    }
    return true;
}

bool should_cancel_tab_complete_packet(events::GameContext* ctx, jobject tab_complete_packet) noexcept {
    JNIEnv* env = ctx->jni->env;
    jstring jmsg = minecraft::packet_tab_complete_get_message(ctx->jni, tab_complete_packet);
    if (jmsg == nullptr || env->GetStringLength(jmsg) <= 0) {
        return false;
    }
    const jchar* chars = env->GetStringChars(jmsg, nullptr);
    if (chars == nullptr) {
        return false;
    }
    const bool starts_with_bang = (chars[0] == '!');
    env->ReleaseStringChars(jmsg, chars);
    return starts_with_bang;
}

} // namespace slinky::commands
