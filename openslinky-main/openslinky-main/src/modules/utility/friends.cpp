#include "modules/utility/friends.hpp"
#include "modules/module_interop.hpp"

#include <cstring>
#include <ctime>

#include "commands/command_manager.hpp"
#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

struct FriendEntry {
    uint64_t uuid_msb = 0;
    uint64_t uuid_lsb = 0;
    int64_t last_seen_time = 0;
    uint64_t reserved = 0;
};
static_assert(sizeof(FriendEntry) == 32, "FriendEntry must match 32-byte binary layout");

constexpr char kHexDigits[17] = "0123456789abcdef";

util::PodVector<FriendEntry> s_friends{};
char s_hex_buffer[49]{};

// sub_18005C750
int compare_friend_uuid(const FriendEntry& a, const FriendEntry& b) noexcept {
    const uint64_t a_hi = __builtin_bswap64(a.uuid_msb);
    const uint64_t b_hi = __builtin_bswap64(b.uuid_msb);
    if (a_hi != b_hi) {
        return (a_hi > b_hi) ? 1 : -1;
    }
    const uint64_t a_lo = __builtin_bswap64(a.uuid_lsb);
    const uint64_t b_lo = __builtin_bswap64(b.uuid_lsb);
    if (a_lo != b_lo) {
        return (a_lo > b_lo) ? 1 : -1;
    }
    return 0;
}

FriendEntry* find_friend(const FriendEntry& key) noexcept {
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(s_friends.count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = compare_friend_uuid(key, s_friends.data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return &s_friends.data[mid];
        } else {
            lo = mid + 1;
        }
    }
    return nullptr;
}

FriendEntry* insert_friend_unique(const FriendEntry& entry) noexcept {
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(s_friends.count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = compare_friend_uuid(entry, s_friends.data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return &s_friends.data[mid];
        } else {
            lo = mid + 1;
        }
    }
    return s_friends.insert_at(static_cast<int>(lo), entry);
}

void insert_friend_sorted(const FriendEntry& entry) noexcept {
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(s_friends.count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = compare_friend_uuid(entry, s_friends.data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            lo = mid;
            break;
        } else {
            lo = mid + 1;
        }
    }
    s_friends.insert_at(static_cast<int>(lo), entry);
}

void erase_friend_ptr(FriendEntry* entry_ptr) noexcept {
    const int idx = static_cast<int>(entry_ptr - s_friends.data);
    s_friends.erase_at(idx);
}

// sub_18005D070
size_t friends_list_get_count(void* /*ctx*/) noexcept {
    return static_cast<size_t>(static_cast<uint32_t>(s_friends.count));
}

// sub_18005D080
const char* friends_list_get_name(void* /*ctx*/, size_t index) noexcept {
    const auto* raw = reinterpret_cast<const uint8_t*>(&s_friends.data[index]);
    for (size_t i = 0; i < 24; ++i) {
        s_hex_buffer[i * 2] = kHexDigits[raw[i] >> 4];
        s_hex_buffer[i * 2 + 1] = kHexDigits[raw[i] & 0x0Fu];
    }
    s_hex_buffer[48] = '\0';
    return s_hex_buffer;
}

bool decode_lowercase_hex_nibble(char ch, uint8_t* out_val) noexcept {
    const uint8_t uch = static_cast<uint8_t>(ch);
    if (static_cast<uint8_t>(uch - '0') < 10u) {
        *out_val = static_cast<uint8_t>(uch - '0');
        return true;
    }
    if (static_cast<uint8_t>(uch - 'a') <= 25u) {
        *out_val = static_cast<uint8_t>(uch - 87u);
        return true;
    }
    return false;
}

// sub_18005D280
bool friends_list_add_item(void* /*ctx*/, const char* hex_str, const math::Color3f* /*color*/) noexcept {
    FriendEntry entry{};
    const size_t len = std::strlen(hex_str);
    if (len < 48u) {
        return false;
    }

    auto* dst = reinterpret_cast<uint8_t*>(&entry);
    for (size_t i = 0; i < 16; ++i) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!decode_lowercase_hex_nibble(hex_str[2 * i], &hi) ||
            !decode_lowercase_hex_nibble(hex_str[2 * i + 1], &lo)) {
            break;
        }
        dst[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    for (size_t i = 16; i < 24; ++i) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!decode_lowercase_hex_nibble(hex_str[2 * i], &hi) ||
            !decode_lowercase_hex_nibble(hex_str[2 * i + 1], &lo)) {
            break;
        }
        dst[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    insert_friend_unique(entry);
    return true;
}

// sub_18005D540
void friends_list_remove_at(void* /*ctx*/, size_t index) noexcept {
    s_friends.erase_at(static_cast<int>(index));
}

// sub_18005D560
void friends_list_clear_all(void* /*ctx*/) noexcept {
    s_friends.count = 0;
}

config::BoolSetting s_middle_click_add{
    {},
    "Middle-click to add",
    false,
    false,
    {},
};

config::DynamicListSetting s_friends_setting{
    "Friends",
    "Enter friend username",
    nullptr,
    config::DynamicListItemKind::StringOnly,
    0,
    &s_friends,
    friends_list_get_count,
    friends_list_get_name,
    nullptr,
    friends_list_add_item,
    friends_list_remove_at,
    friends_list_clear_all,
    nullptr,
    false,
    {},
};

char ascii_tolower(char ch) noexcept {
    const uint8_t uch = static_cast<uint8_t>(ch);
    return (static_cast<uint8_t>(uch - 'A') < 26u) ? static_cast<char>(uch + 32u) : ch;
}

// sub_18005CDF0
bool resolve_player_by_prefix(
    events::GameContext* ctx,
    const commands::CommandArgValue& username_arg,
    FriendEntry* out_entry,
    char* out_resolved_name) noexcept {
    const char* query = username_arg.str.data;
    const size_t query_len = username_arg.str.length;
    events::JniContext* jni = ctx->jni;

    if (query_len >= 17u) {
        mc::print_client_chat(jni, "\xC2\xA7" "cUsername is too long.");
        return false;
    }

    JNIEnv* env = jni->env;
    char query_buf[32]{};
    if (query_len > 0) {
        std::memcpy(query_buf, query, query_len);
    }
    query_buf[query_len] = '\0';
    if (env->NewStringUTF(query_buf) == nullptr) {
        return false;
    }

    jobject player_list = mc::world_get_player_entities(jni, ctx->world);
    if (mc::java_list_size(jni, player_list) <= 0) {
        mc::print_client_chat(jni, "\xC2\xA7" "cPlayer not found.");
        return false;
    }

    jobject matched_player = nullptr;
    for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
        jobject candidate = mc::java_list_get(jni, player_list, i);
        if (env->IsSameObject(candidate, ctx->local_player)) {
            continue;
        }

        jstring name_jstr = mc::entity_get_name(jni, candidate);
        const jsize name_len = env->GetStringUTFLength(name_jstr);
        if (query_len <= static_cast<size_t>(name_len)) {
            const char* name_utf = env->GetStringUTFChars(name_jstr, nullptr);
            bool prefix_matches = true;
            for (size_t k = 0; k < query_len; ++k) {
                if (ascii_tolower(query[k]) != ascii_tolower(name_utf[k])) {
                    prefix_matches = false;
                    break;
                }
            }

            if (prefix_matches) {
                if (matched_player != nullptr) {
                    env->ReleaseStringUTFChars(name_jstr, name_utf);
                    mc::print_client_chat(
                        jni,
                        "\xC2\xA7" "cFound multiple names starting with '%.*s'.",
                        static_cast<int>(query_len),
                        query);
                    return false;
                }
                const int32_t copy_len = (name_len >= 16) ? 16 : static_cast<int32_t>(name_len);
                std::memcpy(out_resolved_name, name_utf, static_cast<size_t>(copy_len));
                out_resolved_name[copy_len] = '\0';
                matched_player = candidate;
            }
            env->ReleaseStringUTFChars(name_jstr, name_utf);
        }
    }

    if (matched_player == nullptr) {
        mc::print_client_chat(jni, "\xC2\xA7" "cPlayer not found.");
        return false;
    }

    jobject uuid = mc::entity_get_unique_id(jni, matched_player);
    out_entry->uuid_msb = static_cast<uint64_t>(mc::uuid_get_most_sig_bits(jni, uuid));
    out_entry->uuid_lsb = static_cast<uint64_t>(mc::uuid_get_least_sig_bits(jni, uuid));
    out_entry->last_seen_time = 0;
    out_entry->reserved = 0;
    return true;
}

// sub_18005CA50
void cmd_friend_add(events::GameContext* ctx, const commands::CommandArgValue* args) noexcept {
    FriendEntry entry{};
    char resolved_name[32]{};
    if (resolve_player_by_prefix(ctx, args[0], &entry, resolved_name)) {
        insert_friend_unique(entry);
        mc::print_client_chat(ctx->jni, "\xC2\xA7" "aAdded %s as friend.", resolved_name);
    }
}

// sub_18005CAD0
void cmd_friend_remove(events::GameContext* ctx, const commands::CommandArgValue* args) noexcept {
    FriendEntry entry{};
    char resolved_name[32]{};
    if (resolve_player_by_prefix(ctx, args[0], &entry, resolved_name)) {
        FriendEntry* existing = find_friend(entry);
        if (existing != nullptr) {
            erase_friend_ptr(existing);
        }
        mc::print_client_chat(ctx->jni, "\xC2\xA7" "aRemoved %s as friend.", resolved_name);
    }
}

// sub_18005CB70
void cmd_friend_addnearby(events::GameContext* ctx, const commands::CommandArgValue* args) noexcept {
    const uint64_t range_blocks = args[0].integer;
    const bool replace = args[1].boolean;
    const size_t prev_count = static_cast<size_t>(static_cast<uint32_t>(s_friends.count));

    if (replace) {
        s_friends.count = 0;
    }

    events::JniContext* jni = ctx->jni;
    const math::Vec3d local_pos = mc::entity_get_position(jni, ctx->local_player);
    jobject player_list = mc::world_get_player_entities(jni, ctx->world);

    size_t added_count = 0;
    const int32_t list_count = mc::java_list_size(jni, player_list);
    if (list_count > 0) {
        const double range_d = static_cast<double>(range_blocks);
        const double max_dist_sq = range_d * range_d;
        for (int32_t i = 0; i < list_count; ++i) {
            jobject player = mc::java_list_get(jni, player_list, i);
            if (!jni->env->IsSameObject(player, ctx->local_player) &&
                !mc::player_has_tag_flags(jni, player, mc::kPlayerTagBot)) {
                const math::Vec3d cand_pos = mc::entity_get_position(jni, player);
                if (math::vec3_dist_sq(local_pos, cand_pos) <= max_dist_sq) {
                    jobject uuid = mc::entity_get_unique_id(jni, player);
                    FriendEntry entry{};
                    entry.uuid_msb = static_cast<uint64_t>(mc::uuid_get_most_sig_bits(jni, uuid));
                    entry.uuid_lsb = static_cast<uint64_t>(mc::uuid_get_least_sig_bits(jni, uuid));
                    insert_friend_unique(entry);
                    ++added_count;
                }
            }
        }
    }

    const char* suffix = (added_count == 1) ? "" : "s";
    if (replace) {
        mc::print_client_chat(
            jni,
            "\xC2\xA7" "aRemoved %zu, added %zu friend%s.",
            prev_count,
            added_count,
            suffix);
    } else {
        mc::print_client_chat(
            jni,
            "\xC2\xA7" "aAdded %zu friend%s.",
            added_count,
            suffix);
    }
}

// sub_18005CDA0
void cmd_friend_clear(events::GameContext* ctx, const commands::CommandArgValue* /*args*/) noexcept {
    const size_t removed_count = static_cast<size_t>(static_cast<uint32_t>(s_friends.count));
    const char* suffix = (removed_count == 1) ? "" : "s";
    mc::print_client_chat(ctx->jni, "\xC2\xA7" "aRemoved %zu friend%s.", removed_count, suffix);
    s_friends.count = 0;
}

const commands::CommandSpec s_friend_subcommands[5] = {
    {
        "add",
        "Add a friend",
        {
            {"username", commands::CommandArgType::String, commands::kCommandArgRequired, {}},
            {},
            {},
        },
        cmd_friend_add,
        nullptr,
    },
    {
        "remove",
        "Remove a friend",
        {
            {"username", commands::CommandArgType::String, commands::kCommandArgRequired, {}},
            {},
            {},
        },
        cmd_friend_remove,
        nullptr,
    },
    {
        "addnearby",
        "Add nearby players as friends",
        {
            {"range", commands::CommandArgType::Integer, commands::kCommandArgRequired, {}},
            {"replace", commands::CommandArgType::Boolean, commands::kCommandArgOptional, {}},
            {},
        },
        cmd_friend_addnearby,
        nullptr,
    },
    {
        "clear",
        "Remove all friends",
        {},
        cmd_friend_clear,
        nullptr,
    },
    {},
};

const commands::CommandSpec s_friends_root_command{
    "f",
    "Manage your friends list",
    {},
    nullptr,
    s_friend_subcommands,
};

// sub_18005C790
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    if (ctx->local_player == nullptr) {
        return;
    }

    const int64_t now_epoch = static_cast<int64_t>(std::time(nullptr));
    events::JniContext* jni = ctx->jni;
    jobject player_list = mc::world_get_player_entities(jni, ctx->world);

    for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
        jobject player = mc::java_list_get(jni, player_list, i);
        if (jni->env->IsSameObject(ctx->local_player, player)) {
            continue;
        }

        jobject uuid = mc::entity_get_unique_id(jni, player);
        FriendEntry key{};
        key.uuid_msb = static_cast<uint64_t>(mc::uuid_get_most_sig_bits(jni, uuid));
        key.uuid_lsb = static_cast<uint64_t>(mc::uuid_get_least_sig_bits(jni, uuid));

        FriendEntry* found = find_friend(key);
        if (found != nullptr) {
            mc::player_add_tag_flags(jni, player, mc::kPlayerTagFriend);
            found->last_seen_time = now_epoch;
        } else {
            mc::player_remove_tag_flags(jni, player, mc::kPlayerTagFriend);
        }
    }
}

// sub_18005C8E0
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && !event->enabled) {
        events::GameContext* ctx = event->ctx;
        if (ctx->world != nullptr) {
            events::JniContext* jni = ctx->jni;
            jobject player_list = mc::world_get_player_entities(jni, ctx->world);
            for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
                jobject player = mc::java_list_get(jni, player_list, i);
                if (!jni->env->IsSameObject(ctx->local_player, player)) {
                    mc::player_remove_tag_flags(jni, player, mc::kPlayerTagFriend);
                }
            }
        }
    }
}

// sub_18005C9A0
void on_client_shutdown(void* /*raw_event*/) noexcept {
    s_friends.clear_and_free();
}

// sub_18005C9D0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_middle_click_add.render(gui, renderer);
}

// sub_18005CA10
void visit_config(config::ConfigArchive* archive) noexcept {
    s_middle_click_add.visit(archive, "middle_click_add");
    s_friends_setting.visit(archive, "friends");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Friends";
    mod.description = "Lets you mark other players as friendly.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagHiddenFromHud | events::kModuleFlagGlobalSettings;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 10000, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.events[events::kEventClientShutdown] = {on_client_shutdown, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

// sub_18005C5D0
void friends_on_middle_click(events::JniContext* jni, events::ModuleManager* manager) noexcept {
    if (!s_middle_click_add.value) {
        return;
    }

    jobject mc = mc::mc_get_instance(jni);
    jobject mop = mc::mc_get_object_mouse_over(jni, mc);
    if (mop == nullptr) {
        return;
    }

    jobject hit_entity = mc::mop_get_entity_hit(jni, mop);
    if (hit_entity == nullptr) {
        return;
    }

    jobject other_player = mc::as_entity_other_player_mp(jni, hit_entity);
    if (other_player == nullptr) {
        return;
    }

    jobject uuid = mc::entity_get_unique_id(jni, other_player);
    FriendEntry entry{};
    entry.uuid_msb = static_cast<uint64_t>(mc::uuid_get_most_sig_bits(jni, uuid));
    entry.uuid_lsb = static_cast<uint64_t>(mc::uuid_get_least_sig_bits(jni, uuid));
    entry.last_seen_time = static_cast<int64_t>(std::time(nullptr));
    entry.reserved = 0;

    events::FriendChangedEvent changed_ev{};
    changed_ev.id = events::kEventFriendChanged;

    FriendEntry* existing = find_friend(entry);
    if (existing != nullptr) {
        changed_ev.added = false;
        erase_friend_ptr(existing);
    } else {
        if (mc::player_has_tag_flags(jni, other_player, mc::kPlayerTagBot)) {
            return;
        }
        changed_ev.added = true;
        insert_friend_sorted(entry);
    }

    manager->dispatch(&changed_ev);
}

// off_18016F730
const commands::CommandSpec& get_friends_command_spec() noexcept {
    return s_friends_root_command;
}

events::Module* get_friends_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
