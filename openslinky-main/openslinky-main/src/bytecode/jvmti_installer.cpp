#include "bytecode/jvmti_installer.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "embedded_assets.hpp"

namespace slinky::bytecode {

namespace {

inline constexpr const char* kHooksClassName = "SlinkyHooks13";

// ============================================================================
// Global State (.data @ 0x180314A00 .. 0x180314C3C)
// ============================================================================
HookCallbacks g_hook_callbacks{};                          // 0x180314A00 (184 bytes)
std::atomic<bool> g_hooks_installed{false};                // 0x180314AB8
jclass g_slinky_hooks_class = nullptr;                     // 0x180314AC0
ClassPatchSpec g_class_patch_specs[kHookedClassCount]{};   // 0x180314AD0 .. 0x180314C37
std::atomic<int> g_hook_rewrite_error{0};                  // 0x180314C38
jvmtiEnv* g_owned_jvmti_env = nullptr;                     // retained when installed via JavaVM* overload

// Persistent storage for materialized hook method entries, instruction targets,
// and method patch descriptors (eliminating stack-pointer lifetime hazards).
struct ActivePatchStorage {
    // [0] Minecraft
    HookMethodEntry mc_hooks[6];
    InsnPatchTarget mc_run_tick_targets[5];
    InsnPatchTarget mc_run_input_tick_targets[2];
    InsnPatchTarget mc_display_targets[1];
    MethodPatchSpec mc_methods[3];

    // [1] EntityRenderer
    HookMethodEntry er_hooks[5];
    InsnPatchTarget er_render_world_targets[2];
    InsnPatchTarget er_setup_camera_targets[1];
    InsnPatchTarget er_update_camera_targets[1];
    MethodPatchSpec er_methods[3];

    // [2] RenderManager
    HookMethodEntry rm_hooks[2];
    InsnPatchTarget rm_cache_targets[2];
    MethodPatchSpec rm_methods[1];

    // [3] RendererLivingEntity
    HookMethodEntry rle_hooks[1];
    InsnPatchTarget rle_can_render_targets[1];
    MethodPatchSpec rle_methods[1];

    // [4] RenderPlayer
    HookMethodEntry rp_hooks[1];
    InsnPatchTarget rp_do_render_targets[1];
    MethodPatchSpec rp_methods[1];

    // [5] NetHandlerPlayClient
    HookMethodEntry nh_hooks[3];
    InsnPatchTarget nh_chunk_data_targets[1];
    InsnPatchTarget nh_chunk_bulk_targets[1];
    InsnPatchTarget nh_send_packet_targets[1];
    MethodPatchSpec nh_methods[3];

    // [6] WorldClient
    HookMethodEntry wc_hooks[3];
    InsnPatchTarget wc_set_block_targets[1];
    InsnPatchTarget wc_pre_chunk_targets[1];
    MethodPatchSpec wc_methods[2];

    // [7] Entity
    HookMethodEntry ent_hooks[1];
    InsnPatchTarget ent_move_targets[1];
    MethodPatchSpec ent_methods[1];

    // [8] EntityLivingBase
    HookMethodEntry elb_hooks[1];
    InsnPatchTarget elb_move_targets[1];
    MethodPatchSpec elb_methods[1];
};

ActivePatchStorage g_patch_storage{};

// ============================================================================
// Static Mapping Tables (.rdata @ 0x180169AC0 .. 0x18016BBB8)
// Indexed by [version_idx (0=1.7.10, 1=1.8.9)][env_idx (0=Notch, 1=MCP, 2=SRG)]
// ============================================================================
struct NameDescPair {
    const char* name;
    const char* desc;
};

// Class owner internal names: [version_idx][is_deobf (0=Notch, 1=MCP/SRG)]
constexpr const char* kClsMinecraft[2][2] = {
    {"bao", "net/minecraft/client/Minecraft"},
    {"ave", "net/minecraft/client/Minecraft"},
};

constexpr const char* kClsEntityRenderer[2][2] = {
    {"blt", "net/minecraft/client/renderer/EntityRenderer"},
    {"bfk", "net/minecraft/client/renderer/EntityRenderer"},
};

constexpr const char* kClsWorldClient[2][2] = {
    {"bjf", "net/minecraft/client/multiplayer/WorldClient"},
    {"bdb", "net/minecraft/client/multiplayer/WorldClient"},
};

constexpr const char* kClsPlayerControllerMP[2][2] = {
    {"bje", "net/minecraft/client/multiplayer/PlayerControllerMP"},
    {"bda", "net/minecraft/client/multiplayer/PlayerControllerMP"},
};

constexpr const char* kClsActiveRenderInfo[2][2] = {
    {"baj", "net/minecraft/client/renderer/ActiveRenderInfo"},
    {"auz", "net/minecraft/client/renderer/ActiveRenderInfo"},
};

constexpr const char* kClsGlStateManager18[2] = {
    "bfl", "net/minecraft/client/renderer/GlStateManager",
};

constexpr const char* kClsRenderManager[2][2] = {
    {"bnn", "net/minecraft/client/renderer/entity/RenderManager"},
    {"biu", "net/minecraft/client/renderer/entity/RenderManager"},
};

constexpr const char* kClsRendererLivingEntity[2][2] = {
    {"boh", "net/minecraft/client/renderer/entity/RendererLivingEntity"},
    {"bjl", "net/minecraft/client/renderer/entity/RendererLivingEntity"},
};

constexpr const char* kClsNetworkManager[2][2] = {
    {"ej", "net/minecraft/network/NetworkManager"},
    {"ek", "net/minecraft/network/NetworkManager"},
};

constexpr const char* kClsWorld[2][2] = {
    {"ahb", "net/minecraft/world/World"},
    {"adm", "net/minecraft/world/World"},
};

constexpr const char* kClsChunkProviderClient[2][2] = {
    {"bjd", "net/minecraft/client/multiplayer/ChunkProviderClient"},
    {"bcz", "net/minecraft/client/multiplayer/ChunkProviderClient"},
};

constexpr const char* kClsEntity[2][2] = {
    {"sa", "net/minecraft/entity/Entity"},
    {"pk", "net/minecraft/entity/Entity"},
};

constexpr const char* kClsEntityLivingBase[2][2] = {
    {"sv", "net/minecraft/entity/EntityLivingBase"},
    {"pr", "net/minecraft/entity/EntityLivingBase"},
};

// [0] Minecraft method & target tables
constexpr NameDescPair kMcRunTick[2][3] = {
    {{"p", "()V"}, {"runTick", "()V"}, {"func_71407_l", "()V"}},
    {{"s", "()V"}, {"runTick", "()V"}, {"func_71407_l", "()V"}},
};

constexpr NameDescPair kMcUpdateDisplayMethod[2][3] = {
    {{"g", "()V"}, {"resetSize", "()V"}, {"func_147120_f", "()V"}},
    {{"h", "()V"}, {"updateDisplay", "()V"}, {"func_175601_h", "()V"}},
};

constexpr NameDescPair kErGetMouseOver[2][3] = {
    {{"a", "(F)V"}, {"getMouseOver", "(F)V"}, {"func_78473_a", "(F)V"}},
    {{"a", "(F)V"}, {"getMouseOver", "(F)V"}, {"func_78473_a", "(F)V"}},
};

constexpr NameDescPair kMcClickMouse[2][3] = {
    {{"al", "()V"}, {"clickMouse", "()V"}, {"func_147116_af", "()V"}},
    {{"aw", "()V"}, {"clickMouse", "()V"}, {"func_147116_af", "()V"}},
};

constexpr NameDescPair kMcRightClickMouse[2][3] = {
    {{"am", "()V"}, {"rightClickMouse", "()V"}, {"func_147121_ag", "()V"}},
    {{"ax", "()V"}, {"rightClickMouse", "()V"}, {"func_147121_ag", "()V"}},
};

constexpr NameDescPair kWcUpdateEntities[2][3] = {
    {{"h", "()V"}, {"updateEntities", "()V"}, {"func_72939_s", "()V"}},
    {{"i", "()V"}, {"updateEntities", "()V"}, {"func_72939_s", "()V"}},
};

constexpr NameDescPair kPcUpdateController[2][3] = {
    {{"e", "()V"}, {"updateController", "()V"}, {"func_78765_e", "()V"}},
    {{"e", "()V"}, {"updateController", "()V"}, {"func_78765_e", "()V"}},
};

// [1] EntityRenderer method & target tables
constexpr NameDescPair kErRenderWorld[2][3] = {
    {{"a", "(FJ)V"}, {"renderWorld", "(FJ)V"}, {"func_78471_a", "(FJ)V"}},
    {{"a", "(IFJ)V"}, {"renderWorldPass", "(IFJ)V"}, {"func_175068_a", "(IFJ)V"}},
};

constexpr NameDescPair kGlStateClear18[3] = {
    {"m", "(I)V"}, {"clear", "(I)V"}, {"func_179086_m", "(I)V"},
};

constexpr NameDescPair kAriUpdateRenderInfo[2][3] = {
    {
        {"a", "(Lyz;Z)V"},
        {"updateRenderInfo", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"},
        {"func_74583_a", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"},
    },
    {
        {"a", "(Lwn;Z)V"},
        {"updateRenderInfo", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"},
        {"func_74583_a", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"},
    },
};

constexpr NameDescPair kErSetupCameraTransform[2][3] = {
    {{"a", "(FI)V"}, {"setupCameraTransform", "(FI)V"}, {"func_78479_a", "(FI)V"}},
    {{"a", "(FI)V"}, {"setupCameraTransform", "(FI)V"}, {"func_78479_a", "(FI)V"}},
};

constexpr NameDescPair kErOrientCamera[2][3] = {
    {{"h", "(F)V"}, {"orientCamera", "(F)V"}, {"func_78467_g", "(F)V"}},
    {{"f", "(F)V"}, {"orientCamera", "(F)V"}, {"func_78467_g", "(F)V"}},
};

constexpr NameDescPair kErUpdateCameraAndRender[2][3] = {
    {{"b", "(F)V"}, {"updateCameraAndRender", "(F)V"}, {"func_78480_b", "(F)V"}},
    {{"a", "(FJ)V"}, {"updateCameraAndRender", "(FJ)V"}, {"func_181560_a", "(FJ)V"}},
};

// [2] RenderManager method & field tables
constexpr NameDescPair kRmCacheActiveRenderInfo[2][3] = {
    {
        {"a", "(Lahb;Lbqf;Lbbu;Lsv;Lsa;Lbbj;F)V"},
        {"cacheActiveRenderInfo", "(Lnet/minecraft/world/World;Lnet/minecraft/client/renderer/texture/TextureManager;Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/entity/EntityLivingBase;Lnet/minecraft/entity/Entity;Lnet/minecraft/client/settings/GameSettings;F)V"},
        {"func_147938_a", "(Lnet/minecraft/world/World;Lnet/minecraft/client/renderer/texture/TextureManager;Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/entity/EntityLivingBase;Lnet/minecraft/entity/Entity;Lnet/minecraft/client/settings/GameSettings;F)V"},
    },
    {
        {"a", "(Ladm;Lavn;Lpk;Lpk;Lavh;F)V"},
        {"cacheActiveRenderInfo", "(Lnet/minecraft/world/World;Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/entity/Entity;Lnet/minecraft/entity/Entity;Lnet/minecraft/client/settings/GameSettings;F)V"},
        {"func_180597_a", "(Lnet/minecraft/world/World;Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/entity/Entity;Lnet/minecraft/entity/Entity;Lnet/minecraft/client/settings/GameSettings;F)V"},
    },
};

constexpr NameDescPair kRmFieldPlayerViewX[2][3] = {
    {{"k", "F"}, {"playerViewX", "F"}, {"field_78732_j", "F"}},
    {{"f", "F"}, {"playerViewX", "F"}, {"field_78732_j", "F"}},
};

constexpr NameDescPair kRmFieldPlayerViewY[2][3] = {
    {{"j", "F"}, {"playerViewY", "F"}, {"field_78735_i", "F"}},
    {{"e", "F"}, {"playerViewY", "F"}, {"field_78735_i", "F"}},
};

// [3] & [4] Fallback defaults for RendererLivingEntity & RenderPlayer if Mappings slots are null
constexpr NameDescPair kFallbackCanRenderName[2][3] = {
    {
        {"b", "(Lsv;)Z"},
        {"canRenderName", "(Lnet/minecraft/entity/EntityLivingBase;)Z"},
        {"func_110813_b", "(Lnet/minecraft/entity/EntityLivingBase;)Z"},
    },
    {
        {"a", "(Lpr;)Z"},
        {"canRenderName", "(Lnet/minecraft/entity/EntityLivingBase;)Z"},
        {"func_177070_b", "(Lnet/minecraft/entity/EntityLivingBase;)Z"},
    },
};

constexpr NameDescPair kFallbackLivingDoRender[2][3] = {
    {
        {"a", "(Lsv;DDDFF)V"},
        {"doRender", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"},
        {"func_76986_a", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"},
    },
    {
        {"a", "(Lpr;DDDFF)V"},
        {"doRender", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"},
        {"func_76986_a", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"},
    },
};

constexpr NameDescPair kFallbackRenderPlayerDoRender[2][3] = {
    {
        {"a", "(Lblg;DDDFF)V"},
        {"doRender", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"},
        {"func_76986_a", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"},
    },
    {
        {"a", "(Lbet;DDDFF)V"},
        {"doRender", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"},
        {"func_76986_a", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"},
    },
};

// [5] NetHandlerPlayClient method & target tables
constexpr NameDescPair kNhHandleChunkData[2][3] = {
    {
        {"a", "(Lgx;)V"},
        {"handleChunkData", "(Lnet/minecraft/network/play/server/S21PacketChunkData;)V"},
        {"func_147263_a", "(Lnet/minecraft/network/play/server/S21PacketChunkData;)V"},
    },
    {
        {"a", "(Lgo;)V"},
        {"handleChunkData", "(Lnet/minecraft/network/play/server/S21PacketChunkData;)V"},
        {"func_147263_a", "(Lnet/minecraft/network/play/server/S21PacketChunkData;)V"},
    },
};

constexpr NameDescPair kNhHandleMapChunkBulk[2][3] = {
    {
        {"a", "(Lgz;)V"},
        {"handleMapChunkBulk", "(Lnet/minecraft/network/play/server/S26PacketMapChunkBulk;)V"},
        {"func_147269_a", "(Lnet/minecraft/network/play/server/S26PacketMapChunkBulk;)V"},
    },
    {
        {"a", "(Lgp;)V"},
        {"handleMapChunkBulk", "(Lnet/minecraft/network/play/server/S26PacketMapChunkBulk;)V"},
        {"func_147269_a", "(Lnet/minecraft/network/play/server/S26PacketMapChunkBulk;)V"},
    },
};

constexpr NameDescPair kWcMarkBlockRange[2][3] = {
    {
        {"c", "(IIIIII)V"},
        {"markBlockRangeForRenderUpdate", "(IIIIII)V"},
        {"func_147458_c", "(IIIIII)V"},
    },
    {
        {"b", "(IIIIII)V"},
        {"markBlockRangeForRenderUpdate", "(IIIIII)V"},
        {"func_147458_c", "(IIIIII)V"},
    },
};

constexpr NameDescPair kNhAddToSendQueue[2][3] = {
    {
        {"a", "(Lft;)V"},
        {"addToSendQueue", "(Lnet/minecraft/network/Packet;)V"},
        {"func_147297_a", "(Lnet/minecraft/network/Packet;)V"},
    },
    {
        {"a", "(Lff;)V"},
        {"addToSendQueue", "(Lnet/minecraft/network/Packet;)V"},
        {"func_147297_a", "(Lnet/minecraft/network/Packet;)V"},
    },
};

constexpr NameDescPair kNmSendPacket[2][3] = {
    {
        {"a", "(Lft;[Lio/netty/util/concurrent/GenericFutureListener;)V"},
        {"scheduleOutboundPacket", "(Lnet/minecraft/network/Packet;[Lio/netty/util/concurrent/GenericFutureListener;)V"},
        {"func_150725_a", "(Lnet/minecraft/network/Packet;[Lio/netty/util/concurrent/GenericFutureListener;)V"},
    },
    {
        {"a", "(Lff;)V"},
        {"sendPacket", "(Lnet/minecraft/network/Packet;)V"},
        {"func_179290_a", "(Lnet/minecraft/network/Packet;)V"},
    },
};

// [6] WorldClient method & target tables
constexpr NameDescPair kWcInvalidateRegionAndSetBlock[2][3] = {
    {
        {"c", "(IIILaji;I)Z"},
        {"func_147492_c", "(IIILnet/minecraft/block/Block;I)Z"},
        {"func_147492_c", "(IIILnet/minecraft/block/Block;I)Z"},
    },
    {
        {"b", "(Lcj;Lalz;)Z"},
        {"invalidateRegionAndSetBlock", "(Lnet/minecraft/util/BlockPos;Lnet/minecraft/block/state/IBlockState;)Z"},
        {"func_180503_b", "(Lnet/minecraft/util/BlockPos;Lnet/minecraft/block/state/IBlockState;)Z"},
    },
};

constexpr NameDescPair kWorldSetBlock[2][3] = {
    {
        {"d", "(IIILaji;II)Z"},
        {"setBlock", "(IIILnet/minecraft/block/Block;II)Z"},
        {"func_147465_d", "(IIILnet/minecraft/block/Block;II)Z"},
    },
    {
        {"a", "(Lcj;Lalz;I)Z"},
        {"setBlockState", "(Lnet/minecraft/util/BlockPos;Lnet/minecraft/block/state/IBlockState;I)Z"},
        {"func_180501_a", "(Lnet/minecraft/util/BlockPos;Lnet/minecraft/block/state/IBlockState;I)Z"},
    },
};

constexpr NameDescPair kWcDoPreChunk[2][3] = {
    {{"a", "(IIZ)V"}, {"doPreChunk", "(IIZ)V"}, {"func_73025_a", "(IIZ)V"}},
    {{"b", "(IIZ)V"}, {"doPreChunk", "(IIZ)V"}, {"func_73025_a", "(IIZ)V"}},
};

constexpr NameDescPair kCpcUnloadChunk[2][3] = {
    {{"b", "(II)V"}, {"unloadChunk", "(II)V"}, {"func_73234_b", "(II)V"}},
    {{"b", "(II)V"}, {"unloadChunk", "(II)V"}, {"func_73234_b", "(II)V"}},
};

// [7] Entity method & target tables
constexpr NameDescPair kEntMoveEntity[2][3] = {
    {{"d", "(DDD)V"}, {"moveEntity", "(DDD)V"}, {"func_70091_d", "(DDD)V"}},
    {{"d", "(DDD)V"}, {"moveEntity", "(DDD)V"}, {"func_70091_d", "(DDD)V"}},
};

constexpr NameDescPair kEntIsSneaking[2][3] = {
    {{"an", "()Z"}, {"isSneaking", "()Z"}, {"func_70093_af", "()Z"}},
    {{"av", "()Z"}, {"isSneaking", "()Z"}, {"func_70093_af", "()Z"}},
};

// [8] EntityLivingBase method & target tables
constexpr NameDescPair kElbMoveEntityWithHeading[2][3] = {
    {{"e", "(FF)V"}, {"moveEntityWithHeading", "(FF)V"}, {"func_70612_e", "(FF)V"}},
    {{"g", "(FF)V"}, {"moveEntityWithHeading", "(FF)V"}, {"func_70612_e", "(FF)V"}},
};

constexpr NameDescPair kElbMoveFlying[2][3] = {
    {{"a", "(FFF)V"}, {"moveFlying", "(FFF)V"}, {"func_70060_a", "(FFF)V"}},
    {{"a", "(FFF)V"}, {"moveFlying", "(FFF)V"}, {"func_70060_a", "(FFF)V"}},
};

template <typename T>
T read_raw_offset(const void* base, size_t byte_offset) noexcept {
    T val{};
    std::memcpy(&val, static_cast<const uint8_t*>(base) + byte_offset, sizeof(T));
    return val;
}

void build_active_patch_specs(const HookTargetContext& target) noexcept {
    const size_t v = (target.mc_version == McVersion::V1_7_10) ? 0u : 1u;
    size_t e = static_cast<size_t>(target.mapping_env);
    if (e > 2u) {
        e = 0u;
    }
    const size_t deobf = (e != 0u) ? 1u : 0u;

    ActivePatchStorage& s = g_patch_storage;

    // ------------------------------------------------------------------------
    // [0] Minecraft (Mappings + 0x178)
    // ------------------------------------------------------------------------
    s.mc_hooks[0] = {"getMouseOver", "(Ljava/lang/Object;F)V", 0};
    s.mc_hooks[1] = {"updateDisplay", "()V", 0};
    s.mc_hooks[2] = {"clickMouse", "(Ljava/lang/Object;)V", 0};
    s.mc_hooks[3] = {"rightClickMouse", "(Ljava/lang/Object;)V", 0};
    s.mc_hooks[4] = {"updateEntities", "(Ljava/lang/Object;)V", 0};
    s.mc_hooks[5] = {"updateController", "(Ljava/lang/Object;)V", 0};

    s.mc_run_tick_targets[0] = {
        kClsEntityRenderer[v][deobf], kErGetMouseOver[v][e].name, kErGetMouseOver[v][e].desc,
        HookInsnKind::InvokeInstance, &s.mc_hooks[0].resolved_cp_index};
    s.mc_run_tick_targets[1] = {
        kClsMinecraft[v][deobf], kMcClickMouse[v][e].name, kMcClickMouse[v][e].desc,
        HookInsnKind::InvokeInstance, &s.mc_hooks[2].resolved_cp_index};
    s.mc_run_tick_targets[2] = {
        kClsMinecraft[v][deobf], kMcRightClickMouse[v][e].name, kMcRightClickMouse[v][e].desc,
        HookInsnKind::InvokeInstance, &s.mc_hooks[3].resolved_cp_index};
    s.mc_run_tick_targets[3] = {
        kClsWorldClient[v][deobf], kWcUpdateEntities[v][e].name, kWcUpdateEntities[v][e].desc,
        HookInsnKind::InvokeInstance, &s.mc_hooks[4].resolved_cp_index};
    s.mc_run_tick_targets[4] = {
        kClsPlayerControllerMP[v][deobf], kPcUpdateController[v][e].name, kPcUpdateController[v][e].desc,
        HookInsnKind::InvokeInstance, &s.mc_hooks[5].resolved_cp_index};

    s.mc_run_input_tick_targets[0] = s.mc_run_tick_targets[1];
    s.mc_run_input_tick_targets[1] = s.mc_run_tick_targets[2];

    s.mc_display_targets[0] = {
        "org/lwjgl/opengl/Display", "update", "()V",
        HookInsnKind::InvokeStatic, &s.mc_hooks[1].resolved_cp_index};

    s.mc_methods[0] = {kMcRunTick[v][e].name, kMcRunTick[v][e].desc, s.mc_run_tick_targets, 5};
    s.mc_methods[1] = {"runInputTick", "()V", s.mc_run_input_tick_targets, 2};
    s.mc_methods[2] = {kMcUpdateDisplayMethod[v][e].name, kMcUpdateDisplayMethod[v][e].desc, s.mc_display_targets, 1};

    g_class_patch_specs[0] = {target.cls_minecraft, s.mc_hooks, 6, s.mc_methods, 3};

    // ------------------------------------------------------------------------
    // [1] EntityRenderer (Mappings + 0x980)
    // ------------------------------------------------------------------------
    s.er_hooks[0] = {"postWorldClear_1_7", "(I)V", 0};
    s.er_hooks[1] = {"postWorldClear_1_8", "(I)V", 0};
    s.er_hooks[2] = {"orientCamera", "(Ljava/lang/Object;F)V", 0};
    s.er_hooks[3] = {"updateActiveRenderInfo", "(Ljava/lang/Object;Z)V", 0};
    s.er_hooks[4] = {"isDisplayActivePreRender", "()Z", 0};

    if (v == 0u) {
        s.er_render_world_targets[0] = {
            "org/lwjgl/opengl/GL11", "glClear", "(I)V",
            HookInsnKind::InvokeStatic, &s.er_hooks[0].resolved_cp_index};
    } else {
        s.er_render_world_targets[0] = {
            kClsGlStateManager18[deobf], kGlStateClear18[e].name, kGlStateClear18[e].desc,
            HookInsnKind::InvokeStatic, &s.er_hooks[1].resolved_cp_index};
    }
    s.er_render_world_targets[1] = {
        kClsActiveRenderInfo[v][deobf], kAriUpdateRenderInfo[v][e].name, kAriUpdateRenderInfo[v][e].desc,
        HookInsnKind::InvokeStatic, &s.er_hooks[3].resolved_cp_index};

    s.er_setup_camera_targets[0] = {
        kClsEntityRenderer[v][deobf], kErOrientCamera[v][e].name, kErOrientCamera[v][e].desc,
        HookInsnKind::InvokeInstance, &s.er_hooks[2].resolved_cp_index};

    s.er_update_camera_targets[0] = {
        "org/lwjgl/opengl/Display", "isActive", "()Z",
        HookInsnKind::InvokeStatic, &s.er_hooks[4].resolved_cp_index};

    s.er_methods[0] = {kErRenderWorld[v][e].name, kErRenderWorld[v][e].desc, s.er_render_world_targets, 2};
    s.er_methods[1] = {kErSetupCameraTransform[v][e].name, kErSetupCameraTransform[v][e].desc, s.er_setup_camera_targets, 1};
    s.er_methods[2] = {kErUpdateCameraAndRender[v][e].name, kErUpdateCameraAndRender[v][e].desc, s.er_update_camera_targets, 1};

    g_class_patch_specs[1] = {target.cls_entity_renderer, s.er_hooks, 5, s.er_methods, 3};

    // ------------------------------------------------------------------------
    // [2] RenderManager (Mappings + 0x848)
    // ------------------------------------------------------------------------
    s.rm_hooks[0] = {"setRenderX", "(Ljava/lang/Object;F)V", 0};
    s.rm_hooks[1] = {"setRenderY", "(Ljava/lang/Object;F)V", 0};

    s.rm_cache_targets[0] = {
        kClsRenderManager[v][deobf], kRmFieldPlayerViewX[v][e].name, kRmFieldPlayerViewX[v][e].desc,
        HookInsnKind::PutField, &s.rm_hooks[0].resolved_cp_index};
    s.rm_cache_targets[1] = {
        kClsRenderManager[v][deobf], kRmFieldPlayerViewY[v][e].name, kRmFieldPlayerViewY[v][e].desc,
        HookInsnKind::PutField, &s.rm_hooks[1].resolved_cp_index};

    s.rm_methods[0] = {
        kRmCacheActiveRenderInfo[v][e].name, kRmCacheActiveRenderInfo[v][e].desc, s.rm_cache_targets, 2};

    g_class_patch_specs[2] = {target.cls_render_manager, s.rm_hooks, 2, s.rm_methods, 1};

    // ------------------------------------------------------------------------
    // [3] RendererLivingEntity (Mappings + 0xB40)
    // ------------------------------------------------------------------------
    s.rle_hooks[0] = {"canRenderName", "(Ljava/lang/Object;Ljava/lang/Object;)Z", 0};

    const char* can_render_name = target.can_render_name_name
                                      ? target.can_render_name_name
                                      : kFallbackCanRenderName[v][e].name;
    const char* can_render_desc = target.can_render_name_desc
                                      ? target.can_render_name_desc
                                      : kFallbackCanRenderName[v][e].desc;

    s.rle_can_render_targets[0] = {
        kClsRendererLivingEntity[v][deobf], can_render_name, can_render_desc,
        HookInsnKind::InvokeInstance, &s.rle_hooks[0].resolved_cp_index};

    s.rle_methods[0] = {
        wildcard_method_str(), wildcard_method_str(), s.rle_can_render_targets, 1};

    g_class_patch_specs[3] = {target.cls_renderer_living_entity, s.rle_hooks, 1, s.rle_methods, 1};

    // ------------------------------------------------------------------------
    // [4] RenderPlayer (Mappings + 0xC80)
    // ------------------------------------------------------------------------
    s.rp_hooks[0] = {"renderPlayer", "(Ljava/lang/Object;Ljava/lang/Object;DDDFF)V", 0};

    const char* living_do_render_name = target.living_do_render_name
                                            ? target.living_do_render_name
                                            : kFallbackLivingDoRender[v][e].name;
    const char* living_do_render_desc = target.living_do_render_desc
                                            ? target.living_do_render_desc
                                            : kFallbackLivingDoRender[v][e].desc;
    const char* rp_do_render_name = target.render_player_do_render_name
                                        ? target.render_player_do_render_name
                                        : kFallbackRenderPlayerDoRender[v][e].name;
    const char* rp_do_render_desc = target.render_player_do_render_desc
                                        ? target.render_player_do_render_desc
                                        : kFallbackRenderPlayerDoRender[v][e].desc;

    s.rp_do_render_targets[0] = {
        kClsRendererLivingEntity[v][deobf], living_do_render_name, living_do_render_desc,
        HookInsnKind::InvokeInstance, &s.rp_hooks[0].resolved_cp_index};

    s.rp_methods[0] = {rp_do_render_name, rp_do_render_desc, s.rp_do_render_targets, 1};

    g_class_patch_specs[4] = {target.cls_render_player, s.rp_hooks, 1, s.rp_methods, 1};

    // ------------------------------------------------------------------------
    // [5] NetHandlerPlayClient (Mappings + 0xB08)
    // ------------------------------------------------------------------------
    s.nh_hooks[0] = {"invalidateChunk", "(Ljava/lang/Object;IIIIII)V", 0};
    s.nh_hooks[1] = {"sendPacket_1_7", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V", 0};
    s.nh_hooks[2] = {"sendPacket_1_8", "(Ljava/lang/Object;Ljava/lang/Object;)V", 0};

    s.nh_chunk_data_targets[0] = {
        kClsWorldClient[v][deobf], kWcMarkBlockRange[v][e].name, kWcMarkBlockRange[v][e].desc,
        HookInsnKind::InvokeInstance, &s.nh_hooks[0].resolved_cp_index};
    s.nh_chunk_bulk_targets[0] = s.nh_chunk_data_targets[0];

    const uint16_t* send_packet_cp_ptr = (v == 0u)
                                             ? &s.nh_hooks[1].resolved_cp_index
                                             : &s.nh_hooks[2].resolved_cp_index;
    s.nh_send_packet_targets[0] = {
        kClsNetworkManager[v][deobf], kNmSendPacket[v][e].name, kNmSendPacket[v][e].desc,
        HookInsnKind::InvokeInstance, send_packet_cp_ptr};

    s.nh_methods[0] = {kNhHandleChunkData[v][e].name, kNhHandleChunkData[v][e].desc, s.nh_chunk_data_targets, 1};
    s.nh_methods[1] = {kNhHandleMapChunkBulk[v][e].name, kNhHandleMapChunkBulk[v][e].desc, s.nh_chunk_bulk_targets, 1};
    s.nh_methods[2] = {kNhAddToSendQueue[v][e].name, kNhAddToSendQueue[v][e].desc, s.nh_send_packet_targets, 1};

    g_class_patch_specs[5] = {target.cls_net_handler_play, s.nh_hooks, 3, s.nh_methods, 3};

    // ------------------------------------------------------------------------
    // [6] WorldClient (Mappings + 0xA30)
    // ------------------------------------------------------------------------
    s.wc_hooks[0] = {"unloadChunk", "(Ljava/lang/Object;II)V", 0};
    s.wc_hooks[1] = {"setBlock_1_7", "(Ljava/lang/Object;IIILjava/lang/Object;II)Z", 0};
    s.wc_hooks[2] = {"setBlock_1_8", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;I)Z", 0};

    const uint16_t* set_block_cp_ptr = (v == 0u)
                                           ? &s.wc_hooks[1].resolved_cp_index
                                           : &s.wc_hooks[2].resolved_cp_index;
    s.wc_set_block_targets[0] = {
        kClsWorld[v][deobf], kWorldSetBlock[v][e].name, kWorldSetBlock[v][e].desc,
        HookInsnKind::InvokeInstance, set_block_cp_ptr};

    s.wc_pre_chunk_targets[0] = {
        kClsChunkProviderClient[v][deobf], kCpcUnloadChunk[v][e].name, kCpcUnloadChunk[v][e].desc,
        HookInsnKind::InvokeInstance, &s.wc_hooks[0].resolved_cp_index};

    s.wc_methods[0] = {
        kWcInvalidateRegionAndSetBlock[v][e].name, kWcInvalidateRegionAndSetBlock[v][e].desc,
        s.wc_set_block_targets, 1};
    s.wc_methods[1] = {
        kWcDoPreChunk[v][e].name, kWcDoPreChunk[v][e].desc,
        s.wc_pre_chunk_targets, 1};

    g_class_patch_specs[6] = {target.cls_world_client, s.wc_hooks, 3, s.wc_methods, 2};

    // ------------------------------------------------------------------------
    // [7] Entity (Mappings + 0x248)
    // ------------------------------------------------------------------------
    s.ent_hooks[0] = {"shouldClipLedge", "(Ljava/lang/Object;)Z", 0};

    s.ent_move_targets[0] = {
        kClsEntity[v][deobf], kEntIsSneaking[v][e].name, kEntIsSneaking[v][e].desc,
        HookInsnKind::InvokeInstance, &s.ent_hooks[0].resolved_cp_index};

    s.ent_methods[0] = {
        kEntMoveEntity[v][e].name, kEntMoveEntity[v][e].desc, s.ent_move_targets, 1};

    g_class_patch_specs[7] = {target.cls_entity, s.ent_hooks, 1, s.ent_methods, 1};

    // ------------------------------------------------------------------------
    // [8] EntityLivingBase (Mappings + 0x610)
    // ------------------------------------------------------------------------
    s.elb_hooks[0] = {"moveFlying", "(Ljava/lang/Object;FFF)V", 0};

    s.elb_move_targets[0] = {
        kClsEntityLivingBase[v][deobf], kElbMoveFlying[v][e].name, kElbMoveFlying[v][e].desc,
        HookInsnKind::InvokeInstance, &s.elb_hooks[0].resolved_cp_index};

    s.elb_methods[0] = {
        kElbMoveEntityWithHeading[v][e].name, kElbMoveEntityWithHeading[v][e].desc, s.elb_move_targets, 1};

    g_class_patch_specs[8] = {target.cls_entity_living_base, s.elb_hooks, 1, s.elb_methods, 1};
}

// sub_180040A60: Load class by name via ClassLoader.loadClass(String), converting '/' -> '.'
jclass try_load_class_via_loader(
    JNIEnv* env,
    jobject class_loader,
    jmethodID load_class_mid,
    const char* class_name) {
    if (!env || !class_name) {
        return nullptr;
    }
    if (!class_loader) {
        return env->FindClass(class_name);
    }

    if (!load_class_mid) {
        jclass loader_cls = env->GetObjectClass(class_loader);
        if (!loader_cls) {
            env->ExceptionClear();
            return nullptr;
        }
        load_class_mid = env->GetMethodID(loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        env->DeleteLocalRef(loader_cls);
        if (!load_class_mid) {
            env->ExceptionClear();
            return nullptr;
        }
    }

    std::string dot_name(class_name);
    for (char& ch : dot_name) {
        if (ch == '/') {
            ch = '.';
        }
    }

    jstring jname = env->NewStringUTF(dot_name.c_str());
    if (!jname) {
        env->ExceptionClear();
        return nullptr;
    }

    auto loaded = static_cast<jclass>(env->CallObjectMethod(class_loader, load_class_mid, jname));
    env->DeleteLocalRef(jname);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return loaded;
}

} // namespace

// ============================================================================
// Public Functions
// ============================================================================

HookTargetContext extract_hook_targets_from_mappings(const mc::Mappings* mappings) noexcept {
    HookTargetContext out{};
    if (!mappings) {
        return out;
    }

    out.mc_version        = static_cast<McVersion>(read_raw_offset<uint32_t>(mappings, 0x000));
    out.mapping_env       = static_cast<MappingEnv>(read_raw_offset<uint32_t>(mappings, 0x004));
    out.class_loader      = read_raw_offset<jobject>(mappings, 0x010);
    out.load_class_mid    = read_raw_offset<jmethodID>(mappings, 0x018);

    out.cls_minecraft              = read_raw_offset<jclass>(mappings, 0x178);
    out.cls_entity_renderer        = read_raw_offset<jclass>(mappings, 0x980);
    out.cls_render_manager         = read_raw_offset<jclass>(mappings, 0x848);
    out.cls_renderer_living_entity = read_raw_offset<jclass>(mappings, 0xB40);
    out.cls_render_player          = read_raw_offset<jclass>(mappings, 0xC80);
    out.cls_net_handler_play       = read_raw_offset<jclass>(mappings, 0xB08);
    out.cls_world_client           = read_raw_offset<jclass>(mappings, 0xA30);
    out.cls_entity                 = read_raw_offset<jclass>(mappings, 0x248);
    out.cls_entity_living_base     = read_raw_offset<jclass>(mappings, 0x610);

    out.can_render_name_name          = read_raw_offset<const char*>(mappings, 0xB60);
    out.can_render_name_desc          = read_raw_offset<const char*>(mappings, 0xB68);
    out.living_do_render_name         = read_raw_offset<const char*>(mappings, 0xB78);
    out.living_do_render_desc         = read_raw_offset<const char*>(mappings, 0xB80);
    out.render_player_do_render_name  = read_raw_offset<const char*>(mappings, 0xC88);
    out.render_player_do_render_desc  = read_raw_offset<const char*>(mappings, 0xC90);

    return out;
}

// sub_180042690
void set_hook_callbacks(const HookCallbacks& callbacks) noexcept {
    g_hook_callbacks = callbacks;
}

// sub_180042690
void set_hook_callbacks(const void* callbacks_184_bytes) noexcept {
    if (callbacks_184_bytes) {
        std::memcpy(&g_hook_callbacks, callbacks_184_bytes, sizeof(HookCallbacks));
    }
}

// sub_18002CB90
int define_and_register_hooks_class(
    JNIEnv* env,
    const char* class_name,
    jobject class_loader,
    jmethodID load_class_mid,
    const uint8_t* class_bytes,
    jsize class_len,
    const JNINativeMethod* methods,
    jint method_count,
    jclass* out_local_class) {
    if (!env || !class_name || !class_bytes || class_len <= 0 || !out_local_class) {
        return kErrDefineClassFailed;
    }

    jclass cls = try_load_class_via_loader(env, class_loader, load_class_mid, class_name);
    if (!cls) {
        env->ExceptionClear();
        cls = env->DefineClass(
            class_name,
            class_loader,
            reinterpret_cast<const jbyte*>(class_bytes),
            class_len);
        if (!cls) {
            env->ExceptionClear();
            return kErrDefineClassFailed;
        }
    }

    if (methods && method_count > 0) {
        if (env->RegisterNatives(cls, methods, method_count) != JNI_OK) {
            env->ExceptionClear();
            env->DeleteLocalRef(cls);
            return kErrRegisterNativesFailed;
        }
    }

    *out_local_class = cls;
    return kInstallOk;
}

// sub_180043A30
void JNICALL jvmti_class_file_load_hook(
    jvmtiEnv* jvmti_env,
    JNIEnv* jni_env,
    jclass class_being_redefined,
    jobject /*loader*/,
    const char* /*name*/,
    jobject /*protection_domain*/,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data) {
    if (g_hook_rewrite_error.load(std::memory_order_acquire) != 0) {
        return;
    }
    if (!jvmti_env || !jni_env || !class_being_redefined || !class_data ||
        class_data_len <= 0 || !new_class_data_len || !new_class_data) {
        return;
    }

    ClassPatchSpec* matched_spec = nullptr;
    for (size_t i = 0; i < kHookedClassCount; ++i) {
        if (g_class_patch_specs[i].target_class != nullptr &&
            jni_env->IsSameObject(g_class_patch_specs[i].target_class, class_being_redefined) == JNI_TRUE) {
            matched_spec = &g_class_patch_specs[i];
            break;
        }
    }
    if (!matched_spec) {
        return;
    }

    uint8_t* rewritten_bytes = nullptr;
    uint32_t rewritten_len = 0;
    int err = rewrite_class_bytecode(
        class_data,
        class_data_len,
        &rewritten_bytes,
        &rewritten_len,
        kHooksClassName,
        matched_spec->hook_methods,
        static_cast<int>(matched_spec->hook_method_count),
        &select_method_patcher,
        matched_spec);
    if (err != kRewriteOk) {
        int expected = 0;
        g_hook_rewrite_error.compare_exchange_strong(expected, err, std::memory_order_acq_rel);
        return;
    }

    unsigned char* jvmti_buf = nullptr;
    const jvmtiError alloc_err = jvmti_env->Allocate(static_cast<jlong>(rewritten_len), &jvmti_buf);
    if (alloc_err != JVMTI_ERROR_NONE) {
        std::free(rewritten_bytes);
        const int reported = static_cast<int>(alloc_err) + kErrJvmtiBase;
        int expected = 0;
        g_hook_rewrite_error.compare_exchange_strong(expected, reported, std::memory_order_acq_rel);
        return;
    }

    std::memcpy(jvmti_buf, rewritten_bytes, rewritten_len);
    *new_class_data = jvmti_buf;
    *new_class_data_len = static_cast<jint>(rewritten_len);
    std::free(rewritten_bytes);
}

// sub_1800426B0
int install_jvmti_hooks(JNIEnv* env, jvmtiEnv* jvmti, const HookTargetContext& target) {
    if (g_hooks_installed.load(std::memory_order_acquire)) {
        return kInstallOk;
    }
    if (!env || !jvmti) {
        return kErrGetJvmtiEnvFailed;
    }

    // Build exact 23 JNINativeMethod table ([rbp+0x800 .. rbp+0xA28] in sub_1800426B0)
    const JNINativeMethod native_methods[kSlinkyHookMethodCount] = {
        {const_cast<char*>("getMouseOver"),             const_cast<char*>("(Ljava/lang/Object;F)V"),                                     reinterpret_cast<void*>(g_hook_callbacks.get_mouse_over)},
        {const_cast<char*>("isDisplayActivePreRender"), const_cast<char*>("()Z"),                                                        reinterpret_cast<void*>(g_hook_callbacks.is_display_active_pre_render)},
        {const_cast<char*>("updateDisplay"),            const_cast<char*>("()V"),                                                        reinterpret_cast<void*>(g_hook_callbacks.update_display)},
        {const_cast<char*>("postWorldClear_1_7"),       const_cast<char*>("(I)V"),                                                       reinterpret_cast<void*>(g_hook_callbacks.post_world_clear_1_7)},
        {const_cast<char*>("postWorldClear_1_8"),       const_cast<char*>("(I)V"),                                                       reinterpret_cast<void*>(g_hook_callbacks.post_world_clear_1_8)},
        {const_cast<char*>("clickMouse"),               const_cast<char*>("(Ljava/lang/Object;)V"),                                      reinterpret_cast<void*>(g_hook_callbacks.click_mouse)},
        {const_cast<char*>("rightClickMouse"),          const_cast<char*>("(Ljava/lang/Object;)V"),                                      reinterpret_cast<void*>(g_hook_callbacks.right_click_mouse)},
        {const_cast<char*>("orientCamera"),             const_cast<char*>("(Ljava/lang/Object;F)V"),                                     reinterpret_cast<void*>(g_hook_callbacks.orient_camera)},
        {const_cast<char*>("updateActiveRenderInfo"),   const_cast<char*>("(Ljava/lang/Object;Z)V"),                                     reinterpret_cast<void*>(g_hook_callbacks.update_active_render_info)},
        {const_cast<char*>("setRenderX"),               const_cast<char*>("(Ljava/lang/Object;F)V"),                                     reinterpret_cast<void*>(g_hook_callbacks.set_render_x)},
        {const_cast<char*>("setRenderY"),               const_cast<char*>("(Ljava/lang/Object;F)V"),                                     reinterpret_cast<void*>(g_hook_callbacks.set_render_y)},
        {const_cast<char*>("updateEntities"),           const_cast<char*>("(Ljava/lang/Object;)V"),                                      reinterpret_cast<void*>(g_hook_callbacks.update_entities)},
        {const_cast<char*>("canRenderName"),            const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;)Z"),                    reinterpret_cast<void*>(g_hook_callbacks.can_render_name)},
        {const_cast<char*>("renderPlayer"),             const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;DDDFF)V"),               reinterpret_cast<void*>(g_hook_callbacks.render_player)},
        {const_cast<char*>("unloadChunk"),              const_cast<char*>("(Ljava/lang/Object;II)V"),                                    reinterpret_cast<void*>(g_hook_callbacks.unload_chunk)},
        {const_cast<char*>("setBlock_1_7"),             const_cast<char*>("(Ljava/lang/Object;IIILjava/lang/Object;II)Z"),               reinterpret_cast<void*>(g_hook_callbacks.set_block_1_7)},
        {const_cast<char*>("setBlock_1_8"),             const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;I)Z"), reinterpret_cast<void*>(g_hook_callbacks.set_block_1_8)},
        {const_cast<char*>("invalidateChunk"),          const_cast<char*>("(Ljava/lang/Object;IIIIII)V"),                                reinterpret_cast<void*>(g_hook_callbacks.invalidate_chunk)},
        {const_cast<char*>("shouldClipLedge"),          const_cast<char*>("(Ljava/lang/Object;)Z"),                                      reinterpret_cast<void*>(g_hook_callbacks.should_clip_ledge)},
        {const_cast<char*>("moveFlying"),               const_cast<char*>("(Ljava/lang/Object;FFF)V"),                                   reinterpret_cast<void*>(g_hook_callbacks.move_flying)},
        {const_cast<char*>("sendPacket_1_7"),           const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V"),  reinterpret_cast<void*>(g_hook_callbacks.send_packet_1_7)},
        {const_cast<char*>("sendPacket_1_8"),           const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;)V"),                    reinterpret_cast<void*>(g_hook_callbacks.send_packet_1_8)},
        {const_cast<char*>("updateController"),         const_cast<char*>("(Ljava/lang/Object;)V"),                                      reinterpret_cast<void*>(g_hook_callbacks.update_controller)},
    };

    jclass local_hooks_cls = nullptr;
    const int def_err = define_and_register_hooks_class(
        env,
        kHooksClassName,
        target.class_loader,
        target.load_class_mid,
        assets::kClassSlinkyHooks13,
        static_cast<jsize>(assets::kClassSlinkyHooks13_size),
        native_methods,
        static_cast<jint>(kSlinkyHookMethodCount),
        &local_hooks_cls);
    if (def_err != kInstallOk) {
        return def_err;
    }

    g_slinky_hooks_class = static_cast<jclass>(env->NewGlobalRef(local_hooks_cls));
    env->DeleteLocalRef(local_hooks_cls);

    jvmtiCapabilities caps{};
    caps.can_retransform_classes = 1;
    jvmtiError jvmti_err = jvmti->AddCapabilities(&caps);
    if (jvmti_err != JVMTI_ERROR_NONE) {
        return static_cast<int>(jvmti_err) + kErrJvmtiBase;
    }

    jvmtiEventCallbacks event_callbacks{};
    event_callbacks.ClassFileLoadHook = &jvmti_class_file_load_hook;
    jvmti_err = jvmti->SetEventCallbacks(&event_callbacks, static_cast<jint>(sizeof(event_callbacks)));
    if (jvmti_err != JVMTI_ERROR_NONE) {
        return static_cast<int>(jvmti_err) + kErrJvmtiBase;
    }

    jvmti_err = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    if (jvmti_err != JVMTI_ERROR_NONE) {
        return static_cast<int>(jvmti_err) + kErrJvmtiBase;
    }

    build_active_patch_specs(target);
    g_hook_rewrite_error.store(0, std::memory_order_release);

    const jclass classes_to_retransform[kHookedClassCount] = {
        target.cls_minecraft,
        target.cls_entity_renderer,
        target.cls_render_manager,
        target.cls_renderer_living_entity,
        target.cls_render_player,
        target.cls_net_handler_play,
        target.cls_world_client,
        target.cls_entity,
        target.cls_entity_living_base,
    };

    jvmti_err = jvmti->RetransformClasses(
        static_cast<jint>(kHookedClassCount), classes_to_retransform);
    if (jvmti_err != JVMTI_ERROR_NONE) {
        jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
        return static_cast<int>(jvmti_err) + kErrJvmtiBase;
    }

    g_hooks_installed.store(true, std::memory_order_release);

    jvmti_err = jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    if (jvmti_err != JVMTI_ERROR_NONE) {
        return static_cast<int>(jvmti_err) + kErrJvmtiBase;
    }

    return g_hook_rewrite_error.load(std::memory_order_acquire);
}

// sub_1800426B0
int install_jvmti_hooks(const events::JniContext* ctx, jvmtiEnv* jvmti) {
    if (!ctx || !ctx->env || !ctx->mappings || !jvmti) {
        return kErrGetJvmtiEnvFailed;
    }
    const HookTargetContext target = extract_hook_targets_from_mappings(ctx->mappings);
    return install_jvmti_hooks(ctx->env, jvmti, target);
}

// sub_1800426B0 convenience overload acquiring jvmtiEnv from JavaVM
int install_jvmti_hooks(JavaVM* vm, JNIEnv* env, const HookTargetContext& target) {
    if (!vm || !env) {
        return kErrGetJvmtiEnvFailed;
    }
    if (g_hooks_installed.load(std::memory_order_acquire)) {
        return kInstallOk;
    }
    jvmtiEnv* jvmti = g_owned_jvmti_env;
    bool created_env = false;
    if (!jvmti) {
        if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK || !jvmti) {
            if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_0) != JNI_OK || !jvmti) {
                return kErrGetJvmtiEnvFailed;
            }
        }
        created_env = true;
    }
    const int err = install_jvmti_hooks(env, jvmti, target);
    if (err == kInstallOk) {
        g_owned_jvmti_env = jvmti;
    } else if (created_env) {
        jvmti->DisposeEnvironment();
    }
    return err;
}

// sub_1800426B0 convenience overload acquiring jvmtiEnv from JavaVM
int install_jvmti_hooks(JavaVM* vm, const events::JniContext* ctx) {
    if (!vm || !ctx || !ctx->env || !ctx->mappings) {
        return kErrGetJvmtiEnvFailed;
    }
    const HookTargetContext target = extract_hook_targets_from_mappings(ctx->mappings);
    return install_jvmti_hooks(vm, ctx->env, target);
}

// sub_180043C80
int uninstall_jvmti_hooks(JNIEnv* env, jvmtiEnv* jvmti) {
    int status = kInstallOk;
    bool expected = true;
    if (g_hooks_installed.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        if (!jvmti) {
            status = kErrGetJvmtiEnvFailed;
        } else {
            jvmtiError jvmti_err = jvmti->SetEventNotificationMode(
                JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
            if (jvmti_err != JVMTI_ERROR_NONE) {
                status = static_cast<int>(jvmti_err) + kErrJvmtiBase;
            } else {
                jclass classes_to_restore[kHookedClassCount]{};
                jint valid_count = 0;
                for (size_t i = 0; i < kHookedClassCount; ++i) {
                    if (g_class_patch_specs[i].target_class != nullptr) {
                        classes_to_restore[valid_count++] = g_class_patch_specs[i].target_class;
                    }
                }
                if (valid_count > 0) {
                    jvmti_err = jvmti->RetransformClasses(valid_count, classes_to_restore);
                }
                if (jvmti_err != JVMTI_ERROR_NONE) {
                    status = static_cast<int>(jvmti_err) + kErrJvmtiBase;
                } else {
                    jvmtiCapabilities caps{};
                    caps.can_retransform_classes = 1;
                    jvmti_err = jvmti->RelinquishCapabilities(&caps);
                    if (jvmti_err != JVMTI_ERROR_NONE) {
                        status = static_cast<int>(jvmti_err) + kErrJvmtiBase;
                    }
                }
            }
        }
    }

    if (g_slinky_hooks_class != nullptr && env != nullptr) {
        env->DeleteGlobalRef(g_slinky_hooks_class);
        g_slinky_hooks_class = nullptr;
    }

    return status;
}

// sub_180043C80 convenience overload acquiring JNIEnv & jvmtiEnv from JavaVM
int uninstall_jvmti_hooks(JavaVM* vm, JNIEnv* env) {
    if (!vm) {
        return kErrGetJvmtiEnvFailed;
    }

    bool attached_here = false;
    if (!env) {
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
            if (vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK || !env) {
                return kErrGetJvmtiEnvFailed;
            }
            attached_here = true;
        }
    }

    jvmtiEnv* jvmti = g_owned_jvmti_env;
    bool temp_jvmti = false;
    if (!jvmti) {
        if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK || !jvmti) {
            if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_0) != JNI_OK || !jvmti) {
                if (attached_here) {
                    vm->DetachCurrentThread();
                }
                return kErrGetJvmtiEnvFailed;
            }
        }
        jvmtiCapabilities caps{};
        caps.can_retransform_classes = 1;
        jvmti->AddCapabilities(&caps);
        temp_jvmti = true;
    }

    const int status = uninstall_jvmti_hooks(env, jvmti);
    if (g_owned_jvmti_env != nullptr) {
        g_owned_jvmti_env->DisposeEnvironment();
        g_owned_jvmti_env = nullptr;
    } else if (temp_jvmti) {
        jvmti->DisposeEnvironment();
    }
    if (attached_here) {
        vm->DetachCurrentThread();
    }
    return status;
}

bool are_jvmti_hooks_installed() noexcept {
    return g_hooks_installed.load(std::memory_order_acquire);
}

jclass get_slinky_hooks_class() noexcept {
    return g_slinky_hooks_class;
}

int get_last_hook_rewrite_error() noexcept {
    return g_hook_rewrite_error.load(std::memory_order_acquire);
}

} // namespace slinky::bytecode
