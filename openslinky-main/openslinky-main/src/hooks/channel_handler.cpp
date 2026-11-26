#include "hooks/channel_handler.hpp"

#include "embedded_assets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::hooks {

namespace {

int define_and_register_single_native_class(
    JNIEnv* env,
    const char* class_name,
    const minecraft::ClassLoaderHandle* loader,
    const uint8_t* class_bytes,
    jsize class_size,
    const JNINativeMethod* method,
    jclass* out_local_class) noexcept
{
    jclass cls = minecraft::load_class(loader, env, class_name);
    if (cls == nullptr) {
        env->ExceptionClear();
        cls = env->DefineClass(
            class_name,
            minecraft::get_class_loader_object(loader),
            reinterpret_cast<const jbyte*>(class_bytes),
            class_size);
        if (cls == nullptr) {
            return 2006;
        }
    }

    if (env->RegisterNatives(cls, method, 1) != 0) {
        return 2001;
    }

    *out_local_class = cls;
    return 0;
}

} // namespace

int define_and_register_channel_handler(
    events::JniContext* jni,
    const minecraft::ClassLoaderHandle* loader,
    ChannelHandlerHandle* out_handle,
    void* on_packet_fn) noexcept
{
    JNINativeMethod native_method{
        const_cast<char*>("onPacket"),
        const_cast<char*>("(Ljava/lang/Object;ZJI)I"),
        on_packet_fn,
    };

    JNIEnv* env = jni->env;
    jclass local_cls = nullptr;
    const int status = define_and_register_single_native_class(
        env,
        "SlinkyChannelHandler",
        loader,
        assets::kClassSlinkyChannelHandler,
        static_cast<jsize>(assets::kClassSlinkyChannelHandler_size),
        &native_method,
        &local_cls);
    if (status != 0) {
        return status;
    }

    const jmethodID init_mid = env->GetMethodID(local_cls, "<init>", "()V");
    out_handle->init_mid = init_mid;
    if (init_mid == nullptr) {
        return 2000;
    }

    const auto global_cls = static_cast<jclass>(env->NewGlobalRef(local_cls));
    out_handle->handler_class = global_cls;
    if (global_cls == nullptr) {
        return 2005;
    }

    return 0;
}

void unregister_channel_handler(JNIEnv* env, ChannelHandlerHandle* handle) noexcept {
    if (handle != nullptr && handle->handler_class != nullptr) {
        env->UnregisterNatives(handle->handler_class);
        env->DeleteGlobalRef(handle->handler_class);
        handle->handler_class = nullptr;
        handle->init_mid = nullptr;
    }
}

int ensure_channel_handler_installed(
    events::JniContext* jni,
    const ChannelHandlerHandle* handle) noexcept
{
    jobject mc = minecraft::mc_get_instance(jni);
    if (mc == nullptr) {
        return 1020;
    }

    jobject net_handler = minecraft::mc_get_net_handler(jni, mc);
    if (net_handler == nullptr) {
        return 1021;
    }

    jobject net_mgr = minecraft::net_handler_get_network_manager(jni, net_handler);
    jobject channel = minecraft::network_manager_get_channel(jni, net_mgr);
    if (channel == nullptr) {
        return 1021;
    }

    jobject pipeline = minecraft::netty_channel_get_pipeline(jni, channel);
    if (pipeline == nullptr) {
        return 1021;
    }

    if (minecraft::netty_pipeline_get_by_class(jni, pipeline, handle->handler_class) != nullptr) {
        return 0;
    }

    JNIEnv* env = jni->env;
    jobject handler_obj = env->NewObject(handle->handler_class, handle->init_mid);
    if (handler_obj == nullptr) {
        return 2005;
    }

    minecraft::netty_pipeline_add_before(jni, pipeline, "packet_handler", "slinky_handler", handler_obj);
    if (env->ExceptionCheck() != JNI_FALSE) {
        return 2004;
    }
    return 0;
}

int remove_channel_handler_from_pipeline(
    events::JniContext* jni,
    const ChannelHandlerHandle* handle) noexcept
{
    jobject mc = minecraft::mc_get_instance(jni);
    if (mc == nullptr) {
        return 1020;
    }

    jobject net_handler = minecraft::mc_get_net_handler(jni, mc);
    if (net_handler == nullptr) {
        return 1021;
    }

    jobject net_mgr = minecraft::net_handler_get_network_manager(jni, net_handler);
    jobject channel = minecraft::network_manager_get_channel(jni, net_mgr);
    if (channel == nullptr) {
        return 1021;
    }

    jobject pipeline = minecraft::netty_channel_get_pipeline(jni, channel);
    if (pipeline == nullptr) {
        return 1021;
    }

    if (minecraft::netty_pipeline_get_by_class(jni, pipeline, handle->handler_class) == nullptr) {
        return 0;
    }

    minecraft::netty_pipeline_remove_by_class(jni, pipeline, handle->handler_class);
    if (jni->env->ExceptionOccurred() != nullptr) {
        return 2004;
    }
    return 0;
}

} // namespace slinky::hooks
