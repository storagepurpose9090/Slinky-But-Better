#pragma once

#include <cstddef>
#include <cstdint>
#include <jni.h>

namespace slinky {

namespace mc {

enum class MinecraftVersion : int32_t {
    k1_7_10 = 0,
    k1_8_9  = 1,
};

enum class MappingFlavor : int32_t {
    kVanilla  = 0, // Notch obfuscated names
    kMcp      = 1, // Deobfuscated MCP / Lunar names
    kForgeSrg = 2, // Forge SRG field_*/func_* names
};

// Cached JNI ClassLoader + loadClass(String) handle (16 bytes)
struct ClassLoaderHandle {
    jobject loader = nullptr;              // +0x00 (GlobalRef)
    jmethodID load_class_method = nullptr; // +0x08
};
static_assert(sizeof(ClassLoaderHandle) == 16, "ClassLoaderHandle must be 16 bytes");

// Complete 419-QWORD (3352-byte) JNI mappings table matching slinky_beta layout
struct Mappings {
    // QWORD 0 (+0x000)
    int32_t version = 0;                   // +0x000 (0 = 1.7.10, 1 = 1.8.9)
    int32_t mapping_flavor = 0;            // +0x004 (0 = Vanilla, 1 = MCP/Lunar, 2 = Forge/SRG)
    // QWORD 1 (+0x008)
    bool lunar_underscore_inner = false;   // +0x008 (Lunar '_' inner class separator)
    uint8_t pad_lunar[7]{};                // +0x009
    // QWORDs 2..3 (+0x010..+0x01F)
    ClassLoaderHandle client_class_loader{}; // +0x010

    jclass       cls_netty_channel                            = nullptr; // [  4] +0x020 io.netty.channel.Channel
    jmethodID    mid_channel_pipeline                         = nullptr; // [  5] +0x028 Channel.pipeline()
    jclass       cls_player_controller_mp                     = nullptr; // [  6] +0x030 PlayerControllerMP
    jfieldID     fid_controller_cur_block_damage_mp           = nullptr; // [  7] +0x038 PlayerControllerMP.curBlockDamageMP (F)
    jfieldID     fid_controller_block_hit_delay               = nullptr; // [  8] +0x040 PlayerControllerMP.blockHitDelay (I)
    jfieldID     fid_controller_is_hitting_block              = nullptr; // [  9] +0x048 PlayerControllerMP.isHittingBlock (Z)
    jmethodID    mid_controller_update_controller             = nullptr; // [ 10] +0x050 PlayerControllerMP.updateController()V
    jclass       cls_c0e_packet_click_window                  = nullptr; // [ 11] +0x058 C0EPacketClickWindow
    jclass       cls_nbt_tag_list                             = nullptr; // [ 12] +0x060 NBTTagList
    jmethodID    mid_nbt_tag_list_tag_count                   = nullptr; // [ 13] +0x068 NBTTagList.tagCount()I
    jmethodID    mid_nbt_tag_list_get_compound_tag_at         = nullptr; // [ 14] +0x070 NBTTagList.getCompoundTagAt(I)NBTTagCompound
    jclass       cls_nbt_tag_compound                         = nullptr; // [ 15] +0x078 NBTTagCompound
    jmethodID    mid_nbt_tag_compound_get_short               = nullptr; // [ 16] +0x080 NBTTagCompound.getShort(String)S
    jclass       cls_entity_arrow                             = nullptr; // [ 17] +0x088 EntityArrow
    void*        reserved_slot_18                             = nullptr; // [ 18] +0x090 Unused/reserved slot 18
    jfieldID     fid_entity_arrow_in_ground                   = nullptr; // [ 19] +0x098 EntityArrow.inGround (Z)
    jclass       cls_potion_effect                            = nullptr; // [ 20] +0x0A0 PotionEffect
    jfieldID     fid_potion_effect_duration                   = nullptr; // [ 21] +0x0A8 PotionEffect.duration (I)
    jclass       cls_network_player_info                      = nullptr; // [ 22] +0x0B0 NetworkPlayerInfo (1.8.9)
    jfieldID     fid_network_player_info_display_name         = nullptr; // [ 23] +0x0B8 NetworkPlayerInfo.displayName (1.8.9)
    jmethodID    mid_network_player_info_get_player_team      = nullptr; // [ 24] +0x0C0 NetworkPlayerInfo.getPlayerTeam() (1.8.9)
    jclass       cls_netty_generic_future_listener            = nullptr; // [ 25] +0x0C8 io.netty.util.concurrent.GenericFutureListener
    jclass       cls_c16_packet_client_status                 = nullptr; // [ 26] +0x0D0 C16PacketClientStatus
    jfieldID     fid_c16_packet_status                        = nullptr; // [ 27] +0x0D8 C16PacketClientStatus.status
    jmethodID    mid_c16_packet_init                          = nullptr; // [ 28] +0x0E0 C16PacketClientStatus.<init>(EnumState)V
    jclass       cls_c16_enum_state                           = nullptr; // [ 29] +0x0E8 C16PacketClientStatus$EnumState
    jfieldID     sfid_c16_open_inventory_achievement          = nullptr; // [ 30] +0x0F0 EnumState.OPEN_INVENTORY_ACHIEVEMENT
    jclass       cls_c0d_packet_close_window                  = nullptr; // [ 31] +0x0F8 C0DPacketCloseWindow
    jmethodID    mid_c0d_packet_init                          = nullptr; // [ 32] +0x100 C0DPacketCloseWindow.<init>(I)V
    jclass       cls_container_player                         = nullptr; // [ 33] +0x108 ContainerPlayer
    jfieldID     fid_container_player_craft_matrix            = nullptr; // [ 34] +0x110 ContainerPlayer.craftMatrix
    jclass       cls_gui_screen                               = nullptr; // [ 35] +0x118 GuiScreen
    jmethodID    mid_gui_screen_key_typed                     = nullptr; // [ 36] +0x120 GuiScreen.keyTyped(CI)V
    jclass       cls_gui_chest                                = nullptr; // [ 37] +0x128 GuiChest
    jfieldID     fid_gui_chest_lower_chest_inventory          = nullptr; // [ 38] +0x130 GuiChest.lowerChestInventory
    jclass       cls_inventory_basic                          = nullptr; // [ 39] +0x138 InventoryBasic
    jfieldID     fid_inventory_basic_title                    = nullptr; // [ 40] +0x140 InventoryBasic.inventoryTitle
    jclass       cls_netty_channel_pipeline                   = nullptr; // [ 41] +0x148 io.netty.channel.ChannelPipeline
    jmethodID    mid_pipeline_add_before                      = nullptr; // [ 42] +0x150 ChannelPipeline.addBefore(String,String,ChannelHandler)
    jmethodID    mid_pipeline_get_by_class                    = nullptr; // [ 43] +0x158 ChannelPipeline.get(Class)
    jmethodID    mid_pipeline_remove_by_class                 = nullptr; // [ 44] +0x160 ChannelPipeline.remove(Class)
    jclass       cls_java_lang_enum                           = nullptr; // [ 45] +0x168 java.lang.Enum
    jmethodID    mid_enum_ordinal                             = nullptr; // [ 46] +0x170 Enum.ordinal()I
    jclass       cls_minecraft                                = nullptr; // [ 47] +0x178 Minecraft
    jmethodID    mid_mc_get_net_handler                       = nullptr; // [ 48] +0x180 Minecraft.getNetHandler()
    void*        reserved_slot_49                             = nullptr; // [ 49] +0x188 Unused/reserved slot 49
    jmethodID    mid_mc_add_scheduled_task                    = nullptr; // [ 50] +0x190 Minecraft.addScheduledTask(Runnable)
    jmethodID    mid_mc_click_mouse                           = nullptr; // [ 51] +0x198 Minecraft.clickMouse()V
    jmethodID    mid_mc_right_click_mouse                     = nullptr; // [ 52] +0x1A0 Minecraft.rightClickMouse()V
    jfieldID     sfid_mc_the_minecraft                        = nullptr; // [ 53] +0x1A8 Minecraft.theMinecraft (static)
    jfieldID     fid_mc_the_player                            = nullptr; // [ 54] +0x1B0 Minecraft.thePlayer
    jfieldID     fid_mc_the_world                             = nullptr; // [ 55] +0x1B8 Minecraft.theWorld
    jfieldID     fid_mc_timer                                 = nullptr; // [ 56] +0x1C0 Minecraft.timer
    jfieldID     fid_mc_game_settings                         = nullptr; // [ 57] +0x1C8 Minecraft.gameSettings
    jfieldID     fid_mc_in_game_has_focus                     = nullptr; // [ 58] +0x1D0 Minecraft.inGameHasFocus (Z)
    jfieldID     fid_mc_object_mouse_over                     = nullptr; // [ 59] +0x1D8 Minecraft.objectMouseOver
    jfieldID     fid_mc_left_click_counter                    = nullptr; // [ 60] +0x1E0 Minecraft.leftClickCounter (I)
    jfieldID     fid_mc_right_click_delay_timer               = nullptr; // [ 61] +0x1E8 Minecraft.rightClickDelayTimer (I)
    jfieldID     fid_mc_current_screen                        = nullptr; // [ 62] +0x1F0 Minecraft.currentScreen
    jfieldID     fid_mc_ingame_gui                            = nullptr; // [ 63] +0x1F8 Minecraft.ingameGUI
    jfieldID     fid_mc_display_width                         = nullptr; // [ 64] +0x200 Minecraft.displayWidth (I)
    jfieldID     fid_mc_display_height                        = nullptr; // [ 65] +0x208 Minecraft.displayHeight (I)
    jfieldID     fid_mc_render_engine                         = nullptr; // [ 66] +0x210 Minecraft.renderEngine (TextureManager)
    jfieldID     fid_mc_render_manager                        = nullptr; // [ 67] +0x218 Minecraft.renderManager (1.8.9)
    jfieldID     fid_mc_render_view_entity                    = nullptr; // [ 68] +0x220 Minecraft.renderViewEntity
    jfieldID     fid_mc_render_global                         = nullptr; // [ 69] +0x228 Minecraft.renderGlobal
    jfieldID     fid_mc_render_item                           = nullptr; // [ 70] +0x230 Minecraft.renderItem (1.8.9)
    jfieldID     fid_mc_entity_renderer                       = nullptr; // [ 71] +0x238 Minecraft.entityRenderer
    jfieldID     fid_mc_player_controller                     = nullptr; // [ 72] +0x240 Minecraft.playerController
    jclass       cls_entity                                   = nullptr; // [ 73] +0x248 Entity
    jmethodID    mid_entity_is_sprinting                      = nullptr; // [ 74] +0x250 Entity.isSprinting()Z
    jmethodID    mid_entity_set_sprinting                     = nullptr; // [ 75] +0x258 Entity.setSprinting(Z)V
    jmethodID    mid_entity_is_sneaking                       = nullptr; // [ 76] +0x260 Entity.isSneaking()Z
    jmethodID    mid_entity_get_display_name                  = nullptr; // [ 77] +0x268 Entity.getDisplayName()IChatComponent
    jmethodID    mid_entity_get_name                          = nullptr; // [ 78] +0x270 Entity.getCommandSenderName/getName()String
    jmethodID    mid_entity_set_angles                        = nullptr; // [ 79] +0x278 Entity.setAngles(FF)V
    jmethodID    mid_entity_move_flying                       = nullptr; // [ 80] +0x280 Entity.moveFlying(FFF)V
    jmethodID    mid_entity_can_be_collided_with              = nullptr; // [ 81] +0x288 Entity.canBeCollidedWith()Z
    jfieldID     fid_entity_pos_x                             = nullptr; // [ 82] +0x290 Entity.posX (D)
    jfieldID     fid_entity_pos_y                             = nullptr; // [ 83] +0x298 Entity.posY (D)
    jfieldID     fid_entity_pos_z                             = nullptr; // [ 84] +0x2A0 Entity.posZ (D)
    jfieldID     fid_entity_prev_pos_x                        = nullptr; // [ 85] +0x2A8 Entity.prevPosX (D)
    jfieldID     fid_entity_prev_pos_y                        = nullptr; // [ 86] +0x2B0 Entity.prevPosY (D)
    jfieldID     fid_entity_prev_pos_z                        = nullptr; // [ 87] +0x2B8 Entity.prevPosZ (D)
    jfieldID     fid_entity_rotation_yaw                      = nullptr; // [ 88] +0x2C0 Entity.rotationYaw (F)
    jfieldID     fid_entity_rotation_pitch                    = nullptr; // [ 89] +0x2C8 Entity.rotationPitch (F)
    jfieldID     fid_entity_prev_rotation_yaw                 = nullptr; // [ 90] +0x2D0 Entity.prevRotationYaw (F)
    jfieldID     fid_entity_prev_rotation_pitch               = nullptr; // [ 91] +0x2D8 Entity.prevRotationPitch (F)
    jfieldID     fid_entity_motion_x                          = nullptr; // [ 92] +0x2E0 Entity.motionX (D)
    jfieldID     fid_entity_motion_y                          = nullptr; // [ 93] +0x2E8 Entity.motionY (D)
    jfieldID     fid_entity_motion_z                          = nullptr; // [ 94] +0x2F0 Entity.motionZ (D)
    jfieldID     fid_entity_bounding_box                      = nullptr; // [ 95] +0x2F8 Entity.boundingBox (AxisAlignedBB)
    jfieldID     fid_entity_ticks_existed                     = nullptr; // [ 96] +0x300 Entity.ticksExisted (I)
    jfieldID     fid_entity_id                                = nullptr; // [ 97] +0x308 Entity.entityId (I)
    jfieldID     fid_entity_unique_id                         = nullptr; // [ 98] +0x310 Entity.entityUniqueID (UUID)
    jfieldID     fid_entity_on_ground                         = nullptr; // [ 99] +0x318 Entity.onGround (Z)
    jfieldID     fid_entity_is_dead                           = nullptr; // [100] +0x320 Entity.isDead (Z)
    jfieldID     fid_entity_fall_distance                     = nullptr; // [101] +0x328 Entity.fallDistance (F)
    jfieldID     fid_entity_riding_entity                     = nullptr; // [102] +0x330 Entity.ridingEntity
    jclass       cls_world                                    = nullptr; // [103] +0x338 World
    jmethodID    mid_world_get_entity_by_id                   = nullptr; // [104] +0x340 World.getEntityByID(I)Entity
    jmethodID    mid_world_get_block_1_7                      = nullptr; // [105] +0x348 World.getBlock(III)Block (1.7.10)
    jmethodID    mid_world_get_block_state_1_8                = nullptr; // [106] +0x350 World.getBlockState(BlockPos)IBlockState (1.8.9)
    jmethodID    mid_world_ray_trace_blocks                   = nullptr; // [107] +0x358 World.rayTraceBlocks(Vec3,Vec3,ZZZ)MovingObjectPosition
    jmethodID    mid_world_update_entities                    = nullptr; // [108] +0x360 World.updateEntities()V
    jmethodID    mid_world_get_chunk_from_chunk_coords        = nullptr; // [109] +0x368 World.getChunkFromChunkCoords(II)Chunk
    jmethodID    mid_world_mark_block_range_for_render_update = nullptr; // [110] +0x370 World.markBlockRangeForRenderUpdate(IIIIII)V
    jmethodID    mid_world_set_block_1_7                      = nullptr; // [111] +0x378 World.setBlock(IIILBlock;II)Z (1.7.10)
    jmethodID    mid_world_set_block_state_1_8                = nullptr; // [112] +0x380 World.setBlockState(BlockPos,IBlockState,I)Z (1.8.9)
    jfieldID     fid_world_player_entities                    = nullptr; // [113] +0x388 World.playerEntities (List)
    jfieldID     fid_world_loaded_entity_list                 = nullptr; // [114] +0x390 World.loadedEntityList (List)
    jfieldID     fid_world_loaded_tile_entity_list            = nullptr; // [115] +0x398 World.loadedTileEntityList (List)
    jfieldID     fid_world_chunk_provider                     = nullptr; // [116] +0x3A0 World.chunkProvider
    jclass       cls_timer                                    = nullptr; // [117] +0x3A8 Timer
    jfieldID     fid_timer_render_partial_ticks               = nullptr; // [118] +0x3B0 Timer.renderPartialTicks (F)
    jfieldID     fid_timer_speed                              = nullptr; // [119] +0x3B8 Timer.timerSpeed (F)
    jfieldID     fid_timer_elapsed_partial_ticks              = nullptr; // [120] +0x3C0 Timer.elapsedPartialTicks (F)
    jfieldID     fid_timer_elapsed_ticks                      = nullptr; // [121] +0x3C8 Timer.elapsedTicks (I)
    jclass       cls_game_settings                            = nullptr; // [122] +0x3D0 GameSettings
    jfieldID     fid_settings_mouse_sensitivity               = nullptr; // [123] +0x3D8 GameSettings.mouseSensitivity (F)
    jfieldID     fid_settings_key_bindings                    = nullptr; // [124] +0x3E0 GameSettings.keyBindings ([KeyBinding)
    jfieldID     fid_settings_third_person_view               = nullptr; // [125] +0x3E8 GameSettings.thirdPersonView (I)
    jclass       cls_moving_object_position                   = nullptr; // [126] +0x3F0 MovingObjectPosition
    jmethodID    mid_mop_init_entity_vec                      = nullptr; // [127] +0x3F8 MovingObjectPosition.<init>(Entity,Vec3)V
    jfieldID     fid_mop_type_of_hit                          = nullptr; // [128] +0x400 MovingObjectPosition.typeOfHit
    jfieldID     fid_mop_hit_vec                              = nullptr; // [129] +0x408 MovingObjectPosition.hitVec (Vec3)
    jfieldID     fid_mop_side_hit_1_7                         = nullptr; // [130] +0x410 MovingObjectPosition.sideHit (I, 1.7.10)
    jfieldID     fid_mop_side_hit_1_8                         = nullptr; // [131] +0x418 MovingObjectPosition.sideHit (EnumFacing, 1.8.9)
    jfieldID     fid_mop_block_x_1_7                          = nullptr; // [132] +0x420 MovingObjectPosition.blockX (I, 1.7.10)
    jfieldID     fid_mop_block_y_1_7                          = nullptr; // [133] +0x428 MovingObjectPosition.blockY (I, 1.7.10)
    jfieldID     fid_mop_block_z_1_7                          = nullptr; // [134] +0x430 MovingObjectPosition.blockZ (I, 1.7.10)
    jfieldID     fid_mop_block_pos_1_8                        = nullptr; // [135] +0x438 MovingObjectPosition.blockPos (BlockPos, 1.8.9)
    jfieldID     fid_mop_entity_hit                           = nullptr; // [136] +0x440 MovingObjectPosition.entityHit
    jclass       cls_iblock_state                             = nullptr; // [137] +0x448 IBlockState (1.8.9)
    jmethodID    mid_iblock_state_get_block                   = nullptr; // [138] +0x450 IBlockState.getBlock()Block (1.8.9)
    jclass       cls_block                                    = nullptr; // [139] +0x458 Block
    jmethodID    smid_block_get_id_from_block                 = nullptr; // [140] +0x460 Block.getIdFromBlock(Block)I
    jmethodID    smid_block_get_block_from_item               = nullptr; // [141] +0x468 Block.getBlockFromItem(Item)Block
    jclass       cls_block_pos                                = nullptr; // [142] +0x470 BlockPos (1.8.9)
    jmethodID    mid_block_pos_init                           = nullptr; // [143] +0x478 BlockPos.<init>(III)V (1.8.9)
    jfieldID     fid_block_pos_x                              = nullptr; // [144] +0x480 Vec3i/BlockPos.x (I, 1.8.9)
    jfieldID     fid_block_pos_y                              = nullptr; // [145] +0x488 Vec3i/BlockPos.y (I, 1.8.9)
    jfieldID     fid_block_pos_z                              = nullptr; // [146] +0x490 Vec3i/BlockPos.z (I, 1.8.9)
    jclass       cls_key_binding                              = nullptr; // [147] +0x498 KeyBinding
    jfieldID     fid_key_binding_pressed                      = nullptr; // [148] +0x4A0 KeyBinding.pressed (Z)
    jfieldID     fid_key_binding_press_time                   = nullptr; // [149] +0x4A8 KeyBinding.pressTime (I)
    jfieldID     fid_key_binding_key_code                     = nullptr; // [150] +0x4B0 KeyBinding.keyCode (I)
    jclass       cls_java_util_list                           = nullptr; // [151] +0x4B8 java.util.List
    jmethodID    mid_list_size                                = nullptr; // [152] +0x4C0 List.size()I
    jmethodID    mid_list_get                                 = nullptr; // [153] +0x4C8 List.get(I)Object
    jmethodID    mid_list_add                                 = nullptr; // [154] +0x4D0 List.add(Object)Z
    jmethodID    mid_list_remove                              = nullptr; // [155] +0x4D8 List.remove(Object)Z
    jclass       cls_java_util_map                            = nullptr; // [156] +0x4E0 java.util.Map
    jmethodID    mid_map_contains_key                         = nullptr; // [157] +0x4E8 Map.containsKey(Object)Z
    jmethodID    mid_map_get                                  = nullptr; // [158] +0x4F0 Map.get(Object)Object
    jclass       cls_lwjgl_keyboard                           = nullptr; // [159] +0x4F8 org.lwjgl.input.Keyboard
    jmethodID    smid_keyboard_is_key_down                    = nullptr; // [160] +0x500 Keyboard.isKeyDown(I)Z
    jmethodID    smid_keyboard_next                           = nullptr; // [161] +0x508 Keyboard.next()Z
    jmethodID    smid_keyboard_get_event_key                  = nullptr; // [162] +0x510 Keyboard.getEventKey()I
    jmethodID    smid_keyboard_get_event_key_state            = nullptr; // [163] +0x518 Keyboard.getEventKeyState()Z
    jclass       cls_lwjgl_mouse                              = nullptr; // [164] +0x520 org.lwjgl.input.Mouse
    jmethodID    smid_mouse_is_grabbed                        = nullptr; // [165] +0x528 Mouse.isGrabbed()Z
    jmethodID    smid_mouse_set_grabbed                       = nullptr; // [166] +0x530 Mouse.setGrabbed(Z)V
    jmethodID    smid_mouse_is_button_down                    = nullptr; // [167] +0x538 Mouse.isButtonDown(I)Z
    jmethodID    smid_mouse_next                              = nullptr; // [168] +0x540 Mouse.next()Z
    jmethodID    smid_mouse_set_cursor_position               = nullptr; // [169] +0x548 Mouse.setCursorPosition(II)V
    jclass       cls_lwjgl_display                            = nullptr; // [170] +0x550 org.lwjgl.opengl.Display
    jmethodID    smid_display_update                          = nullptr; // [171] +0x558 Display.update()V
    jmethodID    smid_display_is_active                       = nullptr; // [172] +0x560 Display.isActive()Z
    jfieldID     sfid_display_width                           = nullptr; // [173] +0x568 Display.width (I)
    jfieldID     sfid_display_height                          = nullptr; // [174] +0x570 Display.height (I)
    jfieldID     sfid_display_framebuffer_width               = nullptr; // [175] +0x578 Display.framebufferWidth (I, fallback width)
    jfieldID     sfid_display_framebuffer_height              = nullptr; // [176] +0x580 Display.framebufferHeight (I, fallback height)
    jclass       cls_java_lang_object                         = nullptr; // [177] +0x588 java.lang.Object
    jmethodID    mid_object_hash_code                         = nullptr; // [178] +0x590 Object.hashCode()I
    jclass       cls_secure_class_loader                      = nullptr; // [179] +0x598 java.security.SecureClassLoader
    jmethodID    mid_secure_class_loader_init                 = nullptr; // [180] +0x5A0 SecureClassLoader.<init>(ClassLoader)V
    jclass       cls_axis_aligned_bb                          = nullptr; // [181] +0x5A8 AxisAlignedBB
    jmethodID    mid_aabb_init                                = nullptr; // [182] +0x5B0 AxisAlignedBB.<init>(DDDDDD)V
    jmethodID    mid_aabb_calculate_intercept                 = nullptr; // [183] +0x5B8 AxisAlignedBB.calculateIntercept(Vec3,Vec3)
    jfieldID     fid_aabb_min_x                               = nullptr; // [184] +0x5C0 AxisAlignedBB.minX (D)
    jfieldID     fid_aabb_min_y                               = nullptr; // [185] +0x5C8 AxisAlignedBB.minY (D)
    jfieldID     fid_aabb_min_z                               = nullptr; // [186] +0x5D0 AxisAlignedBB.minZ (D)
    jfieldID     fid_aabb_max_x                               = nullptr; // [187] +0x5D8 AxisAlignedBB.maxX (D)
    jfieldID     fid_aabb_max_y                               = nullptr; // [188] +0x5E0 AxisAlignedBB.maxY (D)
    jfieldID     fid_aabb_max_z                               = nullptr; // [189] +0x5E8 AxisAlignedBB.maxZ (D)
    jclass       cls_gui_container                            = nullptr; // [190] +0x5F0 GuiContainer
    jmethodID    mid_gui_container_handle_mouse_click         = nullptr; // [191] +0x5F8 GuiContainer.handleMouseClick(Slot,III)V
    jfieldID     fid_gui_container_the_slot                   = nullptr; // [192] +0x600 GuiContainer.theSlot
    jfieldID     fid_gui_container_last_click_time            = nullptr; // [193] +0x608 GuiContainer.lastClickTime (J)
    jclass       cls_entity_living_base                       = nullptr; // [194] +0x610 EntityLivingBase
    jmethodID    mid_living_ray_trace                         = nullptr; // [195] +0x618 EntityLivingBase.rayTrace(DF)MovingObjectPosition
    jmethodID    mid_living_get_position_eyes                 = nullptr; // [196] +0x620 EntityLivingBase.getPosition/getPositionEyes(F)Vec3
    jmethodID    mid_living_get_health                        = nullptr; // [197] +0x628 EntityLivingBase.getHealth()F
    jmethodID    mid_living_get_team                          = nullptr; // [198] +0x630 EntityLivingBase.getTeam()Team
    jmethodID    mid_living_swing_item                        = nullptr; // [199] +0x638 EntityLivingBase.swingItem()V
    jmethodID    mid_living_get_max_health                    = nullptr; // [200] +0x640 EntityLivingBase.getMaxHealth()F
    jmethodID    mid_living_get_active_potion_effect          = nullptr; // [201] +0x648 EntityLivingBase.getActivePotionEffect(Potion)
    jmethodID    mid_living_jump                              = nullptr; // [202] +0x650 EntityLivingBase.jump()V
    jfieldID     fid_living_move_forward                      = nullptr; // [203] +0x658 EntityLivingBase.moveForward (F)
    jfieldID     fid_living_move_strafing                     = nullptr; // [204] +0x660 EntityLivingBase.moveStrafing (F)
    jfieldID     fid_living_max_hurt_time                     = nullptr; // [205] +0x668 EntityLivingBase.maxHurtTime (I)
    jfieldID     fid_living_hurt_time                         = nullptr; // [206] +0x670 EntityLivingBase.hurtTime (I)
    jfieldID     fid_living_last_attacker_time                = nullptr; // [207] +0x678 EntityLivingBase.lastAttackerTime (I)
    jfieldID     fid_living_land_movement_factor              = nullptr; // [208] +0x680 EntityLivingBase.landMovementFactor (F)
    jfieldID     fid_living_jump_ticks                        = nullptr; // [209] +0x688 EntityLivingBase.jumpTicks (I)
    jclass       cls_entity_player                            = nullptr; // [210] +0x690 EntityPlayer
    jmethodID    mid_player_is_invisible_to_player            = nullptr; // [211] +0x698 EntityPlayer.isInvisibleToPlayer(EntityPlayer)Z
    jmethodID    mid_player_is_using_item                     = nullptr; // [212] +0x6A0 EntityPlayer.isUsingItem()Z
    jmethodID    mid_player_get_absorption_amount             = nullptr; // [213] +0x6A8 EntityPlayer.getAbsorptionAmount()F
    jmethodID    mid_player_get_equipment_in_slot             = nullptr; // [214] +0x6B0 EntityPlayer.getEquipmentInSlot(I)ItemStack
    jfieldID     fid_player_inventory                         = nullptr; // [215] +0x6B8 EntityPlayer.inventory (InventoryPlayer)
    jfieldID     fid_player_item_in_use_count                 = nullptr; // [216] +0x6C0 EntityPlayer.itemInUseCount (I)
    jfieldID     fid_player_inventory_container               = nullptr; // [217] +0x6C8 EntityPlayer.inventoryContainer (Container)
    jfieldID     fid_player_sleeping                          = nullptr; // [218] +0x6D0 EntityPlayer.sleeping (Z)
    jfieldID     fid_player_speed_on_ground                   = nullptr; // [219] +0x6D8 EntityPlayer.speedOnGround (F)
    jclass       cls_inventory_player                         = nullptr; // [220] +0x6E0 InventoryPlayer
    jfieldID     fid_inv_player_main_inventory                = nullptr; // [221] +0x6E8 InventoryPlayer.mainInventory ([ItemStack)
    jfieldID     fid_inv_player_current_item                  = nullptr; // [222] +0x6F0 InventoryPlayer.currentItem (I)
    jfieldID     fid_inv_player_dragged_item_stack            = nullptr; // [223] +0x6F8 InventoryPlayer.itemStack (ItemStack)
    jclass       cls_item_stack                               = nullptr; // [224] +0x700 ItemStack
    jmethodID    mid_item_stack_init_block                    = nullptr; // [225] +0x708 ItemStack.<init>(Block)V
    jmethodID    mid_item_stack_get_item                      = nullptr; // [226] +0x710 ItemStack.getItem()Item
    jmethodID    mid_item_stack_get_str_vs_block              = nullptr; // [227] +0x718 ItemStack.getStrVsBlock(Block)F
    jmethodID    mid_item_stack_get_enchantment_tag_list      = nullptr; // [228] +0x720 ItemStack.getEnchantmentTagList()NBTTagList
    jmethodID    mid_item_stack_get_max_stack_size            = nullptr; // [229] +0x728 ItemStack.getMaxStackSize()I
    jmethodID    smid_item_stack_are_tags_equal               = nullptr; // [230] +0x730 ItemStack.areItemStackTagsEqual(ItemStack,ItemStack)Z
    jmethodID    mid_item_stack_has_display_name              = nullptr; // [231] +0x738 ItemStack.hasDisplayName()Z
    jfieldID     fid_item_stack_size                          = nullptr; // [232] +0x740 ItemStack.stackSize (I)
    jfieldID     fid_item_stack_item_damage                   = nullptr; // [233] +0x748 ItemStack.itemDamage/metadata (I)
    jclass       cls_slot                                     = nullptr; // [234] +0x750 Slot
    jfieldID     fid_slot_number                              = nullptr; // [235] +0x758 Slot.slotNumber (I)
    jmethodID    mid_slot_get_stack                           = nullptr; // [236] +0x760 Slot.getStack()ItemStack
    jclass       cls_iinventory                               = nullptr; // [237] +0x768 IInventory
    jmethodID    mid_iinventory_get_stack_in_slot             = nullptr; // [238] +0x770 IInventory.getStackInSlot(I)ItemStack
    jmethodID    mid_iinventory_get_size_inventory            = nullptr; // [239] +0x778 IInventory.getSizeInventory()I
    jclass       cls_network_manager                          = nullptr; // [240] +0x780 NetworkManager
    jfieldID     fid_network_manager_channel                  = nullptr; // [241] +0x788 NetworkManager.channel (Channel)
    jmethodID    mid_network_manager_send_packet_listeners    = nullptr; // [242] +0x790 NetworkManager.scheduleOutboundPacket(Packet,[GenericFutureListener)V (1.7.10)
    jmethodID    mid_network_manager_send_packet              = nullptr; // [243] +0x798 NetworkManager.sendPacket(Packet)V (1.8.9)
    jclass       cls_c03_packet_player                        = nullptr; // [244] +0x7A0 C03PacketPlayer
    jfieldID     fid_c03_moving                               = nullptr; // [245] +0x7A8 C03PacketPlayer.moving (Z)
    jfieldID     fid_c03_on_ground                            = nullptr; // [246] +0x7B0 C03PacketPlayer.onGround (Z)
    jfieldID     fid_c03_x                                    = nullptr; // [247] +0x7B8 C03PacketPlayer.x (D)
    jfieldID     fid_c03_y                                    = nullptr; // [248] +0x7C0 C03PacketPlayer.y (D)
    jfieldID     fid_c03_z                                    = nullptr; // [249] +0x7C8 C03PacketPlayer.z (D)
    jclass       cls_c07_packet_player_digging                = nullptr; // [250] +0x7D0 C07PacketPlayerDigging
    jfieldID     fid_c07_status                               = nullptr; // [251] +0x7D8 C07PacketPlayerDigging.status
    jclass       cls_c08_packet_player_block_placement        = nullptr; // [252] +0x7E0 C08PacketPlayerBlockPlacement
    jfieldID     fid_c08_stack                                = nullptr; // [253] +0x7E8 C08PacketPlayerBlockPlacement.stack (ItemStack)
    jclass       cls_tile_entity                              = nullptr; // [254] +0x7F0 TileEntity
    jfieldID     fid_tile_entity_block_type                   = nullptr; // [255] +0x7F8 TileEntity.blockType (Block)
    jfieldID     fid_tile_entity_pos_1_8                      = nullptr; // [256] +0x800 TileEntity.pos (BlockPos, 1.8.9)
    jfieldID     fid_tile_entity_x_1_7                        = nullptr; // [257] +0x808 TileEntity.xCoord (I, 1.7.10)
    jfieldID     fid_tile_entity_y_1_7                        = nullptr; // [258] +0x810 TileEntity.yCoord (I, 1.7.10)
    jfieldID     fid_tile_entity_z_1_7                        = nullptr; // [259] +0x818 TileEntity.zCoord (I, 1.7.10)
    jclass       cls_s12_packet_entity_velocity               = nullptr; // [260] +0x820 S12PacketEntityVelocity
    jfieldID     fid_s12_entity_id                            = nullptr; // [261] +0x828 S12PacketEntityVelocity.entityID (I)
    jfieldID     fid_s12_motion_x                             = nullptr; // [262] +0x830 S12PacketEntityVelocity.motionX (I)
    jfieldID     fid_s12_motion_y                             = nullptr; // [263] +0x838 S12PacketEntityVelocity.motionY (I)
    jfieldID     fid_s12_motion_z                             = nullptr; // [264] +0x840 S12PacketEntityVelocity.motionZ (I)
    jclass       cls_render_manager                           = nullptr; // [265] +0x848 RenderManager
    jfieldID     sfid_render_manager_instance_1_7             = nullptr; // [266] +0x850 RenderManager.instance (1.7.10)
    jfieldID     fid_render_manager_player_view_x             = nullptr; // [267] +0x858 RenderManager.playerViewX (F)
    jfieldID     fid_render_manager_player_view_y             = nullptr; // [268] +0x860 RenderManager.playerViewY (F)
    jfieldID     fid_render_manager_font_renderer             = nullptr; // [269] +0x868 RenderManager.textRenderer (FontRenderer)
    jclass       cls_s14_packet_entity                        = nullptr; // [270] +0x870 S14PacketEntity
    jfieldID     fid_s14_entity_id                            = nullptr; // [271] +0x878 S14PacketEntity.entityId (I)
    jfieldID     fid_s14_pos_x                                = nullptr; // [272] +0x880 S14PacketEntity.posX (B)
    jfieldID     fid_s14_pos_y                                = nullptr; // [273] +0x888 S14PacketEntity.posY (B)
    jfieldID     fid_s14_pos_z                                = nullptr; // [274] +0x890 S14PacketEntity.posZ (B)
    jfieldID     fid_s14_yaw                                  = nullptr; // [275] +0x898 S14PacketEntity.yaw (B)
    jfieldID     fid_s14_pitch                                = nullptr; // [276] +0x8A0 S14PacketEntity.pitch (B)
    jfieldID     fid_s14_has_rot                              = nullptr; // [277] +0x8A8 S14PacketEntity.onGround/hasRot (Z)
    jclass       cls_s18_packet_entity_teleport               = nullptr; // [278] +0x8B0 S18PacketEntityTeleport
    jfieldID     fid_s18_entity_id                            = nullptr; // [279] +0x8B8 S18PacketEntityTeleport.entityId (I)
    jfieldID     fid_s18_pos_x                                = nullptr; // [280] +0x8C0 S18PacketEntityTeleport.posX (I)
    jfieldID     fid_s18_pos_y                                = nullptr; // [281] +0x8C8 S18PacketEntityTeleport.posY (I)
    jfieldID     fid_s18_pos_z                                = nullptr; // [282] +0x8D0 S18PacketEntityTeleport.posZ (I)
    jfieldID     fid_s18_yaw                                  = nullptr; // [283] +0x8D8 S18PacketEntityTeleport.yaw (B)
    jfieldID     fid_s18_pitch                                = nullptr; // [284] +0x8E0 S18PacketEntityTeleport.pitch (B)
    jclass       cls_s19_packet_entity_status                 = nullptr; // [285] +0x8E8 S19PacketEntityStatus
    jfieldID     fid_s19_entity_id                            = nullptr; // [286] +0x8F0 S19PacketEntityStatus.entityId (I)
    jclass       cls_s29_packet_sound_effect                  = nullptr; // [287] +0x8F8 S29PacketSoundEffect
    jclass       cls_s06_packet_update_health                 = nullptr; // [288] +0x900 S06PacketUpdateHealth
    jclass       cls_entity_other_player_mp                   = nullptr; // [289] +0x908 EntityOtherPlayerMP
    jfieldID     fid_other_player_pos_rot_increments          = nullptr; // [290] +0x910 EntityOtherPlayerMP.otherPlayerMPPosRotationIncrements (I)
    jfieldID     fid_other_player_mp_x                        = nullptr; // [291] +0x918 EntityOtherPlayerMP.otherPlayerMPX (D)
    jfieldID     fid_other_player_mp_y                        = nullptr; // [292] +0x920 EntityOtherPlayerMP.otherPlayerMPY (D)
    jfieldID     fid_other_player_mp_z                        = nullptr; // [293] +0x928 EntityOtherPlayerMP.otherPlayerMPZ (D)
    jfieldID     fid_other_player_mp_yaw                      = nullptr; // [294] +0x930 EntityOtherPlayerMP.otherPlayerMPYaw (D)
    jclass       cls_gui_inventory                            = nullptr; // [295] +0x938 GuiInventory
    jclass       cls_entity_player_sp                         = nullptr; // [296] +0x940 EntityPlayerSP
    jfieldID     fid_player_sp_render_arm_yaw                 = nullptr; // [297] +0x948 EntityPlayerSP.renderArmYaw (F)
    jfieldID     fid_player_sp_render_arm_pitch               = nullptr; // [298] +0x950 EntityPlayerSP.renderArmPitch (F)
    jfieldID     fid_player_sp_prev_render_arm_yaw            = nullptr; // [299] +0x958 EntityPlayerSP.prevRenderArmYaw (F)
    jfieldID     fid_player_sp_prev_render_arm_pitch          = nullptr; // [300] +0x960 EntityPlayerSP.prevRenderArmPitch (F)
    jfieldID     fid_player_sp_time_in_portal                 = nullptr; // [301] +0x968 EntityPlayerSP.timeInPortal (F)
    jfieldID     fid_player_sp_prev_time_in_portal            = nullptr; // [302] +0x970 EntityPlayerSP.prevTimeInPortal (F)
    jmethodID    mid_player_sp_add_chat_message               = nullptr; // [303] +0x978 EntityPlayerSP.addChatMessage(IChatComponent)V
    jclass       cls_entity_renderer                          = nullptr; // [304] +0x980 EntityRenderer
    jmethodID    mid_entity_renderer_get_mouse_over           = nullptr; // [305] +0x988 EntityRenderer.getMouseOver(F)V
    jmethodID    mid_entity_renderer_render_hand              = nullptr; // [306] +0x990 EntityRenderer.renderHand(FI)V
    jmethodID    mid_entity_renderer_orient_camera            = nullptr; // [307] +0x998 EntityRenderer.orientCamera(F)V
    jmethodID    mid_entity_renderer_get_fov_modifier         = nullptr; // [308] +0x9A0 EntityRenderer.getFOVModifier(FZ)F
    jmethodID    mid_entity_renderer_setup_camera_transform   = nullptr; // [309] +0x9A8 EntityRenderer.setupCameraTransform(FI)V
    jclass       cls_framebuffer                              = nullptr; // [310] +0x9B0 Framebuffer
    jmethodID    mid_framebuffer_bind                         = nullptr; // [311] +0x9B8 Framebuffer.bindFramebuffer(Z)V
    jclass       cls_render_global                            = nullptr; // [312] +0x9C0 RenderGlobal
    jfieldID     fid_render_global_entities_dirty             = nullptr; // [313] +0x9C8 RenderGlobal.displayListEntitiesDirty (Z)
    jclass       cls_active_render_info                       = nullptr; // [314] +0x9D0 ActiveRenderInfo
    jmethodID    smid_active_render_info_update               = nullptr; // [315] +0x9D8 ActiveRenderInfo.updateRenderInfo(EntityPlayer,Z)V
    jclass       cls_item                                     = nullptr; // [316] +0x9E0 Item
    jmethodID    smid_item_get_id_from_item                   = nullptr; // [317] +0x9E8 Item.getIdFromItem(Item)I
    jmethodID    smid_item_get_item_by_id                     = nullptr; // [318] +0x9F0 Item.getItemById(I)Item
    jclass       cls_vec3                                     = nullptr; // [319] +0x9F8 Vec3
    jmethodID    mid_vec3_init                                = nullptr; // [320] +0xA00 Vec3.<init>(DDD)V
    jfieldID     fid_vec3_x_coord                             = nullptr; // [321] +0xA08 Vec3.xCoord (D)
    jfieldID     fid_vec3_y_coord                             = nullptr; // [322] +0xA10 Vec3.yCoord (D)
    jfieldID     fid_vec3_z_coord                             = nullptr; // [323] +0xA18 Vec3.zCoord (D)
    jclass       cls_extended_block_storage                   = nullptr; // [324] +0xA20 ExtendedBlockStorage
    jfieldID     fid_ebs_block_data                           = nullptr; // [325] +0xA28 ExtendedBlockStorage.blockLSBArray/data
    jclass       cls_world_client                             = nullptr; // [326] +0xA30 WorldClient
    jclass       cls_chunk_provider_client                    = nullptr; // [327] +0xA38 ChunkProviderClient
    jfieldID     fid_chunk_provider_chunk_listing             = nullptr; // [328] +0xA40 ChunkProviderClient.chunkListing (List)
    jmethodID    mid_chunk_provider_unload_chunk              = nullptr; // [329] +0xA48 ChunkProviderClient.unloadChunk(II)V
    jclass       cls_entity_item                              = nullptr; // [330] +0xA50 EntityItem
    jmethodID    mid_entity_item_get_item_stack               = nullptr; // [331] +0xA58 EntityItem.getEntityItem()ItemStack
    jmethodID    mid_entity_item_search_nearby                = nullptr; // [332] +0xA60 EntityItem.searchForOtherItemsNearby()V
    jclass       cls_s3f_packet_custom_payload                = nullptr; // [333] +0xA68 S3FPacketCustomPayload
    jfieldID     fid_s3f_channel                              = nullptr; // [334] +0xA70 S3FPacketCustomPayload.channel (String)
    jfieldID     fid_s3f_data                                 = nullptr; // [335] +0xA78 S3FPacketCustomPayload.data ([B / PacketBuffer)
    jclass       cls_packet_buffer                            = nullptr; // [336] +0xA80 PacketBuffer
    jmethodID    mid_packet_buffer_readable_bytes             = nullptr; // [337] +0xA88 PacketBuffer.readableBytes()I
    jmethodID    mid_packet_buffer_reader_index               = nullptr; // [338] +0xA90 PacketBuffer.readerIndex()I
    jmethodID    mid_packet_buffer_get_bytes                  = nullptr; // [339] +0xA98 PacketBuffer.getBytes(I,[B)ByteBuf
    jclass       cls_java_util_uuid                           = nullptr; // [340] +0xAA0 java.util.UUID
    jmethodID    mid_uuid_get_least_sig_bits                  = nullptr; // [341] +0xAA8 UUID.getLeastSignificantBits()J
    jmethodID    mid_uuid_get_most_sig_bits                   = nullptr; // [342] +0xAB0 UUID.getMostSignificantBits()J
    jclass       cls_chat_component_text                      = nullptr; // [343] +0xAB8 ChatComponentText
    jmethodID    mid_chat_component_text_init                 = nullptr; // [344] +0xAC0 ChatComponentText.<init>(String)V
    jclass       cls_stat_collector                           = nullptr; // [345] +0xAC8 StatCollector
    jmethodID    smid_stat_collector_translate_to_local       = nullptr; // [346] +0xAD0 StatCollector.translateToLocal(String)String
    jclass       cls_c01_packet_chat_message                  = nullptr; // [347] +0xAD8 C01PacketChatMessage
    jfieldID     fid_c01_message                              = nullptr; // [348] +0xAE0 C01PacketChatMessage.message (String)
    jclass       cls_c14_packet_tab_complete                  = nullptr; // [349] +0xAE8 C14PacketTabComplete
    jfieldID     fid_c14_message                              = nullptr; // [350] +0xAF0 C14PacketTabComplete.message (String)
    jclass       cls_score_player_team                        = nullptr; // [351] +0xAF8 ScorePlayerTeam
    jfieldID     fid_score_player_team_name_prefix            = nullptr; // [352] +0xB00 ScorePlayerTeam.namePrefixSPT (String)
    jclass       cls_net_handler_play_client                  = nullptr; // [353] +0xB08 NetHandlerPlayClient
    jfieldID     fid_net_handler_player_info_map              = nullptr; // [354] +0xB10 NetHandlerPlayClient.playerInfoMap (Map)
    jfieldID     fid_net_handler_net_manager                  = nullptr; // [355] +0xB18 NetHandlerPlayClient.netManager (NetworkManager)
    jclass       cls_ichat_component                          = nullptr; // [356] +0xB20 IChatComponent
    jmethodID    mid_ichat_component_get_formatted_text       = nullptr; // [357] +0xB28 IChatComponent.getFormattedText()String
    jclass       cls_ichat_component_serializer               = nullptr; // [358] +0xB30 IChatComponent$Serializer
    jmethodID    smid_ichat_serializer_json_to_component      = nullptr; // [359] +0xB38 Serializer.jsonToComponent(String)IChatComponent
    jclass       cls_renderer_living_entity                   = nullptr; // [360] +0xB40 RendererLivingEntity
    const char*  hook_rle_render_name_method_name             = nullptr; // [361] +0xB48 RendererLivingEntity.passSpecialRender/renderName name
    const char*  hook_rle_render_name_method_desc             = nullptr; // [362] +0xB50 RendererLivingEntity.passSpecialRender/renderName desc
    jmethodID    mid_rle_render_name                          = nullptr; // [363] +0xB58 RendererLivingEntity.passSpecialRender/renderName
    const char*  hook_rle_can_render_name_method_name         = nullptr; // [364] +0xB60 RendererLivingEntity.canRenderName name
    const char*  hook_rle_can_render_name_method_desc         = nullptr; // [365] +0xB68 RendererLivingEntity.canRenderName desc
    jmethodID    mid_rle_can_render_name                      = nullptr; // [366] +0xB70 RendererLivingEntity.canRenderName
    const char*  hook_rle_do_render_method_name               = nullptr; // [367] +0xB78 RendererLivingEntity.doRender name
    const char*  hook_rle_do_render_method_desc               = nullptr; // [368] +0xB80 RendererLivingEntity.doRender desc
    jmethodID    mid_rle_do_render                            = nullptr; // [369] +0xB88 RendererLivingEntity.doRender
    jclass       cls_font_renderer                            = nullptr; // [370] +0xB90 FontRenderer
    jmethodID    mid_font_renderer_get_string_width           = nullptr; // [371] +0xB98 FontRenderer.getStringWidth(String)I
    jmethodID    mid_font_renderer_draw_string                = nullptr; // [372] +0xBA0 FontRenderer.drawString
    jclass       cls_gl_state_manager                         = nullptr; // [373] +0xBA8 GlStateManager (1.8.9)
    jmethodID    smid_gl_enable_depth                         = nullptr; // [374] +0xBB0 GlStateManager.enableDepth()V
    jmethodID    smid_gl_disable_depth                        = nullptr; // [375] +0xBB8 GlStateManager.disableDepth()V
    jmethodID    smid_gl_enable_lighting                      = nullptr; // [376] +0xBC0 GlStateManager.enableLighting()V
    jmethodID    smid_gl_disable_lighting                     = nullptr; // [377] +0xBC8 GlStateManager.disableLighting()V
    jmethodID    smid_gl_enable_texture_2d                    = nullptr; // [378] +0xBD0 GlStateManager.enableTexture2D()V
    jmethodID    smid_gl_disable_texture_2d                   = nullptr; // [379] +0xBD8 GlStateManager.disableTexture2D()V
    jmethodID    smid_gl_enable_blend                         = nullptr; // [380] +0xBE0 GlStateManager.enableBlend()V
    jmethodID    smid_gl_disable_blend                        = nullptr; // [381] +0xBE8 GlStateManager.disableBlend()V
    jmethodID    smid_gl_enable_cull                          = nullptr; // [382] +0xBF0 GlStateManager.enableCull()V
    jmethodID    smid_gl_disable_cull                         = nullptr; // [383] +0xBF8 GlStateManager.disableCull()V
    jmethodID    smid_gl_enable_alpha                         = nullptr; // [384] +0xC00 GlStateManager.enableAlpha()V
    jmethodID    smid_gl_disable_alpha                        = nullptr; // [385] +0xC08 GlStateManager.disableAlpha()V
    jmethodID    smid_gl_blend_func                           = nullptr; // [386] +0xC10 GlStateManager.blendFunc(II)V
    jmethodID    smid_gl_color                                = nullptr; // [387] +0xC18 GlStateManager.color(FFFF)V
    jclass       cls_c02_packet_use_entity                    = nullptr; // [388] +0xC20 C02PacketUseEntity
    jclass       cls_c0b_packet_entity_action                 = nullptr; // [389] +0xC28 C0BPacketEntityAction
    jfieldID     fid_c0b_action                               = nullptr; // [390] +0xC30 C0BPacketEntityAction.action
    jclass       cls_s08_packet_player_pos_look               = nullptr; // [391] +0xC38 S08PacketPlayerPosLook
    jclass       cls_s27_packet_explosion                     = nullptr; // [392] +0xC40 S27PacketExplosion
    jfieldID     fid_s27_motion_x                             = nullptr; // [393] +0xC48 S27PacketExplosion.motionX (F)
    jfieldID     fid_s27_motion_y                             = nullptr; // [394] +0xC50 S27PacketExplosion.motionY (F)
    jfieldID     fid_s27_motion_z                             = nullptr; // [395] +0xC58 S27PacketExplosion.motionZ (F)
    jclass       cls_s00_packet_keep_alive                    = nullptr; // [396] +0xC60 S00PacketKeepAlive
    jclass       cls_c00_packet_keep_alive                    = nullptr; // [397] +0xC68 C00PacketKeepAlive
    jclass       cls_potion                                   = nullptr; // [398] +0xC70 Potion
    jfieldID     sfid_potion_types                            = nullptr; // [399] +0xC78 Potion.potionTypes ([Potion)
    jclass       cls_render_player                            = nullptr; // [400] +0xC80 RenderPlayer
    const char*  hook_render_player_do_render_name            = nullptr; // [401] +0xC88 RenderPlayer.doRender name
    const char*  hook_render_player_do_render_desc            = nullptr; // [402] +0xC90 RenderPlayer.doRender desc
    jmethodID    mid_render_player_do_render                  = nullptr; // [403] +0xC98 RenderPlayer.doRender(AbstractClientPlayer,DDDFF)V
    jclass       cls_render_item                              = nullptr; // [404] +0xCA0 RenderItem
    jfieldID     fid_render_item_z_level                      = nullptr; // [405] +0xCA8 RenderItem.zLevel (F)
    jmethodID    mid_render_item_init_1_7                     = nullptr; // [406] +0xCB0 RenderItem.<init>()V (1.7.10)
    jmethodID    mid_render_item_into_gui                     = nullptr; // [407] +0xCB8 RenderItem.renderItemAndEffectIntoGUI
    jmethodID    mid_render_item_overlay_into_gui             = nullptr; // [408] +0xCC0 RenderItem.renderItemOverlayIntoGUI
    jclass       cls_render_helper                            = nullptr; // [409] +0xCC8 RenderHelper
    jmethodID    smid_render_helper_enable_item_lighting      = nullptr; // [410] +0xCD0 RenderHelper.enableStandardItemLighting()V
    jmethodID    smid_render_helper_disable_item_lighting     = nullptr; // [411] +0xCD8 RenderHelper.disableStandardItemLighting()V
    jclass       cls_entity_list                              = nullptr; // [412] +0xCE0 EntityList
    jmethodID    smid_entity_list_get_entity_id               = nullptr; // [413] +0xCE8 EntityList.getEntityID(Entity)I
    jclass       cls_chunk                                    = nullptr; // [414] +0xCF0 Chunk
    jfieldID     fid_chunk_storage_arrays                     = nullptr; // [415] +0xCF8 Chunk.storageArrays ([ExtendedBlockStorage)
    jfieldID     fid_chunk_x_position                         = nullptr; // [416] +0xD00 Chunk.xPosition (I)
    jfieldID     fid_chunk_z_position                         = nullptr; // [417] +0xD08 Chunk.zPosition (I)
    jclass       cls_s2a_packet_particles                     = nullptr; // [418] +0xD10 S2APacketParticles
};

static_assert(sizeof(Mappings) == 419 * 8, "Mappings must match exact 3352-byte (419 QWORD) layout");
static_assert(offsetof(Mappings, version) == 0x000);
static_assert(offsetof(Mappings, mapping_flavor) == 0x004);
static_assert(offsetof(Mappings, lunar_underscore_inner) == 0x008);
static_assert(offsetof(Mappings, client_class_loader) == 0x010);
static_assert(offsetof(Mappings, cls_netty_channel) == 0x020);
static_assert(offsetof(Mappings, mid_channel_pipeline) == 0x028);
static_assert(offsetof(Mappings, cls_player_controller_mp) == 0x030);
static_assert(offsetof(Mappings, fid_controller_cur_block_damage_mp) == 0x038);
static_assert(offsetof(Mappings, fid_controller_block_hit_delay) == 0x040);
static_assert(offsetof(Mappings, fid_controller_is_hitting_block) == 0x048);
static_assert(offsetof(Mappings, mid_controller_update_controller) == 0x050);
static_assert(offsetof(Mappings, cls_c0e_packet_click_window) == 0x058);
static_assert(offsetof(Mappings, cls_nbt_tag_list) == 0x060);
static_assert(offsetof(Mappings, mid_nbt_tag_list_tag_count) == 0x068);
static_assert(offsetof(Mappings, mid_nbt_tag_list_get_compound_tag_at) == 0x070);
static_assert(offsetof(Mappings, cls_nbt_tag_compound) == 0x078);
static_assert(offsetof(Mappings, mid_nbt_tag_compound_get_short) == 0x080);
static_assert(offsetof(Mappings, cls_entity_arrow) == 0x088);
static_assert(offsetof(Mappings, reserved_slot_18) == 0x090);
static_assert(offsetof(Mappings, fid_entity_arrow_in_ground) == 0x098);
static_assert(offsetof(Mappings, cls_potion_effect) == 0x0A0);
static_assert(offsetof(Mappings, fid_potion_effect_duration) == 0x0A8);
static_assert(offsetof(Mappings, cls_network_player_info) == 0x0B0);
static_assert(offsetof(Mappings, fid_network_player_info_display_name) == 0x0B8);
static_assert(offsetof(Mappings, mid_network_player_info_get_player_team) == 0x0C0);
static_assert(offsetof(Mappings, cls_netty_generic_future_listener) == 0x0C8);
static_assert(offsetof(Mappings, cls_c16_packet_client_status) == 0x0D0);
static_assert(offsetof(Mappings, fid_c16_packet_status) == 0x0D8);
static_assert(offsetof(Mappings, mid_c16_packet_init) == 0x0E0);
static_assert(offsetof(Mappings, cls_c16_enum_state) == 0x0E8);
static_assert(offsetof(Mappings, sfid_c16_open_inventory_achievement) == 0x0F0);
static_assert(offsetof(Mappings, cls_c0d_packet_close_window) == 0x0F8);
static_assert(offsetof(Mappings, mid_c0d_packet_init) == 0x100);
static_assert(offsetof(Mappings, cls_container_player) == 0x108);
static_assert(offsetof(Mappings, fid_container_player_craft_matrix) == 0x110);
static_assert(offsetof(Mappings, cls_gui_screen) == 0x118);
static_assert(offsetof(Mappings, mid_gui_screen_key_typed) == 0x120);
static_assert(offsetof(Mappings, cls_gui_chest) == 0x128);
static_assert(offsetof(Mappings, fid_gui_chest_lower_chest_inventory) == 0x130);
static_assert(offsetof(Mappings, cls_inventory_basic) == 0x138);
static_assert(offsetof(Mappings, fid_inventory_basic_title) == 0x140);
static_assert(offsetof(Mappings, cls_netty_channel_pipeline) == 0x148);
static_assert(offsetof(Mappings, mid_pipeline_add_before) == 0x150);
static_assert(offsetof(Mappings, mid_pipeline_get_by_class) == 0x158);
static_assert(offsetof(Mappings, mid_pipeline_remove_by_class) == 0x160);
static_assert(offsetof(Mappings, cls_java_lang_enum) == 0x168);
static_assert(offsetof(Mappings, mid_enum_ordinal) == 0x170);
static_assert(offsetof(Mappings, cls_minecraft) == 0x178);
static_assert(offsetof(Mappings, mid_mc_get_net_handler) == 0x180);
static_assert(offsetof(Mappings, reserved_slot_49) == 0x188);
static_assert(offsetof(Mappings, mid_mc_add_scheduled_task) == 0x190);
static_assert(offsetof(Mappings, mid_mc_click_mouse) == 0x198);
static_assert(offsetof(Mappings, mid_mc_right_click_mouse) == 0x1A0);
static_assert(offsetof(Mappings, sfid_mc_the_minecraft) == 0x1A8);
static_assert(offsetof(Mappings, fid_mc_the_player) == 0x1B0);
static_assert(offsetof(Mappings, fid_mc_the_world) == 0x1B8);
static_assert(offsetof(Mappings, fid_mc_timer) == 0x1C0);
static_assert(offsetof(Mappings, fid_mc_game_settings) == 0x1C8);
static_assert(offsetof(Mappings, fid_mc_in_game_has_focus) == 0x1D0);
static_assert(offsetof(Mappings, fid_mc_object_mouse_over) == 0x1D8);
static_assert(offsetof(Mappings, fid_mc_left_click_counter) == 0x1E0);
static_assert(offsetof(Mappings, fid_mc_right_click_delay_timer) == 0x1E8);
static_assert(offsetof(Mappings, fid_mc_current_screen) == 0x1F0);
static_assert(offsetof(Mappings, fid_mc_ingame_gui) == 0x1F8);
static_assert(offsetof(Mappings, fid_mc_display_width) == 0x200);
static_assert(offsetof(Mappings, fid_mc_display_height) == 0x208);
static_assert(offsetof(Mappings, fid_mc_render_engine) == 0x210);
static_assert(offsetof(Mappings, fid_mc_render_manager) == 0x218);
static_assert(offsetof(Mappings, fid_mc_render_view_entity) == 0x220);
static_assert(offsetof(Mappings, fid_mc_render_global) == 0x228);
static_assert(offsetof(Mappings, fid_mc_render_item) == 0x230);
static_assert(offsetof(Mappings, fid_mc_entity_renderer) == 0x238);
static_assert(offsetof(Mappings, fid_mc_player_controller) == 0x240);
static_assert(offsetof(Mappings, cls_entity) == 0x248);
static_assert(offsetof(Mappings, mid_entity_is_sprinting) == 0x250);
static_assert(offsetof(Mappings, mid_entity_set_sprinting) == 0x258);
static_assert(offsetof(Mappings, mid_entity_is_sneaking) == 0x260);
static_assert(offsetof(Mappings, mid_entity_get_display_name) == 0x268);
static_assert(offsetof(Mappings, mid_entity_get_name) == 0x270);
static_assert(offsetof(Mappings, mid_entity_set_angles) == 0x278);
static_assert(offsetof(Mappings, mid_entity_move_flying) == 0x280);
static_assert(offsetof(Mappings, mid_entity_can_be_collided_with) == 0x288);
static_assert(offsetof(Mappings, fid_entity_pos_x) == 0x290);
static_assert(offsetof(Mappings, fid_entity_pos_y) == 0x298);
static_assert(offsetof(Mappings, fid_entity_pos_z) == 0x2A0);
static_assert(offsetof(Mappings, fid_entity_prev_pos_x) == 0x2A8);
static_assert(offsetof(Mappings, fid_entity_prev_pos_y) == 0x2B0);
static_assert(offsetof(Mappings, fid_entity_prev_pos_z) == 0x2B8);
static_assert(offsetof(Mappings, fid_entity_rotation_yaw) == 0x2C0);
static_assert(offsetof(Mappings, fid_entity_rotation_pitch) == 0x2C8);
static_assert(offsetof(Mappings, fid_entity_prev_rotation_yaw) == 0x2D0);
static_assert(offsetof(Mappings, fid_entity_prev_rotation_pitch) == 0x2D8);
static_assert(offsetof(Mappings, fid_entity_motion_x) == 0x2E0);
static_assert(offsetof(Mappings, fid_entity_motion_y) == 0x2E8);
static_assert(offsetof(Mappings, fid_entity_motion_z) == 0x2F0);
static_assert(offsetof(Mappings, fid_entity_bounding_box) == 0x2F8);
static_assert(offsetof(Mappings, fid_entity_ticks_existed) == 0x300);
static_assert(offsetof(Mappings, fid_entity_id) == 0x308);
static_assert(offsetof(Mappings, fid_entity_unique_id) == 0x310);
static_assert(offsetof(Mappings, fid_entity_on_ground) == 0x318);
static_assert(offsetof(Mappings, fid_entity_is_dead) == 0x320);
static_assert(offsetof(Mappings, fid_entity_fall_distance) == 0x328);
static_assert(offsetof(Mappings, fid_entity_riding_entity) == 0x330);
static_assert(offsetof(Mappings, cls_world) == 0x338);
static_assert(offsetof(Mappings, mid_world_get_entity_by_id) == 0x340);
static_assert(offsetof(Mappings, mid_world_get_block_1_7) == 0x348);
static_assert(offsetof(Mappings, mid_world_get_block_state_1_8) == 0x350);
static_assert(offsetof(Mappings, mid_world_ray_trace_blocks) == 0x358);
static_assert(offsetof(Mappings, mid_world_update_entities) == 0x360);
static_assert(offsetof(Mappings, mid_world_get_chunk_from_chunk_coords) == 0x368);
static_assert(offsetof(Mappings, mid_world_mark_block_range_for_render_update) == 0x370);
static_assert(offsetof(Mappings, mid_world_set_block_1_7) == 0x378);
static_assert(offsetof(Mappings, mid_world_set_block_state_1_8) == 0x380);
static_assert(offsetof(Mappings, fid_world_player_entities) == 0x388);
static_assert(offsetof(Mappings, fid_world_loaded_entity_list) == 0x390);
static_assert(offsetof(Mappings, fid_world_loaded_tile_entity_list) == 0x398);
static_assert(offsetof(Mappings, fid_world_chunk_provider) == 0x3A0);
static_assert(offsetof(Mappings, cls_timer) == 0x3A8);
static_assert(offsetof(Mappings, fid_timer_render_partial_ticks) == 0x3B0);
static_assert(offsetof(Mappings, fid_timer_speed) == 0x3B8);
static_assert(offsetof(Mappings, fid_timer_elapsed_partial_ticks) == 0x3C0);
static_assert(offsetof(Mappings, fid_timer_elapsed_ticks) == 0x3C8);
static_assert(offsetof(Mappings, cls_game_settings) == 0x3D0);
static_assert(offsetof(Mappings, fid_settings_mouse_sensitivity) == 0x3D8);
static_assert(offsetof(Mappings, fid_settings_key_bindings) == 0x3E0);
static_assert(offsetof(Mappings, fid_settings_third_person_view) == 0x3E8);
static_assert(offsetof(Mappings, cls_moving_object_position) == 0x3F0);
static_assert(offsetof(Mappings, mid_mop_init_entity_vec) == 0x3F8);
static_assert(offsetof(Mappings, fid_mop_type_of_hit) == 0x400);
static_assert(offsetof(Mappings, fid_mop_hit_vec) == 0x408);
static_assert(offsetof(Mappings, fid_mop_side_hit_1_7) == 0x410);
static_assert(offsetof(Mappings, fid_mop_side_hit_1_8) == 0x418);
static_assert(offsetof(Mappings, fid_mop_block_x_1_7) == 0x420);
static_assert(offsetof(Mappings, fid_mop_block_y_1_7) == 0x428);
static_assert(offsetof(Mappings, fid_mop_block_z_1_7) == 0x430);
static_assert(offsetof(Mappings, fid_mop_block_pos_1_8) == 0x438);
static_assert(offsetof(Mappings, fid_mop_entity_hit) == 0x440);
static_assert(offsetof(Mappings, cls_iblock_state) == 0x448);
static_assert(offsetof(Mappings, mid_iblock_state_get_block) == 0x450);
static_assert(offsetof(Mappings, cls_block) == 0x458);
static_assert(offsetof(Mappings, smid_block_get_id_from_block) == 0x460);
static_assert(offsetof(Mappings, smid_block_get_block_from_item) == 0x468);
static_assert(offsetof(Mappings, cls_block_pos) == 0x470);
static_assert(offsetof(Mappings, mid_block_pos_init) == 0x478);
static_assert(offsetof(Mappings, fid_block_pos_x) == 0x480);
static_assert(offsetof(Mappings, fid_block_pos_y) == 0x488);
static_assert(offsetof(Mappings, fid_block_pos_z) == 0x490);
static_assert(offsetof(Mappings, cls_key_binding) == 0x498);
static_assert(offsetof(Mappings, fid_key_binding_pressed) == 0x4A0);
static_assert(offsetof(Mappings, fid_key_binding_press_time) == 0x4A8);
static_assert(offsetof(Mappings, fid_key_binding_key_code) == 0x4B0);
static_assert(offsetof(Mappings, cls_java_util_list) == 0x4B8);
static_assert(offsetof(Mappings, mid_list_size) == 0x4C0);
static_assert(offsetof(Mappings, mid_list_get) == 0x4C8);
static_assert(offsetof(Mappings, mid_list_add) == 0x4D0);
static_assert(offsetof(Mappings, mid_list_remove) == 0x4D8);
static_assert(offsetof(Mappings, cls_java_util_map) == 0x4E0);
static_assert(offsetof(Mappings, mid_map_contains_key) == 0x4E8);
static_assert(offsetof(Mappings, mid_map_get) == 0x4F0);
static_assert(offsetof(Mappings, cls_lwjgl_keyboard) == 0x4F8);
static_assert(offsetof(Mappings, smid_keyboard_is_key_down) == 0x500);
static_assert(offsetof(Mappings, smid_keyboard_next) == 0x508);
static_assert(offsetof(Mappings, smid_keyboard_get_event_key) == 0x510);
static_assert(offsetof(Mappings, smid_keyboard_get_event_key_state) == 0x518);
static_assert(offsetof(Mappings, cls_lwjgl_mouse) == 0x520);
static_assert(offsetof(Mappings, smid_mouse_is_grabbed) == 0x528);
static_assert(offsetof(Mappings, smid_mouse_set_grabbed) == 0x530);
static_assert(offsetof(Mappings, smid_mouse_is_button_down) == 0x538);
static_assert(offsetof(Mappings, smid_mouse_next) == 0x540);
static_assert(offsetof(Mappings, smid_mouse_set_cursor_position) == 0x548);
static_assert(offsetof(Mappings, cls_lwjgl_display) == 0x550);
static_assert(offsetof(Mappings, smid_display_update) == 0x558);
static_assert(offsetof(Mappings, smid_display_is_active) == 0x560);
static_assert(offsetof(Mappings, sfid_display_width) == 0x568);
static_assert(offsetof(Mappings, sfid_display_height) == 0x570);
static_assert(offsetof(Mappings, sfid_display_framebuffer_width) == 0x578);
static_assert(offsetof(Mappings, sfid_display_framebuffer_height) == 0x580);
static_assert(offsetof(Mappings, cls_java_lang_object) == 0x588);
static_assert(offsetof(Mappings, mid_object_hash_code) == 0x590);
static_assert(offsetof(Mappings, cls_secure_class_loader) == 0x598);
static_assert(offsetof(Mappings, mid_secure_class_loader_init) == 0x5A0);
static_assert(offsetof(Mappings, cls_axis_aligned_bb) == 0x5A8);
static_assert(offsetof(Mappings, mid_aabb_init) == 0x5B0);
static_assert(offsetof(Mappings, mid_aabb_calculate_intercept) == 0x5B8);
static_assert(offsetof(Mappings, fid_aabb_min_x) == 0x5C0);
static_assert(offsetof(Mappings, fid_aabb_min_y) == 0x5C8);
static_assert(offsetof(Mappings, fid_aabb_min_z) == 0x5D0);
static_assert(offsetof(Mappings, fid_aabb_max_x) == 0x5D8);
static_assert(offsetof(Mappings, fid_aabb_max_y) == 0x5E0);
static_assert(offsetof(Mappings, fid_aabb_max_z) == 0x5E8);
static_assert(offsetof(Mappings, cls_gui_container) == 0x5F0);
static_assert(offsetof(Mappings, mid_gui_container_handle_mouse_click) == 0x5F8);
static_assert(offsetof(Mappings, fid_gui_container_the_slot) == 0x600);
static_assert(offsetof(Mappings, fid_gui_container_last_click_time) == 0x608);
static_assert(offsetof(Mappings, cls_entity_living_base) == 0x610);
static_assert(offsetof(Mappings, mid_living_ray_trace) == 0x618);
static_assert(offsetof(Mappings, mid_living_get_position_eyes) == 0x620);
static_assert(offsetof(Mappings, mid_living_get_health) == 0x628);
static_assert(offsetof(Mappings, mid_living_get_team) == 0x630);
static_assert(offsetof(Mappings, mid_living_swing_item) == 0x638);
static_assert(offsetof(Mappings, mid_living_get_max_health) == 0x640);
static_assert(offsetof(Mappings, mid_living_get_active_potion_effect) == 0x648);
static_assert(offsetof(Mappings, mid_living_jump) == 0x650);
static_assert(offsetof(Mappings, fid_living_move_forward) == 0x658);
static_assert(offsetof(Mappings, fid_living_move_strafing) == 0x660);
static_assert(offsetof(Mappings, fid_living_max_hurt_time) == 0x668);
static_assert(offsetof(Mappings, fid_living_hurt_time) == 0x670);
static_assert(offsetof(Mappings, fid_living_last_attacker_time) == 0x678);
static_assert(offsetof(Mappings, fid_living_land_movement_factor) == 0x680);
static_assert(offsetof(Mappings, fid_living_jump_ticks) == 0x688);
static_assert(offsetof(Mappings, cls_entity_player) == 0x690);
static_assert(offsetof(Mappings, mid_player_is_invisible_to_player) == 0x698);
static_assert(offsetof(Mappings, mid_player_is_using_item) == 0x6A0);
static_assert(offsetof(Mappings, mid_player_get_absorption_amount) == 0x6A8);
static_assert(offsetof(Mappings, mid_player_get_equipment_in_slot) == 0x6B0);
static_assert(offsetof(Mappings, fid_player_inventory) == 0x6B8);
static_assert(offsetof(Mappings, fid_player_item_in_use_count) == 0x6C0);
static_assert(offsetof(Mappings, fid_player_inventory_container) == 0x6C8);
static_assert(offsetof(Mappings, fid_player_sleeping) == 0x6D0);
static_assert(offsetof(Mappings, fid_player_speed_on_ground) == 0x6D8);
static_assert(offsetof(Mappings, cls_inventory_player) == 0x6E0);
static_assert(offsetof(Mappings, fid_inv_player_main_inventory) == 0x6E8);
static_assert(offsetof(Mappings, fid_inv_player_current_item) == 0x6F0);
static_assert(offsetof(Mappings, fid_inv_player_dragged_item_stack) == 0x6F8);
static_assert(offsetof(Mappings, cls_item_stack) == 0x700);
static_assert(offsetof(Mappings, mid_item_stack_init_block) == 0x708);
static_assert(offsetof(Mappings, mid_item_stack_get_item) == 0x710);
static_assert(offsetof(Mappings, mid_item_stack_get_str_vs_block) == 0x718);
static_assert(offsetof(Mappings, mid_item_stack_get_enchantment_tag_list) == 0x720);
static_assert(offsetof(Mappings, mid_item_stack_get_max_stack_size) == 0x728);
static_assert(offsetof(Mappings, smid_item_stack_are_tags_equal) == 0x730);
static_assert(offsetof(Mappings, mid_item_stack_has_display_name) == 0x738);
static_assert(offsetof(Mappings, fid_item_stack_size) == 0x740);
static_assert(offsetof(Mappings, fid_item_stack_item_damage) == 0x748);
static_assert(offsetof(Mappings, cls_slot) == 0x750);
static_assert(offsetof(Mappings, fid_slot_number) == 0x758);
static_assert(offsetof(Mappings, mid_slot_get_stack) == 0x760);
static_assert(offsetof(Mappings, cls_iinventory) == 0x768);
static_assert(offsetof(Mappings, mid_iinventory_get_stack_in_slot) == 0x770);
static_assert(offsetof(Mappings, mid_iinventory_get_size_inventory) == 0x778);
static_assert(offsetof(Mappings, cls_network_manager) == 0x780);
static_assert(offsetof(Mappings, fid_network_manager_channel) == 0x788);
static_assert(offsetof(Mappings, mid_network_manager_send_packet_listeners) == 0x790);
static_assert(offsetof(Mappings, mid_network_manager_send_packet) == 0x798);
static_assert(offsetof(Mappings, cls_c03_packet_player) == 0x7A0);
static_assert(offsetof(Mappings, fid_c03_moving) == 0x7A8);
static_assert(offsetof(Mappings, fid_c03_on_ground) == 0x7B0);
static_assert(offsetof(Mappings, fid_c03_x) == 0x7B8);
static_assert(offsetof(Mappings, fid_c03_y) == 0x7C0);
static_assert(offsetof(Mappings, fid_c03_z) == 0x7C8);
static_assert(offsetof(Mappings, cls_c07_packet_player_digging) == 0x7D0);
static_assert(offsetof(Mappings, fid_c07_status) == 0x7D8);
static_assert(offsetof(Mappings, cls_c08_packet_player_block_placement) == 0x7E0);
static_assert(offsetof(Mappings, fid_c08_stack) == 0x7E8);
static_assert(offsetof(Mappings, cls_tile_entity) == 0x7F0);
static_assert(offsetof(Mappings, fid_tile_entity_block_type) == 0x7F8);
static_assert(offsetof(Mappings, fid_tile_entity_pos_1_8) == 0x800);
static_assert(offsetof(Mappings, fid_tile_entity_x_1_7) == 0x808);
static_assert(offsetof(Mappings, fid_tile_entity_y_1_7) == 0x810);
static_assert(offsetof(Mappings, fid_tile_entity_z_1_7) == 0x818);
static_assert(offsetof(Mappings, cls_s12_packet_entity_velocity) == 0x820);
static_assert(offsetof(Mappings, fid_s12_entity_id) == 0x828);
static_assert(offsetof(Mappings, fid_s12_motion_x) == 0x830);
static_assert(offsetof(Mappings, fid_s12_motion_y) == 0x838);
static_assert(offsetof(Mappings, fid_s12_motion_z) == 0x840);
static_assert(offsetof(Mappings, cls_render_manager) == 0x848);
static_assert(offsetof(Mappings, sfid_render_manager_instance_1_7) == 0x850);
static_assert(offsetof(Mappings, fid_render_manager_player_view_x) == 0x858);
static_assert(offsetof(Mappings, fid_render_manager_player_view_y) == 0x860);
static_assert(offsetof(Mappings, fid_render_manager_font_renderer) == 0x868);
static_assert(offsetof(Mappings, cls_s14_packet_entity) == 0x870);
static_assert(offsetof(Mappings, fid_s14_entity_id) == 0x878);
static_assert(offsetof(Mappings, fid_s14_pos_x) == 0x880);
static_assert(offsetof(Mappings, fid_s14_pos_y) == 0x888);
static_assert(offsetof(Mappings, fid_s14_pos_z) == 0x890);
static_assert(offsetof(Mappings, fid_s14_yaw) == 0x898);
static_assert(offsetof(Mappings, fid_s14_pitch) == 0x8A0);
static_assert(offsetof(Mappings, fid_s14_has_rot) == 0x8A8);
static_assert(offsetof(Mappings, cls_s18_packet_entity_teleport) == 0x8B0);
static_assert(offsetof(Mappings, fid_s18_entity_id) == 0x8B8);
static_assert(offsetof(Mappings, fid_s18_pos_x) == 0x8C0);
static_assert(offsetof(Mappings, fid_s18_pos_y) == 0x8C8);
static_assert(offsetof(Mappings, fid_s18_pos_z) == 0x8D0);
static_assert(offsetof(Mappings, fid_s18_yaw) == 0x8D8);
static_assert(offsetof(Mappings, fid_s18_pitch) == 0x8E0);
static_assert(offsetof(Mappings, cls_s19_packet_entity_status) == 0x8E8);
static_assert(offsetof(Mappings, fid_s19_entity_id) == 0x8F0);
static_assert(offsetof(Mappings, cls_s29_packet_sound_effect) == 0x8F8);
static_assert(offsetof(Mappings, cls_s06_packet_update_health) == 0x900);
static_assert(offsetof(Mappings, cls_entity_other_player_mp) == 0x908);
static_assert(offsetof(Mappings, fid_other_player_pos_rot_increments) == 0x910);
static_assert(offsetof(Mappings, fid_other_player_mp_x) == 0x918);
static_assert(offsetof(Mappings, fid_other_player_mp_y) == 0x920);
static_assert(offsetof(Mappings, fid_other_player_mp_z) == 0x928);
static_assert(offsetof(Mappings, fid_other_player_mp_yaw) == 0x930);
static_assert(offsetof(Mappings, cls_gui_inventory) == 0x938);
static_assert(offsetof(Mappings, cls_entity_player_sp) == 0x940);
static_assert(offsetof(Mappings, fid_player_sp_render_arm_yaw) == 0x948);
static_assert(offsetof(Mappings, fid_player_sp_render_arm_pitch) == 0x950);
static_assert(offsetof(Mappings, fid_player_sp_prev_render_arm_yaw) == 0x958);
static_assert(offsetof(Mappings, fid_player_sp_prev_render_arm_pitch) == 0x960);
static_assert(offsetof(Mappings, fid_player_sp_time_in_portal) == 0x968);
static_assert(offsetof(Mappings, fid_player_sp_prev_time_in_portal) == 0x970);
static_assert(offsetof(Mappings, mid_player_sp_add_chat_message) == 0x978);
static_assert(offsetof(Mappings, cls_entity_renderer) == 0x980);
static_assert(offsetof(Mappings, mid_entity_renderer_get_mouse_over) == 0x988);
static_assert(offsetof(Mappings, mid_entity_renderer_render_hand) == 0x990);
static_assert(offsetof(Mappings, mid_entity_renderer_orient_camera) == 0x998);
static_assert(offsetof(Mappings, mid_entity_renderer_get_fov_modifier) == 0x9A0);
static_assert(offsetof(Mappings, mid_entity_renderer_setup_camera_transform) == 0x9A8);
static_assert(offsetof(Mappings, cls_framebuffer) == 0x9B0);
static_assert(offsetof(Mappings, mid_framebuffer_bind) == 0x9B8);
static_assert(offsetof(Mappings, cls_render_global) == 0x9C0);
static_assert(offsetof(Mappings, fid_render_global_entities_dirty) == 0x9C8);
static_assert(offsetof(Mappings, cls_active_render_info) == 0x9D0);
static_assert(offsetof(Mappings, smid_active_render_info_update) == 0x9D8);
static_assert(offsetof(Mappings, cls_item) == 0x9E0);
static_assert(offsetof(Mappings, smid_item_get_id_from_item) == 0x9E8);
static_assert(offsetof(Mappings, smid_item_get_item_by_id) == 0x9F0);
static_assert(offsetof(Mappings, cls_vec3) == 0x9F8);
static_assert(offsetof(Mappings, mid_vec3_init) == 0xA00);
static_assert(offsetof(Mappings, fid_vec3_x_coord) == 0xA08);
static_assert(offsetof(Mappings, fid_vec3_y_coord) == 0xA10);
static_assert(offsetof(Mappings, fid_vec3_z_coord) == 0xA18);
static_assert(offsetof(Mappings, cls_extended_block_storage) == 0xA20);
static_assert(offsetof(Mappings, fid_ebs_block_data) == 0xA28);
static_assert(offsetof(Mappings, cls_world_client) == 0xA30);
static_assert(offsetof(Mappings, cls_chunk_provider_client) == 0xA38);
static_assert(offsetof(Mappings, fid_chunk_provider_chunk_listing) == 0xA40);
static_assert(offsetof(Mappings, mid_chunk_provider_unload_chunk) == 0xA48);
static_assert(offsetof(Mappings, cls_entity_item) == 0xA50);
static_assert(offsetof(Mappings, mid_entity_item_get_item_stack) == 0xA58);
static_assert(offsetof(Mappings, mid_entity_item_search_nearby) == 0xA60);
static_assert(offsetof(Mappings, cls_s3f_packet_custom_payload) == 0xA68);
static_assert(offsetof(Mappings, fid_s3f_channel) == 0xA70);
static_assert(offsetof(Mappings, fid_s3f_data) == 0xA78);
static_assert(offsetof(Mappings, cls_packet_buffer) == 0xA80);
static_assert(offsetof(Mappings, mid_packet_buffer_readable_bytes) == 0xA88);
static_assert(offsetof(Mappings, mid_packet_buffer_reader_index) == 0xA90);
static_assert(offsetof(Mappings, mid_packet_buffer_get_bytes) == 0xA98);
static_assert(offsetof(Mappings, cls_java_util_uuid) == 0xAA0);
static_assert(offsetof(Mappings, mid_uuid_get_least_sig_bits) == 0xAA8);
static_assert(offsetof(Mappings, mid_uuid_get_most_sig_bits) == 0xAB0);
static_assert(offsetof(Mappings, cls_chat_component_text) == 0xAB8);
static_assert(offsetof(Mappings, mid_chat_component_text_init) == 0xAC0);
static_assert(offsetof(Mappings, cls_stat_collector) == 0xAC8);
static_assert(offsetof(Mappings, smid_stat_collector_translate_to_local) == 0xAD0);
static_assert(offsetof(Mappings, cls_c01_packet_chat_message) == 0xAD8);
static_assert(offsetof(Mappings, fid_c01_message) == 0xAE0);
static_assert(offsetof(Mappings, cls_c14_packet_tab_complete) == 0xAE8);
static_assert(offsetof(Mappings, fid_c14_message) == 0xAF0);
static_assert(offsetof(Mappings, cls_score_player_team) == 0xAF8);
static_assert(offsetof(Mappings, fid_score_player_team_name_prefix) == 0xB00);
static_assert(offsetof(Mappings, cls_net_handler_play_client) == 0xB08);
static_assert(offsetof(Mappings, fid_net_handler_player_info_map) == 0xB10);
static_assert(offsetof(Mappings, fid_net_handler_net_manager) == 0xB18);
static_assert(offsetof(Mappings, cls_ichat_component) == 0xB20);
static_assert(offsetof(Mappings, mid_ichat_component_get_formatted_text) == 0xB28);
static_assert(offsetof(Mappings, cls_ichat_component_serializer) == 0xB30);
static_assert(offsetof(Mappings, smid_ichat_serializer_json_to_component) == 0xB38);
static_assert(offsetof(Mappings, cls_renderer_living_entity) == 0xB40);
static_assert(offsetof(Mappings, hook_rle_render_name_method_name) == 0xB48);
static_assert(offsetof(Mappings, hook_rle_render_name_method_desc) == 0xB50);
static_assert(offsetof(Mappings, mid_rle_render_name) == 0xB58);
static_assert(offsetof(Mappings, hook_rle_can_render_name_method_name) == 0xB60);
static_assert(offsetof(Mappings, hook_rle_can_render_name_method_desc) == 0xB68);
static_assert(offsetof(Mappings, mid_rle_can_render_name) == 0xB70);
static_assert(offsetof(Mappings, hook_rle_do_render_method_name) == 0xB78);
static_assert(offsetof(Mappings, hook_rle_do_render_method_desc) == 0xB80);
static_assert(offsetof(Mappings, mid_rle_do_render) == 0xB88);
static_assert(offsetof(Mappings, cls_font_renderer) == 0xB90);
static_assert(offsetof(Mappings, mid_font_renderer_get_string_width) == 0xB98);
static_assert(offsetof(Mappings, mid_font_renderer_draw_string) == 0xBA0);
static_assert(offsetof(Mappings, cls_gl_state_manager) == 0xBA8);
static_assert(offsetof(Mappings, smid_gl_enable_depth) == 0xBB0);
static_assert(offsetof(Mappings, smid_gl_disable_depth) == 0xBB8);
static_assert(offsetof(Mappings, smid_gl_enable_lighting) == 0xBC0);
static_assert(offsetof(Mappings, smid_gl_disable_lighting) == 0xBC8);
static_assert(offsetof(Mappings, smid_gl_enable_texture_2d) == 0xBD0);
static_assert(offsetof(Mappings, smid_gl_disable_texture_2d) == 0xBD8);
static_assert(offsetof(Mappings, smid_gl_enable_blend) == 0xBE0);
static_assert(offsetof(Mappings, smid_gl_disable_blend) == 0xBE8);
static_assert(offsetof(Mappings, smid_gl_enable_cull) == 0xBF0);
static_assert(offsetof(Mappings, smid_gl_disable_cull) == 0xBF8);
static_assert(offsetof(Mappings, smid_gl_enable_alpha) == 0xC00);
static_assert(offsetof(Mappings, smid_gl_disable_alpha) == 0xC08);
static_assert(offsetof(Mappings, smid_gl_blend_func) == 0xC10);
static_assert(offsetof(Mappings, smid_gl_color) == 0xC18);
static_assert(offsetof(Mappings, cls_c02_packet_use_entity) == 0xC20);
static_assert(offsetof(Mappings, cls_c0b_packet_entity_action) == 0xC28);
static_assert(offsetof(Mappings, fid_c0b_action) == 0xC30);
static_assert(offsetof(Mappings, cls_s08_packet_player_pos_look) == 0xC38);
static_assert(offsetof(Mappings, cls_s27_packet_explosion) == 0xC40);
static_assert(offsetof(Mappings, fid_s27_motion_x) == 0xC48);
static_assert(offsetof(Mappings, fid_s27_motion_y) == 0xC50);
static_assert(offsetof(Mappings, fid_s27_motion_z) == 0xC58);
static_assert(offsetof(Mappings, cls_s00_packet_keep_alive) == 0xC60);
static_assert(offsetof(Mappings, cls_c00_packet_keep_alive) == 0xC68);
static_assert(offsetof(Mappings, cls_potion) == 0xC70);
static_assert(offsetof(Mappings, sfid_potion_types) == 0xC78);
static_assert(offsetof(Mappings, cls_render_player) == 0xC80);
static_assert(offsetof(Mappings, hook_render_player_do_render_name) == 0xC88);
static_assert(offsetof(Mappings, hook_render_player_do_render_desc) == 0xC90);
static_assert(offsetof(Mappings, mid_render_player_do_render) == 0xC98);
static_assert(offsetof(Mappings, cls_render_item) == 0xCA0);
static_assert(offsetof(Mappings, fid_render_item_z_level) == 0xCA8);
static_assert(offsetof(Mappings, mid_render_item_init_1_7) == 0xCB0);
static_assert(offsetof(Mappings, mid_render_item_into_gui) == 0xCB8);
static_assert(offsetof(Mappings, mid_render_item_overlay_into_gui) == 0xCC0);
static_assert(offsetof(Mappings, cls_render_helper) == 0xCC8);
static_assert(offsetof(Mappings, smid_render_helper_enable_item_lighting) == 0xCD0);
static_assert(offsetof(Mappings, smid_render_helper_disable_item_lighting) == 0xCD8);
static_assert(offsetof(Mappings, cls_entity_list) == 0xCE0);
static_assert(offsetof(Mappings, smid_entity_list_get_entity_id) == 0xCE8);
static_assert(offsetof(Mappings, cls_chunk) == 0xCF0);
static_assert(offsetof(Mappings, fid_chunk_storage_arrays) == 0xCF8);
static_assert(offsetof(Mappings, fid_chunk_x_position) == 0xD00);
static_assert(offsetof(Mappings, fid_chunk_z_position) == 0xD08);
static_assert(offsetof(Mappings, cls_s2a_packet_particles) == 0xD10);

} // namespace mc

namespace minecraft {

using MinecraftVersion  = ::slinky::mc::MinecraftVersion;
using MappingFlavor     = ::slinky::mc::MappingFlavor;
using ClassLoaderHandle = ::slinky::mc::ClassLoaderHandle;
using Mappings          = ::slinky::mc::Mappings;

// Context pair passed to resolve_all_mappings (matches v18[2] in sub_180039EC0)
struct MappingResolveContext {
    JNIEnv* env = nullptr;
    ClassLoaderHandle* class_loader = nullptr;
};

// Error codes returned by mappings/classloader initialization
inline constexpr int kMappingsOk                   = 0;
inline constexpr int kErrorClientThreadNotFound    = 1002;
inline constexpr int kErrorUnsupportedMcVersion    = 1003;
inline constexpr int kErrorThreadReflectionFailed  = 2000;
inline constexpr int kErrorJniLookupFailed         = 2004;
inline constexpr int kErrorJniGlobalRefFailed      = 2005;

// Detects version/flavor, resolves all 413 JNI slots, and creates child SecureClassLoader
int initialize_mappings(Mappings* mappings, JNIEnv* env, ClassLoaderHandle* out_child_loader) noexcept; // sub_180039EC0

// Resolves all 1.7.10 / 1.8.9 Vanilla / MCP / Forge classes, fields, and methods
int resolve_all_mappings(Mappings* mappings, MappingResolveContext* ctx) noexcept; // sub_180039FB0
int resolve_all_mappings(Mappings* mappings, JNIEnv* env, ClassLoaderHandle* loader) noexcept;

// Releases all global JNI class references stored in mappings
void destroy_mappings(Mappings* mappings, JNIEnv* env) noexcept; // sub_18003FE20

// Finds the 'Client thread' context ClassLoader via Thread.getAllStackTraces()
int find_client_thread_class_loader(ClassLoaderHandle* out_loader, JNIEnv* env) noexcept; // sub_180040710

// Wraps an existing java.lang.ClassLoader local ref into a GlobalRef ClassLoaderHandle
int init_class_loader_handle(ClassLoaderHandle* out_loader, JNIEnv* env, jobject loader_obj) noexcept; // sub_1800409D0

// Converts '/' to '.' and invokes ClassLoader.loadClass(name)
jclass load_class(const ClassLoaderHandle* loader, JNIEnv* env, const char* class_name) noexcept; // sub_180040A60

// Returns the underlying jobject GlobalRef from a ClassLoaderHandle
jobject get_class_loader_object(const ClassLoaderHandle* loader) noexcept; // sub_180040C10

// Deletes the GlobalRef held by a ClassLoaderHandle and zeroes it
void destroy_class_loader_handle(ClassLoaderHandle* loader, JNIEnv* env) noexcept; // sub_180040C20

// Probes Vanilla obfuscated Minecraft class ('bao' = 1.7.10, 'ave' = 1.8.9)
int detect_vanilla_version(Mappings* mappings, const ClassLoaderHandle* loader, JNIEnv* env) noexcept; // sub_180040C60

// Probes Forge SRG, MCP/Lunar, and Vanilla 1.7.10 / 1.8.9 and populates version + mapping_flavor
int detect_version_and_flavor(Mappings* mappings, const ClassLoaderHandle* loader, JNIEnv* env) noexcept; // sub_180040D30

} // namespace minecraft

namespace mc {
using namespace ::slinky::minecraft;
} // namespace mc

} // namespace slinky
