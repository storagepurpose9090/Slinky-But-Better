#include "modules/visual/no_hurt_cam.hpp"

#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"

namespace slinky::modules {

namespace {

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "No Hurt Cam";
    mod.description = "Removes camera shake when taking damage.";
    mod.category = events::ModuleCategory::Visual;
    return mod;
}

} // namespace

// Matches sub_180063410
void no_hurt_cam_apply(events::JniContext* jni) noexcept {
    jobject mc = minecraft::mc_get_instance(jni);
    jobject player = minecraft::mc_get_player(jni, mc);
    if (player != nullptr) {
        minecraft::living_set_max_hurt_time(jni, player, 0x7FFFFFFF);
    }
}

events::Module* get_no_hurt_cam_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
