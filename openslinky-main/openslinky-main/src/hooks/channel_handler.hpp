#pragma once

#include <cstdint>
#include <jni.h>

#include "events/events.hpp"
#include "minecraft/mappings.hpp"

namespace slinky::hooks {

// Cached GlobalRef + constructor for injected SlinkyChannelHandler (16 bytes @ 0x180314428)
struct ChannelHandlerHandle {
    jclass handler_class = nullptr; // +0x00
    jmethodID init_mid = nullptr;   // +0x08
};
static_assert(sizeof(ChannelHandlerHandle) == 16, "ChannelHandlerHandle must be 16 bytes");

using OnNettyPacketFn = jint (JNICALL*)(
    JNIEnv* env,
    jobject handler_this,
    jobject packet,
    jboolean outbound,
    jlong elapsed_ms,
    jint packet_id);

// Defines SlinkyChannelHandler in `loader` and registers native `onPacket` (sub_18002E530 / sub_18002CAE0)
int define_and_register_channel_handler(
    events::JniContext* jni,
    const minecraft::ClassLoaderHandle* loader,
    ChannelHandlerHandle* out_handle,
    void* on_packet_fn) noexcept;

// Unregisters natives and deletes the GlobalRef on `handle` (sub_18002E5F0)
void unregister_channel_handler(JNIEnv* env, ChannelHandlerHandle* handle) noexcept;

// Injects `"slinky_handler"` before `"packet_handler"` on the active Netty pipeline (sub_18002E640)
int ensure_channel_handler_installed(
    events::JniContext* jni,
    const ChannelHandlerHandle* handle) noexcept;

// Removes `SlinkyChannelHandler` from the active Netty pipeline if present (sub_18002E730)
int remove_channel_handler_from_pipeline(
    events::JniContext* jni,
    const ChannelHandlerHandle* handle) noexcept;

} // namespace slinky::hooks
