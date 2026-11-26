cmake_minimum_required(VERSION 3.20)
project(openslinky VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(JAVAC_EXECUTABLE javac REQUIRED)

# ============================================================================
# 1. Java Hook & Netty Channel Handler Compilation + Binary Asset Embedding
# ============================================================================
set(SLINKY_JAVA_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/java_classes")
set(SLINKY_GEN_DIR      "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(SLINKY_JAVA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/java/src/SlinkyHooks13.java"
    "${CMAKE_CURRENT_SOURCE_DIR}/java/src/SlinkyChannelHandler.java"
)

set(SLINKY_JAVA_CLASSES
    "${SLINKY_JAVA_OUT_DIR}/SlinkyHooks13.class"
    "${SLINKY_JAVA_OUT_DIR}/SlinkyChannelHandler.class"
)

add_custom_command(
    OUTPUT ${SLINKY_JAVA_CLASSES}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SLINKY_JAVA_OUT_DIR}"
    COMMAND "${JAVAC_EXECUTABLE}" --release 8 -g:none
            -cp "${CMAKE_CURRENT_SOURCE_DIR}/java/stubs"
            -d "${SLINKY_JAVA_OUT_DIR}"
            ${SLINKY_JAVA_SOURCES}
    DEPENDS ${SLINKY_JAVA_SOURCES}
    COMMENT "Compiling SlinkyHooks13.java and SlinkyChannelHandler.java"
    VERBATIM
)

set(SLINKY_EMBED_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.py")
set(SLINKY_ASSET_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/logo.png"
)

add_custom_command(
    OUTPUT
        "${SLINKY_GEN_DIR}/embedded_assets.hpp"
        "${SLINKY_GEN_DIR}/embedded_assets.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SLINKY_GEN_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${SLINKY_EMBED_SCRIPT}"
            "${SLINKY_GEN_DIR}/embedded_assets.hpp"
            "${SLINKY_GEN_DIR}/embedded_assets.cpp"
            kFontInterMedium          "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf"
            kFontInterBold            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf"
            kImageLogoPng             "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/logo.png"
            kClassSlinkyHooks13       "${SLINKY_JAVA_OUT_DIR}/SlinkyHooks13.class"
            kClassSlinkyChannelHandler "${SLINKY_JAVA_OUT_DIR}/SlinkyChannelHandler.class"
    DEPENDS
        "${SLINKY_EMBED_SCRIPT}"
        ${SLINKY_ASSET_FILES}
        ${SLINKY_JAVA_CLASSES}
    COMMENT "Embedding fonts, icons, and compiled Java hook classes"
    VERBATIM
)

add_custom_target(slinky_embedded_assets
    DEPENDS
        "${SLINKY_GEN_DIR}/embedded_assets.hpp"
        "${SLINKY_GEN_DIR}/embedded_assets.cpp"
)

# ============================================================================
# 2. Third-Party C Dependencies (cJSON, GLAD, NanoVG, MinHook)
# ============================================================================
add_library(slinky_third_party STATIC
    third_party/cjson/cJSON.c
    third_party/glad/src/glad.c
    third_party/nanovg/nanovg.c
    third_party/nanovg/nanovg_gl3_impl.c
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c
    third_party/minhook/src/hde/hde64.c
)

target_include_directories(slinky_third_party PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanovg"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jni"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jni/win32"
)

target_compile_definitions(slinky_third_party PUBLIC
    CJSON_HIDE_SYMBOLS
)

if(MINGW OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(slinky_third_party PRIVATE -w)
endif()

# ============================================================================
# 3. Clean Slinky Client Mod Shared Library (`libslinky.dll`)
#    NOTE: Zero vendor files (`src/vendor/`) are included, linked, or called.
# ============================================================================
set(SLINKY_CORE_SOURCES
    src/main.cpp
    src/core/client.cpp
    src/account/alt_manager.cpp
    src/util/containers.cpp
    src/util/filesystem.cpp
    src/util/input_win32.cpp
    src/util/keycodes.cpp
    src/util/random.cpp
    src/util/time.cpp
    src/events/module_manager.cpp
    src/config/profile_manager.cpp
    src/config/settings.cpp
    src/commands/command_manager.cpp
    src/bytecode/classfile_rewriter.cpp
    src/bytecode/jvmti_installer.cpp
    src/gui/gui_renderer.cpp
    src/gui/widgets.cpp
    src/gui/modals_and_color_picker.cpp
    src/gui/click_gui.cpp
    src/minecraft/item_database.cpp
    src/minecraft/mappings.cpp
    src/minecraft/mc_wrappers.cpp
    src/minecraft/game_helpers.cpp
    src/hooks/channel_handler.cpp
    src/hooks/jni_hooks.cpp
    src/modules/module_registry.cpp
    "${SLINKY_GEN_DIR}/embedded_assets.cpp"
)

set(SLINKY_MODULE_SOURCES
    # Combat (17)
    src/modules/combat/aim_assist.cpp
    src/modules/combat/auto_block.cpp
    src/modules/combat/auto_clicker.cpp
    src/modules/combat/auto_weapon.cpp
    src/modules/combat/backtrack.cpp
    src/modules/combat/criticals.cpp
    src/modules/combat/hit_select.cpp
    src/modules/combat/hitboxes.cpp
    src/modules/combat/knockback_delay.cpp
    src/modules/combat/knockback_displace.cpp
    src/modules/combat/lag_range.cpp
    src/modules/combat/piercing.cpp
    src/modules/combat/reach.cpp
    src/modules/combat/sprint_reset.cpp
    src/modules/combat/timer_range.cpp
    src/modules/combat/velocity.cpp
    src/modules/combat/weapons.cpp

    # Movement (10)
    src/modules/movement/fast_accel.cpp
    src/modules/movement/instant_stop.cpp
    src/modules/movement/inv_walk.cpp
    src/modules/movement/keep_sprint.cpp
    src/modules/movement/no_jump_delay.cpp
    src/modules/movement/no_slow.cpp
    src/modules/movement/null_move.cpp
    src/modules/movement/safe_walk.cpp
    src/modules/movement/sprint.cpp
    src/modules/movement/strafe.cpp

    # Visual (15)
    src/modules/visual/anti_debuff.cpp
    src/modules/visual/block_esp.cpp
    src/modules/visual/chams.cpp
    src/modules/visual/chest_esp.cpp
    src/modules/visual/free_look.cpp
    src/modules/visual/gui.cpp
    src/modules/visual/indicators.cpp
    src/modules/visual/item_esp.cpp
    src/modules/visual/mod_overlay.cpp
    src/modules/visual/name_tags.cpp
    src/modules/visual/no_hurt_cam.cpp
    src/modules/visual/notifications.cpp
    src/modules/visual/player_esp.cpp
    src/modules/visual/pointers.cpp
    src/modules/visual/trajectories.cpp

    # World / Block (7)
    src/modules/world/auto_tool.cpp
    src/modules/world/block_in.cpp
    src/modules/world/bridge_assist.cpp
    src/modules/world/clutch.cpp
    src/modules/world/fast_mine.cpp
    src/modules/world/fast_place.cpp
    src/modules/world/ghost_hand.cpp

    # Utility (18)
    src/modules/utility/anti_bot.cpp
    src/modules/utility/auto_pot.cpp
    src/modules/utility/auto_rod.cpp
    src/modules/utility/auto_soup.cpp
    src/modules/utility/blink.cpp
    src/modules/utility/chest_stealer.cpp
    src/modules/utility/debug.cpp
    src/modules/utility/fake_lag.cpp
    src/modules/utility/friends.cpp
    src/modules/utility/inv_manager.cpp
    src/modules/utility/item_use_fix.cpp
    src/modules/utility/no_hit_delay.cpp
    src/modules/utility/no_item_release.cpp
    src/modules/utility/no_use_delay.cpp
    src/modules/utility/ping_fix.cpp
    src/modules/utility/refill.cpp
    src/modules/utility/teams.cpp
    src/modules/utility/timer.cpp
)

add_library(slinky SHARED
    ${SLINKY_CORE_SOURCES}
    ${SLINKY_MODULE_SOURCES}
)

add_dependencies(slinky slinky_embedded_assets)

set_target_properties(slinky PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "slinky"
)

target_include_directories(slinky PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${SLINKY_GEN_DIR}"
)

target_compile_definitions(slinky PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(slinky PRIVATE
    slinky_third_party
    opengl32
    gdi32
    user32
    shell32
)

if(MINGW)
    target_compile_options(slinky PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_options(slinky PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
    )
endif()

# ============================================================================
# 3b. Standalone Launcher / Injector (`SlinkyClient.exe`)
#     Finds a ready Minecraft JVM, then injects libslinky.dll into it.
# ============================================================================
set(SLINKY_LAUNCHER_DIR "${CMAKE_BINARY_DIR}/SlinkyClient")
add_executable(SlinkyClient launcher/main.cpp)
add_dependencies(SlinkyClient slinky)
set_target_properties(SlinkyClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SLINKY_LAUNCHER_DIR}"
)
target_compile_definitions(SlinkyClient PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(SlinkyClient PRIVATE
    shell32
    user32
)
if(MINGW)
    target_compile_options(SlinkyClient PRIVATE -Wall -Wextra -Wno-unused-parameter -municode)
    target_link_options(SlinkyClient PRIVATE
        -municode
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
    )
endif()
add_custom_command(TARGET SlinkyClient POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:slinky>" "${SLINKY_LAUNCHER_DIR}/libslinky.dll"
    COMMENT "Copying libslinky.dll next to SlinkyClient.exe"
)
# Keep the DLL beside the loader fresh even when only libslinky.dll changes
# (the launcher exe itself does not relink in that case).
add_custom_target(slinky_launcher_dll ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:slinky>" "${SLINKY_LAUNCHER_DIR}/libslinky.dll"
    DEPENDS slinky
    COMMENT "Refreshing libslinky.dll next to SlinkyClient.exe"
)
add_dependencies(SlinkyClient slinky_launcher_dll)

# ============================================================================
# 4. Isolated Reference Target for Restored Vendor Code (`slinky_vendor`)
#    Compiled to verify 100% buildability of `src/vendor/` (AES-128, HWID,
#    HTTPS client, licensing, telemetry, anti-debug/anti-tamper), and
#    STRICTLY NEVER linked into or called by `libslinky.dll`.
# ============================================================================
add_library(slinky_vendor STATIC
    src/vendor/crypto_aes.cpp
    src/vendor/anti_debug_tamper.cpp
    src/vendor/license_entry.cpp
    src/vendor/hwid.cpp
    src/vendor/http_client.cpp
    src/vendor/auth_client.cpp
    third_party/curl/curl_win32_shim.c
    third_party/cjson/cJSON.c
)

target_include_directories(slinky_vendor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/curl/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
)

target_compile_definitions(slinky_vendor PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    CURL_STATICLIB
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(slinky_vendor PRIVATE
    winhttp
    advapi32
    iphlpapi
    user32
)

if(MINGW)
    target_compile_options(slinky_vendor PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
