#include "modules/utility/anti_bot.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

struct TrackedPlayerBotState {
    uint64_t uuid_msb = 0;
    uint64_t uuid_lsb = 0;
    double horiz_dist_sq_accum = 0.0;
    bool passed_age = false;
    bool passed_visible = false;
    bool passed_username = false;
    uint8_t pad[5]{};
};
static_assert(sizeof(TrackedPlayerBotState) == 32, "TrackedPlayerBotState must match 32-byte binary layout");

constexpr const char* kInvalidUuidOptions[3] = {
    "Off",
    "Stationary only",
    "Always",
};

config::FloatSetting s_min_age{
    {},
    "Minimum entity age",
    "%.0f s",
    20.0f,
    0.05f,
    0.0f,
    200.0f,
    0.0f,
    0.0f,
};

config::BoolSetting s_always_stationary{
    {},
    "Always stationary",
    false,
    false,
    {},
};

config::ModeSetting s_invalid_uuid{
    {},
    "Invalid UUID",
    3,
    0,
    kInvalidUuidOptions,
    1,
    0,
    false,
    {},
};

config::BoolSetting s_sleeping{
    {},
    "Sleeping",
    false,
    false,
    {},
};

config::BoolSetting s_always_invis{
    {},
    "Always invisible",
    true,
    false,
    {},
};

config::BoolSetting s_invalid_username{
    {},
    "Invalid username",
    true,
    false,
    {},
};

int32_t s_last_world_hash = 0;
util::PodVector<TrackedPlayerBotState> s_tracked_players{};

// sub_180052C80
int compare_uuid_entry(const TrackedPlayerBotState& a, const TrackedPlayerBotState& b) noexcept {
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

TrackedPlayerBotState* get_or_insert_player(const TrackedPlayerBotState& key) noexcept {
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(s_tracked_players.count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = compare_uuid_entry(key, s_tracked_players.data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return &s_tracked_players.data[mid];
        } else {
            lo = mid + 1;
        }
    }
    return s_tracked_players.insert_at(static_cast<int>(lo), key);
}

bool has_any_equipped_armor(events::JniContext* jni, jobject player) noexcept {
    jobject inv = mc::player_get_inventory(jni, player);
    for (int32_t slot = 36; slot <= 39; ++slot) {
        jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
        if (stack != nullptr) {
            jobject item = mc::item_stack_get_item(jni, stack);
            if (item != nullptr && mc::item_get_id(jni, item) != 0) {
                return true;
            }
        }
    }
    return false;
}

bool has_valid_username_char(JNIEnv* env, jstring name_str) noexcept {
    const jsize len = env->GetStringLength(name_str);
    const jchar* chars = env->GetStringCritical(name_str, nullptr);
    bool valid = false;
    if (chars != nullptr) {
        for (jsize i = 0; i < len; ++i) {
            const uint16_t ch = static_cast<uint16_t>(chars[i]);
            if (ch == 0x00A7u) { // '§' formatting prefix skips the next code char
                ++i;
                continue;
            }
            if (static_cast<uint16_t>((ch & 0xFFDFu) - 'A') < 26u ||
                ch == '_' ||
                static_cast<uint16_t>(ch - '0') <= 9u) {
                valid = true;
                break;
            }
        }
        env->ReleaseStringCritical(name_str, chars);
    }
    return valid;
}

// sub_1800525B0
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;

    const int32_t world_hash = (ctx->world != nullptr) ? mc::java_object_hash_code(jni, ctx->world) : 0;
    if (world_hash != s_last_world_hash) {
        s_tracked_players.clear_and_free();
        s_last_world_hash = world_hash;
    }

    if (ctx->world == nullptr) {
        return;
    }

    jobject player_list = mc::world_get_player_entities(jni, ctx->world);
    JNIEnv* env = jni->env;
    for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
        jobject player = mc::java_list_get(jni, player_list, i);
        if (env->IsSameObject(ctx->local_player, player)) {
            continue;
        }

        jobject uuid = mc::entity_get_unique_id(jni, player);
        TrackedPlayerBotState key{};
        key.uuid_msb = static_cast<uint64_t>(mc::uuid_get_most_sig_bits(jni, uuid));
        key.uuid_lsb = static_cast<uint64_t>(mc::uuid_get_least_sig_bits(jni, uuid));
        TrackedPlayerBotState* state = get_or_insert_player(key);

        const math::Vec3d prev_pos = mc::entity_get_prev_position(jni, player);
        const math::Vec3d cur_pos = mc::entity_get_position(jni, player);
        const double dx = prev_pos.x - cur_pos.x;
        const double dz = prev_pos.z - cur_pos.z;
        const double dist_accum = dx * dx + dz * dz + state->horiz_dist_sq_accum;
        state->horiz_dist_sq_accum = dist_accum;

        const bool is_uuid_v2 = (static_cast<uint32_t>(state->uuid_msb) & 0xF000u) == 0x2000u;
        if ((s_always_stationary.value && dist_accum < 1.0) ||
            (s_invalid_uuid.value != 0 && is_uuid_v2 && (dist_accum < 1.0 || s_invalid_uuid.value == 2)) ||
            (s_sleeping.value && mc::player_is_sleeping(jni, player))) {
            mc::player_add_tag_flags(jni, player, mc::kPlayerTagBot);
            continue;
        }

        const int32_t ticks_existed = mc::entity_get_ticks_existed(jni, player);
        if (s_always_invis.value && !state->passed_visible) {
            if (mc::player_is_invisible_to_player(jni, player, ctx->local_player) &&
                !has_any_equipped_armor(jni, player)) {
                mc::player_add_tag_flags(jni, player, mc::kPlayerTagBot);
                continue;
            }
            if (ticks_existed >= 21) {
                state->passed_visible = true;
            }
        }

        if (s_min_age.value > 0.0f && !state->passed_age) {
            if (s_min_age.value > static_cast<float>(ticks_existed)) {
                mc::player_add_tag_flags(jni, player, mc::kPlayerTagBot);
                continue;
            }
            state->passed_age = true;
        }

        if (s_invalid_username.value && !state->passed_username) {
            jstring name_str = mc::entity_get_name(jni, player);
            if (has_valid_username_char(env, name_str)) {
                state->passed_username = true;
            }
            if (!state->passed_username) {
                mc::player_add_tag_flags(jni, player, mc::kPlayerTagBot);
                continue;
            }
        }

        mc::player_remove_tag_flags(jni, player, mc::kPlayerTagBot);
    }
}

// sub_180052A60
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && !event->enabled) {
        s_tracked_players.clear_and_free();

        events::GameContext* ctx = event->ctx;
        if (ctx->world != nullptr) {
            events::JniContext* jni = ctx->jni;
            jobject player_list = mc::world_get_player_entities(jni, ctx->world);
            for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
                jobject player = mc::java_list_get(jni, player_list, i);
                if (!jni->env->IsSameObject(ctx->local_player, player)) {
                    mc::player_remove_tag_flags(jni, player, mc::kPlayerTagBot);
                }
            }
        }
    }
}

// sub_180052B40
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_invalid_uuid.render(gui, renderer);
    s_invalid_username.render(gui, renderer);
    s_always_invis.render(gui, renderer);
    s_always_stationary.render(gui, renderer);
    s_sleeping.render(gui, renderer);
    s_min_age.render(gui, renderer);
}

// sub_180052BE0
void visit_config(config::ConfigArchive* archive) noexcept {
    s_invalid_uuid.visit(archive, "invalid_uuid");
    s_invalid_username.visit(archive, "invalid_username");
    s_always_invis.visit(archive, "always_invis");
    s_always_stationary.visit(archive, "always_stationary");
    s_sleeping.visit(archive, "sleeping");
    s_min_age.visit(archive, "min_age");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Anti Bot";
    mod.description = "Ignores NPCs for specific modules.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 10000, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_anti_bot_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
