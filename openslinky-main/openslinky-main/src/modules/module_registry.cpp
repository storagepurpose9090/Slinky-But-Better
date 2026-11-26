#include "modules/module_registry.hpp"

#include "modules/combat/aim_assist.hpp"
#include "modules/utility/anti_bot.hpp"
#include "modules/visual/anti_debuff.hpp"
#include "modules/combat/auto_block.hpp"
#include "modules/combat/auto_clicker.hpp"
#include "modules/utility/auto_pot.hpp"
#include "modules/utility/auto_rod.hpp"
#include "modules/utility/auto_soup.hpp"
#include "modules/world/auto_tool.hpp"
#include "modules/combat/auto_weapon.hpp"
#include "modules/combat/backtrack.hpp"
#include "modules/utility/blink.hpp"
#include "modules/visual/block_esp.hpp"
#include "modules/world/block_in.hpp"
#include "modules/world/bridge_assist.hpp"
#include "modules/visual/chams.hpp"
#include "modules/visual/chest_esp.hpp"
#include "modules/utility/chest_stealer.hpp"
#include "modules/world/clutch.hpp"
#include "modules/combat/criticals.hpp"
#include "modules/utility/debug.hpp"
#include "modules/utility/fake_lag.hpp"
#include "modules/movement/fast_accel.hpp"
#include "modules/world/fast_mine.hpp"
#include "modules/world/fast_place.hpp"
#include "modules/visual/free_look.hpp"
#include "modules/utility/friends.hpp"
#include "modules/world/ghost_hand.hpp"
#include "modules/visual/gui.hpp"
#include "modules/combat/hitboxes.hpp"
#include "modules/combat/hit_select.hpp"
#include "modules/visual/indicators.hpp"
#include "modules/movement/instant_stop.hpp"
#include "modules/utility/inv_manager.hpp"
#include "modules/movement/inv_walk.hpp"
#include "modules/visual/item_esp.hpp"
#include "modules/utility/item_use_fix.hpp"
#include "modules/movement/keep_sprint.hpp"
#include "modules/combat/knockback_delay.hpp"
#include "modules/combat/knockback_displace.hpp"
#include "modules/combat/lag_range.hpp"
#include "modules/visual/mod_overlay.hpp"
#include "modules/visual/name_tags.hpp"
#include "modules/utility/no_hit_delay.hpp"
#include "modules/visual/no_hurt_cam.hpp"
#include "modules/utility/no_item_release.hpp"
#include "modules/movement/no_jump_delay.hpp"
#include "modules/movement/no_slow.hpp"
#include "modules/visual/notifications.hpp"
#include "modules/utility/no_use_delay.hpp"
#include "modules/movement/null_move.hpp"
#include "modules/combat/piercing.hpp"
#include "modules/utility/ping_fix.hpp"
#include "modules/visual/player_esp.hpp"
#include "modules/visual/pointers.hpp"
#include "modules/combat/reach.hpp"
#include "modules/utility/refill.hpp"
#include "modules/movement/safe_walk.hpp"
#include "modules/movement/sprint.hpp"
#include "modules/combat/sprint_reset.hpp"
#include "modules/movement/strafe.hpp"
#include "modules/utility/teams.hpp"
#include "modules/utility/timer.hpp"
#include "modules/combat/timer_range.hpp"
#include "modules/visual/trajectories.hpp"
#include "modules/combat/velocity.hpp"
#include "modules/combat/weapons.hpp"

namespace slinky::modules {

events::Module* const* get_module_registry() noexcept {
    static events::Module* const s_registry[kModuleRegistryCount] = {
        get_aim_assist_module(),         // [ 0] Aim Assist
        get_anti_bot_module(),           // [ 1] Anti Bot
        get_anti_debuff_module(),        // [ 2] Anti Debuff
        get_auto_block_module(),         // [ 3] Auto Block
        get_auto_clicker_module(),       // [ 4] Auto Clicker
        get_auto_pot_module(),           // [ 5] Auto Pot
        get_auto_rod_module(),           // [ 6] Auto Rod
        get_auto_soup_module(),          // [ 7] Auto Soup
        get_auto_tool_module(),          // [ 8] Auto Tool
        get_auto_weapon_module(),        // [ 9] Auto Weapon
        get_backtrack_module(),          // [10] Backtrack
        get_blink_module(),              // [11] Blink
        get_block_esp_module(),          // [12] Block ESP
        get_block_in_module(),           // [13] Block In
        get_bridge_assist_module(),      // [14] Bridge Assist
        get_chams_module(),              // [15] Chams
        get_chest_esp_module(),          // [16] Chest ESP
        get_chest_stealer_module(),      // [17] Chest Stealer
        get_clutch_module(),             // [18] Clutch
        get_criticals_module(),          // [19] Criticals
        get_debug_module(),              // [20] Debug
        get_fake_lag_module(),           // [21] Fake Lag
        get_fast_accel_module(),         // [22] Fast Accel
        get_fast_mine_module(),          // [23] Fast Mine
        get_fast_place_module(),         // [24] Fast Place
        get_free_look_module(),          // [25] Free Look
        get_friends_module(),            // [26] Friends
        get_ghost_hand_module(),         // [27] Ghost Hand
        get_gui_module(),                // [28] GUI
        get_hitboxes_module(),           // [29] Hitboxes
        get_hit_select_module(),         // [30] Hit Select
        get_indicators_module(),         // [31] Indicators
        get_instant_stop_module(),       // [32] Instant Stop
        get_inv_manager_module(),        // [33] Inv Manager
        get_inv_walk_module(),           // [34] Inv Walk
        get_item_esp_module(),           // [35] Item ESP
        get_item_use_fix_module(),       // [36] Item Use Fix
        get_keep_sprint_module(),        // [37] Keep Sprint
        get_knockback_delay_module(),    // [38] Knockback Delay
        get_knockback_displace_module(), // [39] Knockback Displace
        get_lag_range_module(),          // [40] Lag Range
        get_mod_overlay_module(),        // [41] Mod Overlay
        get_name_tags_module(),          // [42] Name Tags
        get_no_hit_delay_module(),       // [43] No Hit Delay
        get_no_hurt_cam_module(),        // [44] No Hurt Cam
        get_no_item_release_module(),    // [45] No Item Release
        get_no_jump_delay_module(),      // [46] No Jump Delay
        get_no_slow_module(),            // [47] No Slow
        get_notifications_module(),      // [48] Notifications
        get_no_use_delay_module(),       // [49] No Use Delay
        get_null_move_module(),          // [50] Null Move
        get_piercing_module(),           // [51] Piercing
        get_ping_fix_module(),           // [52] Ping Fix
        get_player_esp_module(),         // [53] Player ESP
        get_pointers_module(),           // [54] Pointers
        get_reach_module(),              // [55] Reach
        get_refill_module(),             // [56] Refill
        get_safe_walk_module(),          // [57] Safe Walk
        get_sprint_module(),             // [58] Sprint
        get_sprint_reset_module(),       // [59] Sprint Reset
        get_strafe_module(),             // [60] Strafe
        get_teams_module(),              // [61] Teams
        get_timer_module(),              // [62] Timer
        get_timer_range_module(),        // [63] Timer Range
        get_trajectories_module(),       // [64] Trajectories
        get_velocity_module(),           // [65] Velocity
        get_weapons_module(),            // [66] Weapons
    };
    return s_registry;
}

} // namespace slinky::modules
