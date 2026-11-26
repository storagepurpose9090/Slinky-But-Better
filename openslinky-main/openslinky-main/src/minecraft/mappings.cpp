#include "minecraft/mappings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace slinky::minecraft {

namespace {

// Static descriptor formatting buffers matching byte_180314920 and byte_180314990
char s_itemstack_block_ctor_desc[100]{};
char s_render_player_do_render_desc[100]{};

struct ClassNames2V {
    const char* v1_7_obf;
    const char* v1_7_mcp;
    const char* v1_8_obf;
    const char* v1_8_mcp;
};

struct MemberSpec {
    const char* name;
    const char* desc;
};

using FlavorMembers1V = MemberSpec[3];
using FlavorMembers2V = MemberSpec[2][3];

struct FlavorStrings {
    const char* vanilla;
    const char* mcp;
    const char* srg;
};

inline const char* select_flavor_str(int flavor, const char* vanilla, const char* mcp, const char* srg) noexcept {
    if (flavor == 0) return vanilla;
    if (flavor == 1) return mcp;
    return srg;
}

inline const char* select_flavor_str_2v(int ver, int flavor, const FlavorStrings& v17, const FlavorStrings& v18) noexcept {
    const FlavorStrings& cur = (ver == 0) ? v17 : v18;
    return select_flavor_str(flavor, cur.vanilla, cur.mcp, cur.srg);
}

int resolve_class_const(JNIEnv* env, const ClassLoaderHandle* loader, const char* class_name, jclass* out_cls) noexcept {
    jclass local = load_class(loader, env, class_name);
    if (local == nullptr) {
        return kErrorJniLookupFailed;
    }
    jclass global = static_cast<jclass>(env->NewGlobalRef(local));
    if (global == nullptr) {
        return kErrorJniGlobalRefFailed;
    }
    *out_cls = global;
    return kMappingsOk;
}

int resolve_class_1v(
    JNIEnv* env,
    const ClassLoaderHandle* loader,
    bool is_deobf,
    const char* obf_name,
    const char* mcp_name,
    jclass* out_cls
) noexcept {
    return resolve_class_const(env, loader, is_deobf ? mcp_name : obf_name, out_cls);
}

int resolve_class_2v(
    JNIEnv* env,
    const ClassLoaderHandle* loader,
    int ver,
    bool is_deobf,
    const ClassNames2V& names,
    jclass* out_cls
) noexcept {
    const char* chosen = (ver == 0)
        ? (is_deobf ? names.v1_7_mcp : names.v1_7_obf)
        : (is_deobf ? names.v1_8_mcp : names.v1_8_obf);
    return resolve_class_const(env, loader, chosen, out_cls);
}

int resolve_field_const(JNIEnv* env, jclass cls, const char* name, const char* desc, jfieldID* out_fid) noexcept {
    jfieldID fid = env->GetFieldID(cls, name, desc);
    if (fid == nullptr) {
        return kErrorJniLookupFailed;
    }
    *out_fid = fid;
    return kMappingsOk;
}

int resolve_static_field_const(JNIEnv* env, jclass cls, const char* name, const char* desc, jfieldID* out_fid) noexcept {
    jfieldID fid = env->GetStaticFieldID(cls, name, desc);
    if (fid == nullptr) {
        return kErrorJniLookupFailed;
    }
    *out_fid = fid;
    return kMappingsOk;
}

int resolve_method_const(JNIEnv* env, jclass cls, const char* name, const char* desc, jmethodID* out_mid) noexcept {
    jmethodID mid = env->GetMethodID(cls, name, desc);
    if (mid == nullptr) {
        return kErrorJniLookupFailed;
    }
    *out_mid = mid;
    return kMappingsOk;
}

int resolve_static_method_const(JNIEnv* env, jclass cls, const char* name, const char* desc, jmethodID* out_mid) noexcept {
    jmethodID mid = env->GetStaticMethodID(cls, name, desc);
    if (mid == nullptr) {
        return kErrorJniLookupFailed;
    }
    *out_mid = mid;
    return kMappingsOk;
}

int resolve_field_1v(
    JNIEnv* env,
    jclass cls,
    int flavor,
    const FlavorMembers1V& specs,
    jfieldID* out_fid,
    const char* override_desc = nullptr
) noexcept {
    const MemberSpec& s = specs[flavor];
    return resolve_field_const(env, cls, s.name, override_desc ? override_desc : s.desc, out_fid);
}

int resolve_static_field_1v(
    JNIEnv* env,
    jclass cls,
    int flavor,
    const FlavorMembers1V& specs,
    jfieldID* out_fid
) noexcept {
    const MemberSpec& s = specs[flavor];
    return resolve_static_field_const(env, cls, s.name, s.desc, out_fid);
}

int resolve_method_1v(
    JNIEnv* env,
    jclass cls,
    int flavor,
    const FlavorMembers1V& specs,
    jmethodID* out_mid
) noexcept {
    const MemberSpec& s = specs[flavor];
    return resolve_method_const(env, cls, s.name, s.desc, out_mid);
}

int resolve_static_method_1v(
    JNIEnv* env,
    jclass cls,
    int flavor,
    const FlavorMembers1V& specs,
    jmethodID* out_mid
) noexcept {
    const MemberSpec& s = specs[flavor];
    return resolve_static_method_const(env, cls, s.name, s.desc, out_mid);
}

int resolve_field_2v(
    JNIEnv* env,
    jclass cls,
    int ver,
    int flavor,
    const FlavorMembers2V& specs,
    jfieldID* out_fid,
    const char* override_desc = nullptr
) noexcept {
    const MemberSpec& s = specs[ver][flavor];
    return resolve_field_const(env, cls, s.name, override_desc ? override_desc : s.desc, out_fid);
}

int resolve_static_field_2v(
    JNIEnv* env,
    jclass cls,
    int ver,
    int flavor,
    const FlavorMembers2V& specs,
    jfieldID* out_fid
) noexcept {
    const MemberSpec& s = specs[ver][flavor];
    return resolve_static_field_const(env, cls, s.name, s.desc, out_fid);
}

int resolve_method_2v(
    JNIEnv* env,
    jclass cls,
    int ver,
    int flavor,
    const FlavorMembers2V& specs,
    jmethodID* out_mid
) noexcept {
    const MemberSpec& s = specs[ver][flavor];
    return resolve_method_const(env, cls, s.name, s.desc, out_mid);
}

int resolve_static_method_2v(
    JNIEnv* env,
    jclass cls,
    int ver,
    int flavor,
    const FlavorMembers2V& specs,
    jmethodID* out_mid
) noexcept {
    const MemberSpec& s = specs[ver][flavor];
    return resolve_static_method_const(env, cls, s.name, s.desc, out_mid);
}

void resolve_display_framebuffer_fields(JNIEnv* env, Mappings* mappings) noexcept {
    jfieldID fb_w = env->GetStaticFieldID(mappings->cls_lwjgl_display, "framebufferWidth", "I");
    mappings->sfid_display_framebuffer_width = fb_w;
    if (fb_w != nullptr) {
        jfieldID fb_h = env->GetStaticFieldID(mappings->cls_lwjgl_display, "framebufferHeight", "I");
        mappings->sfid_display_framebuffer_height = fb_h;
        if (fb_h != nullptr) {
            return;
        }
    }
    env->ExceptionClear();
    mappings->sfid_display_framebuffer_width = mappings->sfid_display_width;
    mappings->sfid_display_framebuffer_height = mappings->sfid_display_height;
}

inline void delete_global_class_ref(JNIEnv* env, jclass* slot) noexcept {
    if (*slot != nullptr) {
        env->DeleteGlobalRef(*slot);
        *slot = nullptr;
    }
}

} // namespace

// ============================================================================
// sub_180040710 - Locate "Client thread" context ClassLoader
// ============================================================================
int find_client_thread_class_loader(ClassLoaderHandle* out_loader, JNIEnv* env) noexcept {
    jclass cls_thread = env->FindClass("java/lang/Thread");
    if (cls_thread == nullptr) {
        return kErrorThreadReflectionFailed;
    }
    jmethodID mid_all_stacks = env->GetStaticMethodID(cls_thread, "getAllStackTraces", "()Ljava/util/Map;");
    if (mid_all_stacks == nullptr) {
        return kErrorThreadReflectionFailed;
    }
    jclass cls_map = env->FindClass("java/util/Map");
    if (cls_map == nullptr) {
        return kErrorThreadReflectionFailed;
    }
    jmethodID mid_key_set = env->GetMethodID(cls_map, "keySet", "()Ljava/util/Set;");
    if (mid_key_set == nullptr) {
        return kErrorThreadReflectionFailed;
    }

    jobject stack_map = env->CallStaticObjectMethod(cls_thread, mid_all_stacks);
    jobject key_set   = env->CallObjectMethod(stack_map, mid_key_set);

    jclass cls_collection = env->FindClass("java/util/Collection");
    if (cls_collection == nullptr) {
        return kErrorThreadReflectionFailed;
    }
    jmethodID mid_to_array = env->GetMethodID(cls_collection, "toArray", "()[Ljava/lang/Object;");
    if (mid_to_array == nullptr) {
        return kErrorThreadReflectionFailed;
    }
    jmethodID mid_get_name = env->GetMethodID(cls_thread, "getName", "()Ljava/lang/String;");
    if (mid_get_name == nullptr) {
        return kErrorThreadReflectionFailed;
    }

    auto threads = static_cast<jobjectArray>(env->CallObjectMethod(key_set, mid_to_array));
    const jsize thread_count = env->GetArrayLength(threads);
    if (thread_count <= 0) {
        return kErrorClientThreadNotFound;
    }

    jobject client_thread = nullptr;
    for (jsize i = 0; i < thread_count; ++i) {
        jobject thread = env->GetObjectArrayElement(threads, i);
        auto name_str  = static_cast<jstring>(env->CallObjectMethod(thread, mid_get_name));
        const char* utf = env->GetStringUTFChars(name_str, nullptr);
        const int cmp = std::strcmp(utf, "Client thread");
        env->ReleaseStringUTFChars(name_str, utf);
        if (cmp == 0) {
            client_thread = thread;
            break;
        }
    }
    if (client_thread == nullptr) {
        return kErrorClientThreadNotFound;
    }

    jmethodID mid_get_ctx_loader = env->GetMethodID(cls_thread, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    if (mid_get_ctx_loader == nullptr) {
        return kErrorThreadReflectionFailed;
    }

    jobject loader_obj = env->CallObjectMethod(client_thread, mid_get_ctx_loader);
    jclass cls_loader  = env->GetObjectClass(loader_obj);
    jmethodID mid_load = env->GetMethodID(cls_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (mid_load == nullptr) {
        return kErrorJniLookupFailed;
    }

    jobject global_loader = env->NewGlobalRef(loader_obj);
    if (global_loader == nullptr) {
        return kErrorJniGlobalRefFailed;
    }

    out_loader->loader = global_loader;
    out_loader->load_class_method = mid_load;
    return kMappingsOk;
}

// ============================================================================
// sub_1800409D0 - Wrap java.lang.ClassLoader object into ClassLoaderHandle
// ============================================================================
int init_class_loader_handle(ClassLoaderHandle* out_loader, JNIEnv* env, jobject loader_obj) noexcept {
    jclass cls_loader  = env->GetObjectClass(loader_obj);
    jmethodID mid_load = env->GetMethodID(cls_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (mid_load == nullptr) {
        return kErrorJniLookupFailed;
    }
    jobject global_loader = env->NewGlobalRef(loader_obj);
    if (global_loader == nullptr) {
        return kErrorJniGlobalRefFailed;
    }
    out_loader->loader = global_loader;
    out_loader->load_class_method = mid_load;
    return kMappingsOk;
}

// ============================================================================
// sub_180040A60 - Convert '/' -> '.' and invoke ClassLoader.loadClass(name)
// ============================================================================
jclass load_class(const ClassLoaderHandle* loader, JNIEnv* env, const char* class_name) noexcept {
    const size_t len = std::strlen(class_name);
    char* dotted = static_cast<char*>(std::malloc(len + 1));
    if (dotted == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < len; ++i) {
        const char ch = class_name[i];
        dotted[i] = (ch == '/') ? '.' : ch;
    }
    dotted[len] = '\0';

    jstring jname = env->NewStringUTF(dotted);
    std::free(dotted);
    return static_cast<jclass>(env->CallObjectMethod(loader->loader, loader->load_class_method, jname));
}

// ============================================================================
// sub_180040C10 - Return underlying GlobalRef jobject from ClassLoaderHandle
// ============================================================================
jobject get_class_loader_object(const ClassLoaderHandle* loader) noexcept {
    return loader->loader;
}

// ============================================================================
// sub_180040C20 - Release ClassLoaderHandle GlobalRef
// ============================================================================
void destroy_class_loader_handle(ClassLoaderHandle* loader, JNIEnv* env) noexcept {
    if (loader->loader != nullptr) {
        env->DeleteGlobalRef(loader->loader);
        loader->loader = nullptr;
    }
}

// ============================================================================
// sub_180040C60 - Detect Vanilla 1.7.10 ("bao") vs 1.8.9 ("ave")
// ============================================================================
int detect_vanilla_version(Mappings* mappings, const ClassLoaderHandle* loader, JNIEnv* env) noexcept {
    jclass cls_17 = load_class(loader, env, "bao");
    if (cls_17 != nullptr && env->GetFieldID(cls_17, "I", "Ljava/io/File;") != nullptr) {
        mappings->version = static_cast<int32_t>(MinecraftVersion::k1_7_10);
        mappings->mapping_flavor = static_cast<int32_t>(MappingFlavor::kVanilla);
        return kMappingsOk;
    }
    env->ExceptionClear();

    jclass cls_18 = load_class(loader, env, "ave");
    if (cls_18 != nullptr && env->GetFieldID(cls_18, "N", "Ljava/io/File;") != nullptr) {
        mappings->version = static_cast<int32_t>(MinecraftVersion::k1_8_9);
        mappings->mapping_flavor = static_cast<int32_t>(MappingFlavor::kVanilla);
        return kMappingsOk;
    }
    env->ExceptionClear();
    return kErrorUnsupportedMcVersion;
}

// ============================================================================
// sub_180040D30 - Detect Minecraft version (1.7.10 / 1.8.9) & mapping flavor
// ============================================================================
int detect_version_and_flavor(Mappings* mappings, const ClassLoaderHandle* loader, JNIEnv* env) noexcept {
    jclass cls_mc = load_class(loader, env, "net.minecraft.client.Minecraft");
    if (cls_mc == nullptr) {
        env->ExceptionClear();
        return detect_vanilla_version(mappings, loader, env);
    }

    if (env->GetFieldID(cls_mc, "field_71439_g", "Lnet/minecraft/client/entity/EntityClientPlayerMP;") != nullptr) {
        mappings->version = static_cast<int32_t>(MinecraftVersion::k1_7_10);
        mappings->mapping_flavor = static_cast<int32_t>(MappingFlavor::kForgeSrg);
        return kMappingsOk;
    }
    env->ExceptionClear();

    if (env->GetFieldID(cls_mc, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;") != nullptr) {
        mappings->version = static_cast<int32_t>(MinecraftVersion::k1_8_9);
        mappings->mapping_flavor = static_cast<int32_t>(MappingFlavor::kForgeSrg);
        return kMappingsOk;
    }
    env->ExceptionClear();

    if (env->GetFieldID(cls_mc, "thePlayer", "Lnet/minecraft/client/entity/EntityClientPlayerMP;") != nullptr) {
        mappings->version = static_cast<int32_t>(MinecraftVersion::k1_7_10);
        mappings->mapping_flavor = static_cast<int32_t>(MappingFlavor::kMcp);
        mappings->lunar_underscore_inner = false;
        return kMappingsOk;
    }
    env->ExceptionClear();

    if (env->GetFieldID(cls_mc, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;") != nullptr) {
        mappings->version = static_cast<int32_t>(MinecraftVersion::k1_8_9);
        mappings->mapping_flavor = static_cast<int32_t>(MappingFlavor::kMcp);
        mappings->lunar_underscore_inner = false;
        return kMappingsOk;
    }
    env->ExceptionClear();
    return kErrorUnsupportedMcVersion;
}

// ============================================================================
// sub_180039EC0 - Full mappings & child SecureClassLoader bootstrap
// ============================================================================
int initialize_mappings(Mappings* mappings, JNIEnv* env, ClassLoaderHandle* out_child_loader) noexcept {
    if (env->PushLocalFrame(256) < 0) {
        return kErrorJniLookupFailed;
    }

    ClassLoaderHandle client_loader{};
    int err = find_client_thread_class_loader(&client_loader, env);
    if (err != kMappingsOk) {
        env->PopLocalFrame(nullptr);
        return err;
    }

    err = detect_version_and_flavor(mappings, &client_loader, env);
    if (err != kMappingsOk) {
        env->PopLocalFrame(nullptr);
        return err;
    }

    MappingResolveContext ctx{env, &client_loader};
    err = resolve_all_mappings(mappings, &ctx);
    if (err != kMappingsOk) {
        env->PopLocalFrame(nullptr);
        return err;
    }

    mappings->client_class_loader = client_loader;

    if (out_child_loader != nullptr) {
        jobject child_loader_obj = env->NewObject(
            mappings->cls_secure_class_loader,
            mappings->mid_secure_class_loader_init,
            client_loader.loader
        );
        if (child_loader_obj != nullptr) {
            err = init_class_loader_handle(out_child_loader, env, child_loader_obj);
        }
    }

    env->PopLocalFrame(nullptr);
    return err;
}

// ============================================================================
// sub_180039FB0 - Resolve all 413 JNI classes, fields, methods & hook symbols
// ============================================================================
int resolve_all_mappings(Mappings* mappings, JNIEnv* env, ClassLoaderHandle* loader) noexcept {
    MappingResolveContext ctx{env, loader};
    return resolve_all_mappings(mappings, &ctx);
}

int resolve_all_mappings(Mappings* mappings, MappingResolveContext* ctx) noexcept {
    JNIEnv* env = ctx->env;
    const ClassLoaderHandle* loader = ctx->class_loader;
    const int ver = (mappings->version != 0) ? 1 : 0;
    const int flavor = (mappings->mapping_flavor >= 0 && mappings->mapping_flavor <= 2)
        ? mappings->mapping_flavor
        : 0;
    const bool is_deobf = (flavor != 0);

    if (int err = resolve_class_const(env, loader, "java.util.List", &mappings->cls_java_util_list)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_list, "get", "(I)Ljava/lang/Object;", &mappings->mid_list_get)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_list, "size", "()I", &mappings->mid_list_size)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_list, "add", "(Ljava/lang/Object;)Z", &mappings->mid_list_add)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_list, "remove", "(Ljava/lang/Object;)Z", &mappings->mid_list_remove)) return err;
    if (int err = resolve_class_const(env, loader, "java.util.Map", &mappings->cls_java_util_map)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_map, "containsKey", "(Ljava/lang/Object;)Z", &mappings->mid_map_contains_key)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", &mappings->mid_map_get)) return err;
    if (int err = resolve_class_const(env, loader, "org.lwjgl.input.Mouse", &mappings->cls_lwjgl_mouse)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_mouse, "isGrabbed", "()Z", &mappings->smid_mouse_is_grabbed)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_mouse, "setGrabbed", "(Z)V", &mappings->smid_mouse_set_grabbed)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_mouse, "isButtonDown", "(I)Z", &mappings->smid_mouse_is_button_down)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_mouse, "next", "()Z", &mappings->smid_mouse_next)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_mouse, "setCursorPosition", "(II)V", &mappings->smid_mouse_set_cursor_position)) return err;
    if (int err = resolve_class_const(env, loader, "org.lwjgl.input.Keyboard", &mappings->cls_lwjgl_keyboard)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_keyboard, "isKeyDown", "(I)Z", &mappings->smid_keyboard_is_key_down)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_keyboard, "next", "()Z", &mappings->smid_keyboard_next)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_keyboard, "getEventKey", "()I", &mappings->smid_keyboard_get_event_key)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_keyboard, "getEventKeyState", "()Z", &mappings->smid_keyboard_get_event_key_state)) return err;
    if (int err = resolve_class_const(env, loader, "org.lwjgl.opengl.Display", &mappings->cls_lwjgl_display)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_display, "update", "()V", &mappings->smid_display_update)) return err;
    if (int err = resolve_static_method_const(env, mappings->cls_lwjgl_display, "isActive", "()Z", &mappings->smid_display_is_active)) return err;
    if (int err = resolve_static_field_const(env, mappings->cls_lwjgl_display, "width", "I", &mappings->sfid_display_width)) return err;
    if (int err = resolve_static_field_const(env, mappings->cls_lwjgl_display, "height", "I", &mappings->sfid_display_height)) return err;
    resolve_display_framebuffer_fields(env, mappings);
    if (int err = resolve_class_const(env, loader, "java.lang.Object", &mappings->cls_java_lang_object)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_lang_object, "hashCode", "()I", &mappings->mid_object_hash_code)) return err;
    if (int err = resolve_class_const(env, loader, "java.security.SecureClassLoader", &mappings->cls_secure_class_loader)) return err;
    if (int err = resolve_method_const(env, mappings->cls_secure_class_loader, "<init>", "(Ljava/lang/ClassLoader;)V", &mappings->mid_secure_class_loader_init)) return err;
    if (int err = resolve_class_const(env, loader, "io.netty.channel.Channel", &mappings->cls_netty_channel)) return err;
    if (int err = resolve_method_const(env, mappings->cls_netty_channel, "pipeline", "()Lio/netty/channel/ChannelPipeline;", &mappings->mid_channel_pipeline)) return err;
    if (int err = resolve_class_const(env, loader, "io.netty.channel.ChannelPipeline", &mappings->cls_netty_channel_pipeline)) return err;
    if (int err = resolve_method_const(env, mappings->cls_netty_channel_pipeline, "addBefore", "(Ljava/lang/String;Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;", &mappings->mid_pipeline_add_before)) return err;
    if (int err = resolve_method_const(env, mappings->cls_netty_channel_pipeline, "get", "(Ljava/lang/Class;)Lio/netty/channel/ChannelHandler;", &mappings->mid_pipeline_get_by_class)) return err;
    if (int err = resolve_method_const(env, mappings->cls_netty_channel_pipeline, "remove", "(Ljava/lang/Class;)Lio/netty/channel/ChannelHandler;", &mappings->mid_pipeline_remove_by_class)) return err;
    if (int err = resolve_class_const(env, loader, "java.lang.Enum", &mappings->cls_java_lang_enum)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_lang_enum, "ordinal", "()I", &mappings->mid_enum_ordinal)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bao", "net/minecraft/client/Minecraft", "ave", "net/minecraft/client/Minecraft"}, &mappings->cls_minecraft)) return err;
    if (int err = resolve_static_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"M", "Lbao;"}, {"theMinecraft", "Lnet/minecraft/client/Minecraft;"}, {"field_71432_P", "Lnet/minecraft/client/Minecraft;"}}, {{"S", "Lave;"}, {"theMinecraft", "Lnet/minecraft/client/Minecraft;"}, {"field_71432_P", "Lnet/minecraft/client/Minecraft;"}}}, &mappings->sfid_mc_the_minecraft)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"h", "Lbjk;"}, {"thePlayer", "Lnet/minecraft/client/entity/EntityClientPlayerMP;"}, {"field_71439_g", "Lnet/minecraft/client/entity/EntityClientPlayerMP;"}}, {{"h", "Lbew;"}, {"thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;"}, {"field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;"}}}, &mappings->fid_mc_the_player)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"i", "Lsv;"}, {"renderViewEntity", "Lnet/minecraft/entity/EntityLivingBase;"}, {"field_71451_h", "Lnet/minecraft/entity/EntityLivingBase;"}}, {{"ad", "Lpk;"}, {"renderViewEntity", "Lnet/minecraft/entity/Entity;"}, {"field_175622_Z", "Lnet/minecraft/entity/Entity;"}}}, &mappings->fid_mc_render_view_entity)) return err;
    if (ver != 0) {
        if (int err = resolve_field_1v(env, mappings->cls_minecraft, flavor, {{"aa", "Lbiu;"}, {"renderManager", "Lnet/minecraft/client/renderer/entity/RenderManager;"}, {"field_175616_W", "Lnet/minecraft/client/renderer/entity/RenderManager;"}}, &mappings->fid_mc_render_manager)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_minecraft, flavor, {{"ab", "Lbjh;"}, {"renderItem", "Lnet/minecraft/client/renderer/entity/RenderItem;"}, {"field_175621_X", "Lnet/minecraft/client/renderer/entity/RenderItem;"}}, &mappings->fid_mc_render_item)) return err;
    }
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"f", "Lbjf;"}, {"theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;"}, {"field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;"}}, {{"f", "Lbdb;"}, {"theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;"}, {"field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;"}}}, &mappings->fid_mc_the_world)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"Q", "Lbbr;"}, {"timer", "Lnet/minecraft/util/Timer;"}, {"field_71428_T", "Lnet/minecraft/util/Timer;"}}, {{"Y", "Lavl;"}, {"timer", "Lnet/minecraft/util/Timer;"}, {"field_71428_T", "Lnet/minecraft/util/Timer;"}}}, &mappings->fid_mc_timer)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"u", "Lbbj;"}, {"gameSettings", "Lnet/minecraft/client/settings/GameSettings;"}, {"field_71474_y", "Lnet/minecraft/client/settings/GameSettings;"}}, {{"t", "Lavh;"}, {"gameSettings", "Lnet/minecraft/client/settings/GameSettings;"}, {"field_71474_y", "Lnet/minecraft/client/settings/GameSettings;"}}}, &mappings->fid_mc_game_settings)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"x", "Z"}, {"inGameHasFocus", "Z"}, {"field_71415_G", "Z"}}, {{"w", "Z"}, {"inGameHasFocus", "Z"}, {"field_71415_G", "Z"}}}, &mappings->fid_mc_in_game_has_focus)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"t", "Lazu;"}, {"objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;"}, {"field_71476_x", "Lnet/minecraft/util/MovingObjectPosition;"}}, {{"s", "Lauh;"}, {"objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;"}, {"field_71476_x", "Lnet/minecraft/util/MovingObjectPosition;"}}}, &mappings->fid_mc_object_mouse_over)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"U", "I"}, {"leftClickCounter", "I"}, {"field_71429_W", "I"}}, {{"ag", "I"}, {"leftClickCounter", "I"}, {"field_71429_W", "I"}}}, &mappings->fid_mc_left_click_counter)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"ad", "I"}, {"rightClickDelayTimer", "I"}, {"field_71467_ac", "I"}}, {{"ap", "I"}, {"rightClickDelayTimer", "I"}, {"field_71467_ac", "I"}}}, &mappings->fid_mc_right_click_delay_timer)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"n", "Lbdw;"}, {"currentScreen", "Lnet/minecraft/client/gui/GuiScreen;"}, {"field_71462_r", "Lnet/minecraft/client/gui/GuiScreen;"}}, {{"m", "Laxu;"}, {"currentScreen", "Lnet/minecraft/client/gui/GuiScreen;"}, {"field_71462_r", "Lnet/minecraft/client/gui/GuiScreen;"}}}, &mappings->fid_mc_current_screen)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"r", "Lbbv;"}, {"ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;"}, {"field_71456_v", "Lnet/minecraft/client/gui/GuiIngame;"}}, {{"q", "Lavo;"}, {"ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;"}, {"field_71456_v", "Lnet/minecraft/client/gui/GuiIngame;"}}}, &mappings->fid_mc_ingame_gui)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"d", "I"}, {"displayWidth", "I"}, {"field_71443_c", "I"}}, {{"d", "I"}, {"displayWidth", "I"}, {"field_71443_c", "I"}}}, &mappings->fid_mc_display_width)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"e", "I"}, {"displayHeight", "I"}, {"field_71440_d", "I"}}, {{"e", "I"}, {"displayHeight", "I"}, {"field_71440_d", "I"}}}, &mappings->fid_mc_display_height)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"L", "Lbqf;"}, {"renderEngine", "Lnet/minecraft/client/renderer/texture/TextureManager;"}, {"field_71446_o", "Lnet/minecraft/client/renderer/texture/TextureManager;"}}, {{"R", "Lbmj;"}, {"renderEngine", "Lnet/minecraft/client/renderer/texture/TextureManager;"}, {"field_71446_o", "Lnet/minecraft/client/renderer/texture/TextureManager;"}}}, &mappings->fid_mc_render_engine)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"g", "Lbma;"}, {"renderGlobal", "Lnet/minecraft/client/renderer/RenderGlobal;"}, {"field_71438_f", "Lnet/minecraft/client/renderer/RenderGlobal;"}}, {{"g", "Lbfr;"}, {"renderGlobal", "Lnet/minecraft/client/renderer/RenderGlobal;"}, {"field_71438_f", "Lnet/minecraft/client/renderer/RenderGlobal;"}}}, &mappings->fid_mc_render_global)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"p", "Lblt;"}, {"entityRenderer", "Lnet/minecraft/client/renderer/EntityRenderer;"}, {"field_71460_t", "Lnet/minecraft/client/renderer/EntityRenderer;"}}, {{"o", "Lbfk;"}, {"entityRenderer", "Lnet/minecraft/client/renderer/EntityRenderer;"}, {"field_71460_t", "Lnet/minecraft/client/renderer/EntityRenderer;"}}}, &mappings->fid_mc_entity_renderer)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_minecraft, ver, flavor, {{{"c", "Lbje;"}, {"playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;"}, {"field_71442_b", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;"}}, {{"c", "Lbda;"}, {"playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;"}, {"field_71442_b", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;"}}}, &mappings->fid_mc_player_controller)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_minecraft, ver, flavor, {{{"v", "()Lbjb;"}, {"getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"}, {"func_147114_u", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"}}, {{"u", "()Lbcy;"}, {"getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"}, {"func_147114_u", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"}}}, &mappings->mid_mc_get_net_handler)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_minecraft, ver, flavor, {{{"a", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;"}, {"addScheduledTask", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;"}, {"func_152344_a", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;"}}, {{"a", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;"}, {"addScheduledTask", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;"}, {"func_152344_a", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;"}}}, &mappings->mid_mc_add_scheduled_task)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_minecraft, ver, flavor, {{{"al", "()V"}, {"clickMouse", "()V"}, {"func_147116_af", "()V"}}, {{"aw", "()V"}, {"clickMouse", "()V"}, {"func_147116_af", "()V"}}}, &mappings->mid_mc_click_mouse)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_minecraft, ver, flavor, {{{"am", "()V"}, {"rightClickMouse", "()V"}, {"func_147121_ag", "()V"}}, {{"ax", "()V"}, {"rightClickMouse", "()V"}, {"func_147121_ag", "()V"}}}, &mappings->mid_mc_right_click_mouse)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"sa", "net/minecraft/entity/Entity", "pk", "net/minecraft/entity/Entity"}, &mappings->cls_entity)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"s", "D"}, {"posX", "D"}, {"field_70165_t", "D"}}, {{"s", "D"}, {"posX", "D"}, {"field_70165_t", "D"}}}, &mappings->fid_entity_pos_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"t", "D"}, {"posY", "D"}, {"field_70163_u", "D"}}, {{"t", "D"}, {"posY", "D"}, {"field_70163_u", "D"}}}, &mappings->fid_entity_pos_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"u", "D"}, {"posZ", "D"}, {"field_70161_v", "D"}}, {{"u", "D"}, {"posZ", "D"}, {"field_70161_v", "D"}}}, &mappings->fid_entity_pos_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"p", "D"}, {"prevPosX", "D"}, {"field_70169_q", "D"}}, {{"p", "D"}, {"prevPosX", "D"}, {"field_70169_q", "D"}}}, &mappings->fid_entity_prev_pos_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"q", "D"}, {"prevPosY", "D"}, {"field_70167_r", "D"}}, {{"q", "D"}, {"prevPosY", "D"}, {"field_70167_r", "D"}}}, &mappings->fid_entity_prev_pos_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"r", "D"}, {"prevPosZ", "D"}, {"field_70166_s", "D"}}, {{"r", "D"}, {"prevPosZ", "D"}, {"field_70166_s", "D"}}}, &mappings->fid_entity_prev_pos_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"y", "F"}, {"rotationYaw", "F"}, {"field_70177_z", "F"}}, {{"y", "F"}, {"rotationYaw", "F"}, {"field_70177_z", "F"}}}, &mappings->fid_entity_rotation_yaw)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"z", "F"}, {"rotationPitch", "F"}, {"field_70125_A", "F"}}, {{"z", "F"}, {"rotationPitch", "F"}, {"field_70125_A", "F"}}}, &mappings->fid_entity_rotation_pitch)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"A", "F"}, {"prevRotationYaw", "F"}, {"field_70126_B", "F"}}, {{"A", "F"}, {"prevRotationYaw", "F"}, {"field_70126_B", "F"}}}, &mappings->fid_entity_prev_rotation_yaw)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"B", "F"}, {"prevRotationPitch", "F"}, {"field_70127_C", "F"}}, {{"B", "F"}, {"prevRotationPitch", "F"}, {"field_70127_C", "F"}}}, &mappings->fid_entity_prev_rotation_pitch)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"v", "D"}, {"motionX", "D"}, {"field_70159_w", "D"}}, {{"v", "D"}, {"motionX", "D"}, {"field_70159_w", "D"}}}, &mappings->fid_entity_motion_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"w", "D"}, {"motionY", "D"}, {"field_70181_x", "D"}}, {{"w", "D"}, {"motionY", "D"}, {"field_70181_x", "D"}}}, &mappings->fid_entity_motion_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"x", "D"}, {"motionZ", "D"}, {"field_70179_y", "D"}}, {{"x", "D"}, {"motionZ", "D"}, {"field_70179_y", "D"}}}, &mappings->fid_entity_motion_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"C", "Lazt;"}, {"boundingBox", "Lnet/minecraft/util/AxisAlignedBB;"}, {"field_70121_D", "Lnet/minecraft/util/AxisAlignedBB;"}}, {{"f", "Laug;"}, {"boundingBox", "Lnet/minecraft/util/AxisAlignedBB;"}, {"field_70121_D", "Lnet/minecraft/util/AxisAlignedBB;"}}}, &mappings->fid_entity_bounding_box)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"aa", "I"}, {"ticksExisted", "I"}, {"field_70173_aa", "I"}}, {{"W", "I"}, {"ticksExisted", "I"}, {"field_70173_aa", "I"}}}, &mappings->fid_entity_ticks_existed)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"c", "I"}, {"entityId", "I"}, {"field_145783_c", "I"}}, {{"c", "I"}, {"entityId", "I"}, {"field_145783_c", "I"}}}, &mappings->fid_entity_id)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"ar", "Ljava/util/UUID;"}, {"entityUniqueID", "Ljava/util/UUID;"}, {"field_96093_i", "Ljava/util/UUID;"}}, {{"aq", "Ljava/util/UUID;"}, {"entityUniqueID", "Ljava/util/UUID;"}, {"field_96093_i", "Ljava/util/UUID;"}}}, &mappings->fid_entity_unique_id)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"D", "Z"}, {"onGround", "Z"}, {"field_70122_E", "Z"}}, {{"C", "Z"}, {"onGround", "Z"}, {"field_70122_E", "Z"}}}, &mappings->fid_entity_on_ground)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"K", "Z"}, {"isDead", "Z"}, {"field_70128_L", "Z"}}, {{"I", "Z"}, {"isDead", "Z"}, {"field_70128_L", "Z"}}}, &mappings->fid_entity_is_dead)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"R", "F"}, {"fallDistance", "F"}, {"field_70143_R", "F"}}, {{"O", "F"}, {"fallDistance", "F"}, {"field_70143_R", "F"}}}, &mappings->fid_entity_fall_distance)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity, ver, flavor, {{{"m", "Lsa;"}, {"ridingEntity", "Lnet/minecraft/entity/Entity;"}, {"field_70154_o", "Lnet/minecraft/entity/Entity;"}}, {{"m", "Lpk;"}, {"ridingEntity", "Lnet/minecraft/entity/Entity;"}, {"field_70154_o", "Lnet/minecraft/entity/Entity;"}}}, &mappings->fid_entity_riding_entity)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"ao", "()Z"}, {"isSprinting", "()Z"}, {"func_70051_ag", "()Z"}}, {{"aw", "()Z"}, {"isSprinting", "()Z"}, {"func_70051_ag", "()Z"}}}, &mappings->mid_entity_is_sprinting)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"c", "(Z)V"}, {"setSprinting", "(Z)V"}, {"func_70031_b", "(Z)V"}}, {{"d", "(Z)V"}, {"setSprinting", "(Z)V"}, {"func_70031_b", "(Z)V"}}}, &mappings->mid_entity_set_sprinting)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"an", "()Z"}, {"isSneaking", "()Z"}, {"func_70093_af", "()Z"}}, {{"av", "()Z"}, {"isSneaking", "()Z"}, {"func_70093_af", "()Z"}}}, &mappings->mid_entity_is_sneaking)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"c_", "()Lfj;"}, {"getFormattedCommandSenderName", "()Lnet/minecraft/util/IChatComponent;"}, {"func_145748_c_", "()Lnet/minecraft/util/IChatComponent;"}}, {{"f_", "()Leu;"}, {"getDisplayName", "()Lnet/minecraft/util/IChatComponent;"}, {"func_145748_c_", "()Lnet/minecraft/util/IChatComponent;"}}}, &mappings->mid_entity_get_display_name)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"b_", "()Ljava/lang/String;"}, {"getCommandSenderName", "()Ljava/lang/String;"}, {"func_70005_c_", "()Ljava/lang/String;"}}, {{"e_", "()Ljava/lang/String;"}, {"getName", "()Ljava/lang/String;"}, {"func_70005_c_", "()Ljava/lang/String;"}}}, &mappings->mid_entity_get_name)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"c", "(FF)V"}, {"setAngles", "(FF)V"}, {"func_70082_c", "(FF)V"}}, {{"c", "(FF)V"}, {"setAngles", "(FF)V"}, {"func_70082_c", "(FF)V"}}}, &mappings->mid_entity_set_angles)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"a", "(FFF)V"}, {"moveFlying", "(FFF)V"}, {"func_70060_a", "(FFF)V"}}, {{"a", "(FFF)V"}, {"moveFlying", "(FFF)V"}, {"func_70060_a", "(FFF)V"}}}, &mappings->mid_entity_move_flying)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity, ver, flavor, {{{"R", "()Z"}, {"canBeCollidedWith", "()Z"}, {"func_70067_L", "()Z"}}, {{"ad", "()Z"}, {"canBeCollidedWith", "()Z"}, {"func_70067_L", "()Z"}}}, &mappings->mid_entity_can_be_collided_with)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ahb", "net/minecraft/world/World", "adm", "net/minecraft/world/World"}, &mappings->cls_world)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_world, ver, flavor, {{{"h", "Ljava/util/List;"}, {"playerEntities", "Ljava/util/List;"}, {"field_73010_i", "Ljava/util/List;"}}, {{"j", "Ljava/util/List;"}, {"playerEntities", "Ljava/util/List;"}, {"field_73010_i", "Ljava/util/List;"}}}, &mappings->fid_world_player_entities)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_world, ver, flavor, {{{"e", "Ljava/util/List;"}, {"loadedEntityList", "Ljava/util/List;"}, {"field_72996_f", "Ljava/util/List;"}}, {{"f", "Ljava/util/List;"}, {"loadedEntityList", "Ljava/util/List;"}, {"field_72996_f", "Ljava/util/List;"}}}, &mappings->fid_world_loaded_entity_list)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_world, ver, flavor, {{{"g", "Ljava/util/List;"}, {"loadedTileEntityList", "Ljava/util/List;"}, {"field_147482_g", "Ljava/util/List;"}}, {{"h", "Ljava/util/List;"}, {"loadedTileEntityList", "Ljava/util/List;"}, {"field_147482_g", "Ljava/util/List;"}}}, &mappings->fid_world_loaded_tile_entity_list)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_world, ver, flavor, {{{"v", "Lapu;"}, {"chunkProvider", "Lnet/minecraft/world/chunk/IChunkProvider;"}, {"field_73020_y", "Lnet/minecraft/world/chunk/IChunkProvider;"}}, {{"v", "Lamv;"}, {"chunkProvider", "Lnet/minecraft/world/chunk/IChunkProvider;"}, {"field_73020_y", "Lnet/minecraft/world/chunk/IChunkProvider;"}}}, &mappings->fid_world_chunk_provider)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_world, ver, flavor, {{{"a", "(I)Lsa;"}, {"getEntityByID", "(I)Lnet/minecraft/entity/Entity;"}, {"func_73045_a", "(I)Lnet/minecraft/entity/Entity;"}}, {{"a", "(I)Lpk;"}, {"getEntityByID", "(I)Lnet/minecraft/entity/Entity;"}, {"func_73045_a", "(I)Lnet/minecraft/entity/Entity;"}}}, &mappings->mid_world_get_entity_by_id)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_world, ver, flavor, {{{"a", "(Lazw;Lazw;ZZZ)Lazu;"}, {"rayTraceBlocks", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;ZZZ)Lnet/minecraft/util/MovingObjectPosition;"}, {"func_147447_a", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;ZZZ)Lnet/minecraft/util/MovingObjectPosition;"}}, {{"a", "(Laui;Laui;ZZZ)Lauh;"}, {"rayTraceBlocks", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;ZZZ)Lnet/minecraft/util/MovingObjectPosition;"}, {"func_147447_a", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;ZZZ)Lnet/minecraft/util/MovingObjectPosition;"}}}, &mappings->mid_world_ray_trace_blocks)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_world, ver, flavor, {{{"h", "()V"}, {"updateEntities", "()V"}, {"func_72939_s", "()V"}}, {{"i", "()V"}, {"updateEntities", "()V"}, {"func_72939_s", "()V"}}}, &mappings->mid_world_update_entities)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_world, ver, flavor, {{{"e", "(II)Lapx;"}, {"getChunkFromChunkCoords", "(II)Lnet/minecraft/world/chunk/Chunk;"}, {"func_72964_e", "(II)Lnet/minecraft/world/chunk/Chunk;"}}, {{"a", "(II)Lamy;"}, {"getChunkFromChunkCoords", "(II)Lnet/minecraft/world/chunk/Chunk;"}, {"func_72964_e", "(II)Lnet/minecraft/world/chunk/Chunk;"}}}, &mappings->mid_world_get_chunk_from_chunk_coords)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_world, ver, flavor, {{{"c", "(IIIIII)V"}, {"markBlockRangeForRenderUpdate", "(IIIIII)V"}, {"func_147458_c", "(IIIIII)V"}}, {{"b", "(IIIIII)V"}, {"markBlockRangeForRenderUpdate", "(IIIIII)V"}, {"func_147458_c", "(IIIIII)V"}}}, &mappings->mid_world_mark_block_range_for_render_update)) return err;
    if (ver == 0) {
        if (int err = resolve_method_1v(env, mappings->cls_world, flavor, {{"a", "(III)Laji;"}, {"getBlock", "(III)Lnet/minecraft/block/Block;"}, {"func_147439_a", "(III)Lnet/minecraft/block/Block;"}}, &mappings->mid_world_get_block_1_7)) return err;
        if (int err = resolve_method_1v(env, mappings->cls_world, flavor, {{"d", "(IIILaji;II)Z"}, {"setBlock", "(IIILnet/minecraft/block/Block;II)Z"}, {"func_147465_d", "(IIILnet/minecraft/block/Block;II)Z"}}, &mappings->mid_world_set_block_1_7)) return err;
    } else {
        if (int err = resolve_method_1v(env, mappings->cls_world, flavor, {{"p", "(Lcj;)Lalz;"}, {"getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;"}, {"func_180495_p", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;"}}, &mappings->mid_world_get_block_state_1_8)) return err;
        if (int err = resolve_method_1v(env, mappings->cls_world, flavor, {{"a", "(Lcj;Lalz;I)Z"}, {"setBlockState", "(Lnet/minecraft/util/BlockPos;Lnet/minecraft/block/state/IBlockState;I)Z"}, {"func_180501_a", "(Lnet/minecraft/util/BlockPos;Lnet/minecraft/block/state/IBlockState;I)Z"}}, &mappings->mid_world_set_block_state_1_8)) return err;
    }
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bbr", "net/minecraft/util/Timer", "avl", "net/minecraft/util/Timer"}, &mappings->cls_timer)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_timer, ver, flavor, {{{"c", "F"}, {"renderPartialTicks", "F"}, {"field_74281_c", "F"}}, {{"c", "F"}, {"renderPartialTicks", "F"}, {"field_74281_c", "F"}}}, &mappings->fid_timer_render_partial_ticks)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_timer, ver, flavor, {{{"d", "F"}, {"timerSpeed", "F"}, {"field_74278_d", "F"}}, {{"d", "F"}, {"timerSpeed", "F"}, {"field_74278_d", "F"}}}, &mappings->fid_timer_speed)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_timer, ver, flavor, {{{"e", "F"}, {"elapsedPartialTicks", "F"}, {"field_74279_e", "F"}}, {{"e", "F"}, {"elapsedPartialTicks", "F"}, {"field_74279_e", "F"}}}, &mappings->fid_timer_elapsed_partial_ticks)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_timer, ver, flavor, {{{"b", "I"}, {"elapsedTicks", "I"}, {"field_74280_b", "I"}}, {{"b", "I"}, {"elapsedTicks", "I"}, {"field_74280_b", "I"}}}, &mappings->fid_timer_elapsed_ticks)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bbj", "net/minecraft/client/settings/GameSettings", "avh", "net/minecraft/client/settings/GameSettings"}, &mappings->cls_game_settings)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_game_settings, ver, flavor, {{{"a", "F"}, {"mouseSensitivity", "F"}, {"field_74341_c", "F"}}, {{"a", "F"}, {"mouseSensitivity", "F"}, {"field_74341_c", "F"}}}, &mappings->fid_settings_mouse_sensitivity)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_game_settings, ver, flavor, {{{"as", "[Lbal;"}, {"keyBindings", "[Lnet/minecraft/client/settings/KeyBinding;"}, {"field_74324_K", "[Lnet/minecraft/client/settings/KeyBinding;"}}, {{"ax", "[Lavb;"}, {"keyBindings", "[Lnet/minecraft/client/settings/KeyBinding;"}, {"field_74324_K", "[Lnet/minecraft/client/settings/KeyBinding;"}}}, &mappings->fid_settings_key_bindings)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_game_settings, ver, flavor, {{{"aw", "I"}, {"thirdPersonView", "I"}, {"field_74320_O", "I"}}, {{"aB", "I"}, {"thirdPersonView", "I"}, {"field_74320_O", "I"}}}, &mappings->fid_settings_third_person_view)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"azu", "net/minecraft/util/MovingObjectPosition", "auh", "net/minecraft/util/MovingObjectPosition"}, &mappings->cls_moving_object_position)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_moving_object_position, ver, flavor, {{{"a", "Lazv;"}, {"typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;"}, {"field_72313_a", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;"}}, {{"a", "Lauh$a;"}, {"typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;"}, {"field_72313_a", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;"}}}, &mappings->fid_mop_type_of_hit, mappings->lunar_underscore_inner ? "Lnet/minecraft/util/MovingObjectPosition_MovingObjectType;" : nullptr)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_moving_object_position, ver, flavor, {{{"f", "Lazw;"}, {"hitVec", "Lnet/minecraft/util/Vec3;"}, {"field_72307_f", "Lnet/minecraft/util/Vec3;"}}, {{"c", "Laui;"}, {"hitVec", "Lnet/minecraft/util/Vec3;"}, {"field_72307_f", "Lnet/minecraft/util/Vec3;"}}}, &mappings->fid_mop_hit_vec)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_moving_object_position, ver, flavor, {{{"g", "Lsa;"}, {"entityHit", "Lnet/minecraft/entity/Entity;"}, {"field_72308_g", "Lnet/minecraft/entity/Entity;"}}, {{"d", "Lpk;"}, {"entityHit", "Lnet/minecraft/entity/Entity;"}, {"field_72308_g", "Lnet/minecraft/entity/Entity;"}}}, &mappings->fid_mop_entity_hit)) return err;
    if (ver == 0) {
        if (int err = resolve_field_1v(env, mappings->cls_moving_object_position, flavor, {{"e", "I"}, {"sideHit", "I"}, {"field_72310_e", "I"}}, &mappings->fid_mop_side_hit_1_7)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_moving_object_position, flavor, {{"b", "I"}, {"blockX", "I"}, {"field_72311_b", "I"}}, &mappings->fid_mop_block_x_1_7)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_moving_object_position, flavor, {{"c", "I"}, {"blockY", "I"}, {"field_72312_c", "I"}}, &mappings->fid_mop_block_y_1_7)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_moving_object_position, flavor, {{"d", "I"}, {"blockZ", "I"}, {"field_72309_d", "I"}}, &mappings->fid_mop_block_z_1_7)) return err;
    } else {
        if (int err = resolve_field_1v(env, mappings->cls_moving_object_position, flavor, {{"b", "Lcq;"}, {"sideHit", "Lnet/minecraft/util/EnumFacing;"}, {"field_178784_b", "Lnet/minecraft/util/EnumFacing;"}}, &mappings->fid_mop_side_hit_1_8)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_moving_object_position, flavor, {{"e", "Lcj;"}, {"blockPos", "Lnet/minecraft/util/BlockPos;"}, {"field_178783_e", "Lnet/minecraft/util/BlockPos;"}}, &mappings->fid_mop_block_pos_1_8)) return err;
    }
    if (int err = resolve_method_2v(env, mappings->cls_moving_object_position, ver, flavor, {{{"<init>", "(Lsa;Lazw;)V"}, {"<init>", "(Lnet/minecraft/entity/Entity;Lnet/minecraft/util/Vec3;)V"}, {"<init>", "(Lnet/minecraft/entity/Entity;Lnet/minecraft/util/Vec3;)V"}}, {{"<init>", "(Lpk;Laui;)V"}, {"<init>", "(Lnet/minecraft/entity/Entity;Lnet/minecraft/util/Vec3;)V"}, {"<init>", "(Lnet/minecraft/entity/Entity;Lnet/minecraft/util/Vec3;)V"}}}, &mappings->mid_mop_init_entity_vec)) return err;
    if (ver != 0) {
        if (int err = resolve_class_1v(env, loader, is_deobf, "alz", "net/minecraft/block/state/IBlockState", &mappings->cls_iblock_state)) return err;
        if (int err = resolve_method_1v(env, mappings->cls_iblock_state, flavor, {{"c", "()Lafh;"}, {"getBlock", "()Lnet/minecraft/block/Block;"}, {"func_177230_c", "()Lnet/minecraft/block/Block;"}}, &mappings->mid_iblock_state_get_block)) return err;
    }
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"aji", "net/minecraft/block/Block", "afh", "net/minecraft/block/Block"}, &mappings->cls_block)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_block, ver, flavor, {{{"b", "(Laji;)I"}, {"getIdFromBlock", "(Lnet/minecraft/block/Block;)I"}, {"func_149682_b", "(Lnet/minecraft/block/Block;)I"}}, {{"a", "(Lafh;)I"}, {"getIdFromBlock", "(Lnet/minecraft/block/Block;)I"}, {"func_149682_b", "(Lnet/minecraft/block/Block;)I"}}}, &mappings->smid_block_get_id_from_block)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_block, ver, flavor, {{{"a", "(Ladb;)Laji;"}, {"getBlockFromItem", "(Lnet/minecraft/item/Item;)Lnet/minecraft/block/Block;"}, {"func_149634_a", "(Lnet/minecraft/item/Item;)Lnet/minecraft/block/Block;"}}, {{"a", "(Lzw;)Lafh;"}, {"getBlockFromItem", "(Lnet/minecraft/item/Item;)Lnet/minecraft/block/Block;"}, {"func_149634_a", "(Lnet/minecraft/item/Item;)Lnet/minecraft/block/Block;"}}}, &mappings->smid_block_get_block_from_item)) return err;
    if (ver != 0) {
        if (int err = resolve_class_1v(env, loader, is_deobf, "cj", "net/minecraft/util/BlockPos", &mappings->cls_block_pos)) return err;
        if (int err = resolve_method_1v(env, mappings->cls_block_pos, flavor, {{"<init>", "(III)V"}, {"<init>", "(III)V"}, {"<init>", "(III)V"}}, &mappings->mid_block_pos_init)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_block_pos, flavor, {{"a", "I"}, {"x", "I"}, {"field_177962_a", "I"}}, &mappings->fid_block_pos_x)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_block_pos, flavor, {{"c", "I"}, {"y", "I"}, {"field_177960_b", "I"}}, &mappings->fid_block_pos_y)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_block_pos, flavor, {{"d", "I"}, {"z", "I"}, {"field_177961_c", "I"}}, &mappings->fid_block_pos_z)) return err;
    }
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bal", "net/minecraft/client/settings/KeyBinding", "avb", "net/minecraft/client/settings/KeyBinding"}, &mappings->cls_key_binding)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_key_binding, ver, flavor, {{{"h", "Z"}, {"pressed", "Z"}, {"field_74513_e", "Z"}}, {{"h", "Z"}, {"pressed", "Z"}, {"field_74513_e", "Z"}}}, &mappings->fid_key_binding_pressed)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_key_binding, ver, flavor, {{{"i", "I"}, {"pressTime", "I"}, {"field_151474_i", "I"}}, {{"i", "I"}, {"pressTime", "I"}, {"field_151474_i", "I"}}}, &mappings->fid_key_binding_press_time)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_key_binding, ver, flavor, {{{"g", "I"}, {"keyCode", "I"}, {"field_74512_d", "I"}}, {{"g", "I"}, {"keyCode", "I"}, {"field_74512_d", "I"}}}, &mappings->fid_key_binding_key_code)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"azt", "net/minecraft/util/AxisAlignedBB", "aug", "net/minecraft/util/AxisAlignedBB"}, &mappings->cls_axis_aligned_bb)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"a", "D"}, {"minX", "D"}, {"field_72340_a", "D"}}, {{"a", "D"}, {"minX", "D"}, {"field_72340_a", "D"}}}, &mappings->fid_aabb_min_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"b", "D"}, {"minY", "D"}, {"field_72338_b", "D"}}, {{"b", "D"}, {"minY", "D"}, {"field_72338_b", "D"}}}, &mappings->fid_aabb_min_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"c", "D"}, {"minZ", "D"}, {"field_72339_c", "D"}}, {{"c", "D"}, {"minZ", "D"}, {"field_72339_c", "D"}}}, &mappings->fid_aabb_min_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"d", "D"}, {"maxX", "D"}, {"field_72336_d", "D"}}, {{"d", "D"}, {"maxX", "D"}, {"field_72336_d", "D"}}}, &mappings->fid_aabb_max_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"e", "D"}, {"maxY", "D"}, {"field_72337_e", "D"}}, {{"e", "D"}, {"maxY", "D"}, {"field_72337_e", "D"}}}, &mappings->fid_aabb_max_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"f", "D"}, {"maxZ", "D"}, {"field_72334_f", "D"}}, {{"f", "D"}, {"maxZ", "D"}, {"field_72334_f", "D"}}}, &mappings->fid_aabb_max_z)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_axis_aligned_bb, ver, flavor, {{{"a", "(Lazw;Lazw;)Lazu;"}, {"calculateIntercept", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;"}, {"func_72327_a", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;"}}, {{"a", "(Laui;Laui;)Lauh;"}, {"calculateIntercept", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;"}, {"func_72327_a", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;"}}}, &mappings->mid_aabb_calculate_intercept)) return err;
    if (int err = resolve_method_const(env, mappings->cls_axis_aligned_bb, "<init>", "(DDDDDD)V", &mappings->mid_aabb_init)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bex", "net/minecraft/client/gui/inventory/GuiContainer", "ayl", "net/minecraft/client/gui/inventory/GuiContainer"}, &mappings->cls_gui_container)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_gui_container, ver, flavor, {{{"u", "Laay;"}, {"theSlot", "Lnet/minecraft/inventory/Slot;"}, {"field_147006_u", "Lnet/minecraft/inventory/Slot;"}}, {{"u", "Lyg;"}, {"theSlot", "Lnet/minecraft/inventory/Slot;"}, {"field_147006_u", "Lnet/minecraft/inventory/Slot;"}}}, &mappings->fid_gui_container_the_slot)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_gui_container, ver, flavor, {{{"J", "J"}, {"lastClickTime", "J"}, {"field_146997_J", "J"}}, {{"J", "J"}, {"lastClickTime", "J"}, {"field_146997_J", "J"}}}, &mappings->fid_gui_container_last_click_time)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_gui_container, ver, flavor, {{{"a", "(Laay;III)V"}, {"handleMouseClick", "(Lnet/minecraft/inventory/Slot;III)V"}, {"func_146984_a", "(Lnet/minecraft/inventory/Slot;III)V"}}, {{"a", "(Lyg;III)V"}, {"handleMouseClick", "(Lnet/minecraft/inventory/Slot;III)V"}, {"func_146984_a", "(Lnet/minecraft/inventory/Slot;III)V"}}}, &mappings->mid_gui_container_handle_mouse_click)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"sv", "net/minecraft/entity/EntityLivingBase", "pr", "net/minecraft/entity/EntityLivingBase"}, &mappings->cls_entity_living_base)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"be", "F"}, {"moveForward", "F"}, {"field_70701_bs", "F"}}, {{"ba", "F"}, {"moveForward", "F"}, {"field_70701_bs", "F"}}}, &mappings->fid_living_move_forward)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"bd", "F"}, {"moveStrafing", "F"}, {"field_70702_br", "F"}}, {{"aZ", "F"}, {"moveStrafing", "F"}, {"field_70702_br", "F"}}}, &mappings->fid_living_move_strafing)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"ax", "I"}, {"hurtTime", "I"}, {"field_70737_aN", "I"}}, {{"au", "I"}, {"hurtTime", "I"}, {"field_70737_aN", "I"}}}, &mappings->fid_living_hurt_time)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"ay", "I"}, {"maxHurtTime", "I"}, {"field_70738_aO", "I"}}, {{"av", "I"}, {"maxHurtTime", "I"}, {"field_70738_aO", "I"}}}, &mappings->fid_living_max_hurt_time)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"bo", "I"}, {"lastAttackerTime", "I"}, {"field_142016_bo", "I"}}, {{"bl", "I"}, {"lastAttackerTime", "I"}, {"field_142016_bo", "I"}}}, &mappings->fid_living_last_attacker_time)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"bp", "F"}, {"landMovementFactor", "F"}, {"field_70746_aG", "F"}}, {{"bm", "F"}, {"landMovementFactor", "F"}, {"field_70746_aG", "F"}}}, &mappings->fid_living_land_movement_factor)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"bq", "I"}, {"jumpTicks", "I"}, {"field_70773_bE", "I"}}, {{"bn", "I"}, {"jumpTicks", "I"}, {"field_70773_bE", "I"}}}, &mappings->fid_living_jump_ticks)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"a", "(DF)Lazu;"}, {"rayTrace", "(DF)Lnet/minecraft/util/MovingObjectPosition;"}, {"func_70614_a", "(DF)Lnet/minecraft/util/MovingObjectPosition;"}}, {{"a", "(DF)Lauh;"}, {"rayTrace", "(DF)Lnet/minecraft/util/MovingObjectPosition;"}, {"func_174822_a", "(DF)Lnet/minecraft/util/MovingObjectPosition;"}}}, &mappings->mid_living_ray_trace)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"l", "(F)Lazw;"}, {"getPosition", "(F)Lnet/minecraft/util/Vec3;"}, {"func_70666_h", "(F)Lnet/minecraft/util/Vec3;"}}, {{"e", "(F)Laui;"}, {"getPositionEyes", "(F)Lnet/minecraft/util/Vec3;"}, {"func_174824_e", "(F)Lnet/minecraft/util/Vec3;"}}}, &mappings->mid_living_get_position_eyes)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"aS", "()F"}, {"getHealth", "()F"}, {"func_110143_aJ", "()F"}}, {{"bn", "()F"}, {"getHealth", "()F"}, {"func_110143_aJ", "()F"}}}, &mappings->mid_living_get_health)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"ba", "()V"}, {"swingItem", "()V"}, {"func_71038_i", "()V"}}, {{"bw", "()V"}, {"swingItem", "()V"}, {"func_71038_i", "()V"}}}, &mappings->mid_living_swing_item)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"bt", "()Lbae;"}, {"getTeam", "()Lnet/minecraft/scoreboard/Team;"}, {"func_96124_cp", "()Lnet/minecraft/scoreboard/Team;"}}, {{"bO", "()Lauq;"}, {"getTeam", "()Lnet/minecraft/scoreboard/Team;"}, {"func_96124_cp", "()Lnet/minecraft/scoreboard/Team;"}}}, &mappings->mid_living_get_team)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"aY", "()F"}, {"getMaxHealth", "()F"}, {"func_110138_aP", "()F"}}, {{"bu", "()F"}, {"getMaxHealth", "()F"}, {"func_110138_aP", "()F"}}}, &mappings->mid_living_get_max_health)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"b", "(Lrv;)Lrw;"}, {"getActivePotionEffect", "(Lnet/minecraft/potion/Potion;)Lnet/minecraft/potion/PotionEffect;"}, {"func_70660_b", "(Lnet/minecraft/potion/Potion;)Lnet/minecraft/potion/PotionEffect;"}}, {{"b", "(Lpe;)Lpf;"}, {"getActivePotionEffect", "(Lnet/minecraft/potion/Potion;)Lnet/minecraft/potion/PotionEffect;"}, {"func_70660_b", "(Lnet/minecraft/potion/Potion;)Lnet/minecraft/potion/PotionEffect;"}}}, &mappings->mid_living_get_active_potion_effect)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_living_base, ver, flavor, {{{"bj", "()V"}, {"jump", "()V"}, {"func_70664_aZ", "()V"}}, {{"bF", "()V"}, {"jump", "()V"}, {"func_70664_aZ", "()V"}}}, &mappings->mid_living_jump)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"yz", "net/minecraft/entity/player/EntityPlayer", "wn", "net/minecraft/entity/player/EntityPlayer"}, &mappings->cls_entity_player)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player, ver, flavor, {{{"bm", "Lyx;"}, {"inventory", "Lnet/minecraft/entity/player/InventoryPlayer;"}, {"field_71071_by", "Lnet/minecraft/entity/player/InventoryPlayer;"}}, {{"bi", "Lwm;"}, {"inventory", "Lnet/minecraft/entity/player/InventoryPlayer;"}, {"field_71071_by", "Lnet/minecraft/entity/player/InventoryPlayer;"}}}, &mappings->fid_player_inventory)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player, ver, flavor, {{{"g", "I"}, {"itemInUseCount", "I"}, {"field_71072_f", "I"}}, {{"h", "I"}, {"itemInUseCount", "I"}, {"field_71072_f", "I"}}}, &mappings->fid_player_item_in_use_count)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player, ver, flavor, {{{"bn", "Lzs;"}, {"inventoryContainer", "Lnet/minecraft/inventory/Container;"}, {"field_71069_bz", "Lnet/minecraft/inventory/Container;"}}, {{"bj", "Lxi;"}, {"inventoryContainer", "Lnet/minecraft/inventory/Container;"}, {"field_71069_bz", "Lnet/minecraft/inventory/Container;"}}}, &mappings->fid_player_inventory_container)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player, ver, flavor, {{{"bA", "Z"}, {"sleeping", "Z"}, {"field_71083_bS", "Z"}}, {{"bw", "Z"}, {"sleeping", "Z"}, {"field_71083_bS", "Z"}}}, &mappings->fid_player_sleeping)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player, ver, flavor, {{{"bI", "F"}, {"speedOnGround", "F"}, {"field_71108_cd", "F"}}, {{"bE", "F"}, {"speedOnGround", "F"}, {"field_71108_cd", "F"}}}, &mappings->fid_player_speed_on_ground)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_player, ver, flavor, {{{"d", "(Lyz;)Z"}, {"isInvisibleToPlayer", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"}, {"func_98034_c", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"}}, {{"f", "(Lwn;)Z"}, {"isInvisibleToPlayer", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"}, {"func_98034_c", "(Lnet/minecraft/entity/player/EntityPlayer;)Z"}}}, &mappings->mid_player_is_invisible_to_player)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_player, ver, flavor, {{{"by", "()Z"}, {"isUsingItem", "()Z"}, {"func_71039_bw", "()Z"}}, {{"bS", "()Z"}, {"isUsingItem", "()Z"}, {"func_71039_bw", "()Z"}}}, &mappings->mid_player_is_using_item)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_player, ver, flavor, {{{"bs", "()F"}, {"getAbsorptionAmount", "()F"}, {"func_110139_bj", "()F"}}, {{"bN", "()F"}, {"getAbsorptionAmount", "()F"}, {"func_110139_bj", "()F"}}}, &mappings->mid_player_get_absorption_amount)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_player, ver, flavor, {{{"q", "(I)Ladd;"}, {"getEquipmentInSlot", "(I)Lnet/minecraft/item/ItemStack;"}, {"func_71124_b", "(I)Lnet/minecraft/item/ItemStack;"}}, {{"p", "(I)Lzx;"}, {"getEquipmentInSlot", "(I)Lnet/minecraft/item/ItemStack;"}, {"func_71124_b", "(I)Lnet/minecraft/item/ItemStack;"}}}, &mappings->mid_player_get_equipment_in_slot)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"yx", "net/minecraft/entity/player/InventoryPlayer", "wm", "net/minecraft/entity/player/InventoryPlayer"}, &mappings->cls_inventory_player)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_inventory_player, ver, flavor, {{{"a", "[Ladd;"}, {"mainInventory", "[Lnet/minecraft/item/ItemStack;"}, {"field_70462_a", "[Lnet/minecraft/item/ItemStack;"}}, {{"a", "[Lzx;"}, {"mainInventory", "[Lnet/minecraft/item/ItemStack;"}, {"field_70462_a", "[Lnet/minecraft/item/ItemStack;"}}}, &mappings->fid_inv_player_main_inventory)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_inventory_player, ver, flavor, {{{"c", "I"}, {"currentItem", "I"}, {"field_70461_c", "I"}}, {{"c", "I"}, {"currentItem", "I"}, {"field_70461_c", "I"}}}, &mappings->fid_inv_player_current_item)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_inventory_player, ver, flavor, {{{"g", "Ladd;"}, {"itemStack", "Lnet/minecraft/item/ItemStack;"}, {"field_70457_g", "Lnet/minecraft/item/ItemStack;"}}, {{"f", "Lzx;"}, {"itemStack", "Lnet/minecraft/item/ItemStack;"}, {"field_70457_g", "Lnet/minecraft/item/ItemStack;"}}}, &mappings->fid_inv_player_dragged_item_stack)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"add", "net/minecraft/item/ItemStack", "zx", "net/minecraft/item/ItemStack"}, &mappings->cls_item_stack)) return err;
    std::snprintf(s_itemstack_block_ctor_desc, sizeof(s_itemstack_block_ctor_desc), "(L%s;)V", is_deobf ? "net/minecraft/item/Item" : (ver == 0 ? "adb" : "zw"));
    if (int err = resolve_method_const(env, mappings->cls_item_stack, "<init>", s_itemstack_block_ctor_desc, &mappings->mid_item_stack_init_block)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_item_stack, ver, flavor, {{{"b", "()Ladb;"}, {"getItem", "()Lnet/minecraft/item/Item;"}, {"func_77973_b", "()Lnet/minecraft/item/Item;"}}, {{"b", "()Lzw;"}, {"getItem", "()Lnet/minecraft/item/Item;"}, {"func_77973_b", "()Lnet/minecraft/item/Item;"}}}, &mappings->mid_item_stack_get_item)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_item_stack, ver, flavor, {{{"a", "(Laji;)F"}, {"getStrVsBlock", "(Lnet/minecraft/block/Block;)F"}, {"func_150997_a", "(Lnet/minecraft/block/Block;)F"}}, {{"a", "(Lafh;)F"}, {"getStrVsBlock", "(Lnet/minecraft/block/Block;)F"}, {"func_150997_a", "(Lnet/minecraft/block/Block;)F"}}}, &mappings->mid_item_stack_get_str_vs_block)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_item_stack, ver, flavor, {{{"r", "()Ldq;"}, {"getEnchantmentTagList", "()Lnet/minecraft/nbt/NBTTagList;"}, {"func_77986_q", "()Lnet/minecraft/nbt/NBTTagList;"}}, {{"p", "()Ldu;"}, {"getEnchantmentTagList", "()Lnet/minecraft/nbt/NBTTagList;"}, {"func_77986_q", "()Lnet/minecraft/nbt/NBTTagList;"}}}, &mappings->mid_item_stack_get_enchantment_tag_list)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_item_stack, ver, flavor, {{{"e", "()I"}, {"getMaxStackSize", "()I"}, {"func_77976_d", "()I"}}, {{"c", "()I"}, {"getMaxStackSize", "()I"}, {"func_77976_d", "()I"}}}, &mappings->mid_item_stack_get_max_stack_size)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_item_stack, ver, flavor, {{{"a", "(Ladd;Ladd;)Z"}, {"areItemStackTagsEqual", "(Lnet/minecraft/item/ItemStack;Lnet/minecraft/item/ItemStack;)Z"}, {"func_77970_a", "(Lnet/minecraft/item/ItemStack;Lnet/minecraft/item/ItemStack;)Z"}}, {{"a", "(Lzx;Lzx;)Z"}, {"areItemStackTagsEqual", "(Lnet/minecraft/item/ItemStack;Lnet/minecraft/item/ItemStack;)Z"}, {"func_77970_a", "(Lnet/minecraft/item/ItemStack;Lnet/minecraft/item/ItemStack;)Z"}}}, &mappings->smid_item_stack_are_tags_equal)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_item_stack, ver, flavor, {{{"u", "()Z"}, {"hasDisplayName", "()Z"}, {"func_82837_s", "()Z"}}, {{"s", "()Z"}, {"hasDisplayName", "()Z"}, {"func_82837_s", "()Z"}}}, &mappings->mid_item_stack_has_display_name)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_item_stack, ver, flavor, {{{"b", "I"}, {"stackSize", "I"}, {"field_77994_a", "I"}}, {{"b", "I"}, {"stackSize", "I"}, {"field_77994_a", "I"}}}, &mappings->fid_item_stack_size)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_item_stack, ver, flavor, {{{"f", "I"}, {"metadata", "I"}, {"field_77991_e", "I"}}, {{"f", "I"}, {"itemDamage", "I"}, {"field_77991_e", "I"}}}, &mappings->fid_item_stack_item_damage)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"adb", "net/minecraft/item/Item", "zw", "net/minecraft/item/Item"}, &mappings->cls_item)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_item, ver, flavor, {{{"b", "(Ladb;)I"}, {"getIdFromItem", "(Lnet/minecraft/item/Item;)I"}, {"func_150891_b", "(Lnet/minecraft/item/Item;)I"}}, {{"b", "(Lzw;)I"}, {"getIdFromItem", "(Lnet/minecraft/item/Item;)I"}, {"func_150891_b", "(Lnet/minecraft/item/Item;)I"}}}, &mappings->smid_item_get_id_from_item)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_item, ver, flavor, {{{"d", "(I)Ladb;"}, {"getItemById", "(I)Lnet/minecraft/item/Item;"}, {"func_150899_d", "(I)Lnet/minecraft/item/Item;"}}, {{"b", "(I)Lzw;"}, {"getItemById", "(I)Lnet/minecraft/item/Item;"}, {"func_150899_d", "(I)Lnet/minecraft/item/Item;"}}}, &mappings->smid_item_get_item_by_id)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"azw", "net/minecraft/util/Vec3", "aui", "net/minecraft/util/Vec3"}, &mappings->cls_vec3)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_vec3, ver, flavor, {{{"a", "D"}, {"xCoord", "D"}, {"field_72450_a", "D"}}, {{"a", "D"}, {"xCoord", "D"}, {"field_72450_a", "D"}}}, &mappings->fid_vec3_x_coord)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_vec3, ver, flavor, {{{"b", "D"}, {"yCoord", "D"}, {"field_72448_b", "D"}}, {{"b", "D"}, {"yCoord", "D"}, {"field_72448_b", "D"}}}, &mappings->fid_vec3_y_coord)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_vec3, ver, flavor, {{{"c", "D"}, {"zCoord", "D"}, {"field_72449_c", "D"}}, {{"c", "D"}, {"zCoord", "D"}, {"field_72449_c", "D"}}}, &mappings->fid_vec3_z_coord)) return err;
    if (int err = resolve_method_const(env, mappings->cls_vec3, "<init>", "(DDD)V", &mappings->mid_vec3_init)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"azy", "net/minecraft/scoreboard/ScorePlayerTeam", "aul", "net/minecraft/scoreboard/ScorePlayerTeam"}, &mappings->cls_score_player_team)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_score_player_team, ver, flavor, {{{"e", "Ljava/lang/String;"}, {"namePrefixSPT", "Ljava/lang/String;"}, {"field_96674_e", "Ljava/lang/String;"}}, {{"e", "Ljava/lang/String;"}, {"namePrefixSPT", "Ljava/lang/String;"}, {"field_96674_e", "Ljava/lang/String;"}}}, &mappings->fid_score_player_team_name_prefix)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bjb", "net/minecraft/client/network/NetHandlerPlayClient", "bcy", "net/minecraft/client/network/NetHandlerPlayClient"}, &mappings->cls_net_handler_play_client)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_net_handler_play_client, ver, flavor, {{{"i", "Ljava/util/Map;"}, {"playerInfoMap", "Ljava/util/Map;"}, {"field_147310_i", "Ljava/util/Map;"}}, {{"i", "Ljava/util/Map;"}, {"playerInfoMap", "Ljava/util/Map;"}, {"field_147310_i", "Ljava/util/Map;"}}}, &mappings->fid_net_handler_player_info_map)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_net_handler_play_client, ver, flavor, {{{"e", "Lej;"}, {"netManager", "Lnet/minecraft/network/NetworkManager;"}, {"field_147302_e", "Lnet/minecraft/network/NetworkManager;"}}, {{"c", "Lek;"}, {"netManager", "Lnet/minecraft/network/NetworkManager;"}, {"field_147302_e", "Lnet/minecraft/network/NetworkManager;"}}}, &mappings->fid_net_handler_net_manager)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"fj", "net/minecraft/util/IChatComponent", "eu", "net/minecraft/util/IChatComponent"}, &mappings->cls_ichat_component)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_ichat_component, ver, flavor, {{{"d", "()Ljava/lang/String;"}, {"getFormattedText", "()Ljava/lang/String;"}, {"func_150254_d", "()Ljava/lang/String;"}}, {{"d", "()Ljava/lang/String;"}, {"getFormattedText", "()Ljava/lang/String;"}, {"func_150254_d", "()Ljava/lang/String;"}}}, &mappings->mid_ichat_component_get_formatted_text)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"fk", "net/minecraft/util/IChatComponent$Serializer", "eu$a", "net/minecraft/util/IChatComponent$Serializer"}, &mappings->cls_ichat_component_serializer)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_ichat_component_serializer, ver, flavor, {{{"a", "(Ljava/lang/String;)Lfj;"}, {"jsonToComponent", "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;"}, {"func_150699_a", "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;"}}, {{"a", "(Ljava/lang/String;)Leu;"}, {"jsonToComponent", "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;"}, {"func_150699_a", "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;"}}}, &mappings->smid_ichat_serializer_json_to_component)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"aay", "net/minecraft/inventory/Slot", "yg", "net/minecraft/inventory/Slot"}, &mappings->cls_slot)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_slot, ver, flavor, {{{"g", "I"}, {"slotNumber", "I"}, {"field_75222_d", "I"}}, {{"e", "I"}, {"slotNumber", "I"}, {"field_75222_d", "I"}}}, &mappings->fid_slot_number)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_slot, ver, flavor, {{{"d", "()Ladd;"}, {"getStack", "()Lnet/minecraft/item/ItemStack;"}, {"func_75211_c", "()Lnet/minecraft/item/ItemStack;"}}, {{"d", "()Lzx;"}, {"getStack", "()Lnet/minecraft/item/ItemStack;"}, {"func_75211_c", "()Lnet/minecraft/item/ItemStack;"}}}, &mappings->mid_slot_get_stack)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"rb", "net/minecraft/inventory/IInventory", "og", "net/minecraft/inventory/IInventory"}, &mappings->cls_iinventory)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_iinventory, ver, flavor, {{{"a", "(I)Ladd;"}, {"getStackInSlot", "(I)Lnet/minecraft/item/ItemStack;"}, {"func_70301_a", "(I)Lnet/minecraft/item/ItemStack;"}}, {{"a", "(I)Lzx;"}, {"getStackInSlot", "(I)Lnet/minecraft/item/ItemStack;"}, {"func_70301_a", "(I)Lnet/minecraft/item/ItemStack;"}}}, &mappings->mid_iinventory_get_stack_in_slot)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_iinventory, ver, flavor, {{{"a", "()I"}, {"getSizeInventory", "()I"}, {"func_70302_i_", "()I"}}, {{"o_", "()I"}, {"getSizeInventory", "()I"}, {"func_70302_i_", "()I"}}}, &mappings->mid_iinventory_get_size_inventory)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ej", "net/minecraft/network/NetworkManager", "ek", "net/minecraft/network/NetworkManager"}, &mappings->cls_network_manager)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_network_manager, ver, flavor, {{{"m", "Lio/netty/channel/Channel;"}, {"channel", "Lio/netty/channel/Channel;"}, {"field_150746_k", "Lio/netty/channel/Channel;"}}, {{"k", "Lio/netty/channel/Channel;"}, {"channel", "Lio/netty/channel/Channel;"}, {"field_150746_k", "Lio/netty/channel/Channel;"}}}, &mappings->fid_network_manager_channel)) return err;
    if (ver == 0) {
        if (int err = resolve_method_1v(env, mappings->cls_network_manager, flavor, {{"a", "(Lft;[Lio/netty/util/concurrent/GenericFutureListener;)V"}, {"scheduleOutboundPacket", "(Lnet/minecraft/network/Packet;[Lio/netty/util/concurrent/GenericFutureListener;)V"}, {"func_150725_a", "(Lnet/minecraft/network/Packet;[Lio/netty/util/concurrent/GenericFutureListener;)V"}}, &mappings->mid_network_manager_send_packet_listeners)) return err;
    } else {
        if (int err = resolve_method_1v(env, mappings->cls_network_manager, flavor, {{"a", "(Lff;)V"}, {"sendPacket", "(Lnet/minecraft/network/Packet;)V"}, {"func_179290_a", "(Lnet/minecraft/network/Packet;)V"}}, &mappings->mid_network_manager_send_packet)) return err;
    }
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"jd", "net/minecraft/network/play/client/C03PacketPlayer", "ip", "net/minecraft/network/play/client/C03PacketPlayer"}, &mappings->cls_c03_packet_player)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c03_packet_player, ver, flavor, {{{"h", "Z"}, {"field_149480_h", "Z"}, {"field_149480_h", "Z"}}, {{"g", "Z"}, {"moving", "Z"}, {"field_149480_h", "Z"}}}, &mappings->fid_c03_moving)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c03_packet_player, ver, flavor, {{{"g", "Z"}, {"field_149474_g", "Z"}, {"field_149474_g", "Z"}}, {{"f", "Z"}, {"onGround", "Z"}, {"field_149474_g", "Z"}}}, &mappings->fid_c03_on_ground)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c03_packet_player, ver, flavor, {{{"a", "D"}, {"x", "D"}, {"field_149479_a", "D"}}, {{"a", "D"}, {"x", "D"}, {"field_149479_a", "D"}}}, &mappings->fid_c03_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c03_packet_player, ver, flavor, {{{"b", "D"}, {"y", "D"}, {"field_149477_b", "D"}}, {{"b", "D"}, {"y", "D"}, {"field_149477_b", "D"}}}, &mappings->fid_c03_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c03_packet_player, ver, flavor, {{{"c", "D"}, {"z", "D"}, {"field_149478_c", "D"}}, {{"c", "D"}, {"z", "D"}, {"field_149478_c", "D"}}}, &mappings->fid_c03_z)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ji", "net/minecraft/network/play/client/C07PacketPlayerDigging", "ir", "net/minecraft/network/play/client/C07PacketPlayerDigging"}, &mappings->cls_c07_packet_player_digging)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c07_packet_player_digging, ver, flavor, {{{"e", "I"}, {"status", "I"}, {"field_149508_e", "I"}}, {{"c", "Lir$a;"}, {"status", "Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;"}, {"field_149508_e", "Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;"}}}, &mappings->fid_c07_status)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"jo", "net/minecraft/network/play/client/C08PacketPlayerBlockPlacement", "ja", "net/minecraft/network/play/client/C08PacketPlayerBlockPlacement"}, &mappings->cls_c08_packet_player_block_placement)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c08_packet_player_block_placement, ver, flavor, {{{"e", "Ladd;"}, {"stack", "Lnet/minecraft/item/ItemStack;"}, {"field_149580_e", "Lnet/minecraft/item/ItemStack;"}}, {{"d", "Lzx;"}, {"stack", "Lnet/minecraft/item/ItemStack;"}, {"field_149580_e", "Lnet/minecraft/item/ItemStack;"}}}, &mappings->fid_c08_stack)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"aor", "net/minecraft/tileentity/TileEntity", "akw", "net/minecraft/tileentity/TileEntity"}, &mappings->cls_tile_entity)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_tile_entity, ver, flavor, {{{"h", "Laji;"}, {"blockType", "Lnet/minecraft/block/Block;"}, {"field_145854_h", "Lnet/minecraft/block/Block;"}}, {{"e", "Lafh;"}, {"blockType", "Lnet/minecraft/block/Block;"}, {"field_145854_h", "Lnet/minecraft/block/Block;"}}}, &mappings->fid_tile_entity_block_type)) return err;
    if (ver == 0) {
        if (int err = resolve_field_1v(env, mappings->cls_tile_entity, flavor, {{"c", "I"}, {"xCoord", "I"}, {"field_145851_c", "I"}}, &mappings->fid_tile_entity_x_1_7)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_tile_entity, flavor, {{"d", "I"}, {"yCoord", "I"}, {"field_145848_d", "I"}}, &mappings->fid_tile_entity_y_1_7)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_tile_entity, flavor, {{"e", "I"}, {"zCoord", "I"}, {"field_145849_e", "I"}}, &mappings->fid_tile_entity_z_1_7)) return err;
    } else {
        if (int err = resolve_field_1v(env, mappings->cls_tile_entity, flavor, {{"c", "Lcj;"}, {"pos", "Lnet/minecraft/util/BlockPos;"}, {"field_174879_c", "Lnet/minecraft/util/BlockPos;"}}, &mappings->fid_tile_entity_pos_1_8)) return err;
    }
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"hy", "net/minecraft/network/play/server/S12PacketEntityVelocity", "hm", "net/minecraft/network/play/server/S12PacketEntityVelocity"}, &mappings->cls_s12_packet_entity_velocity)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s12_packet_entity_velocity, ver, flavor, {{{"a", "I"}, {"field_149417_a", "I"}, {"field_149417_a", "I"}}, {{"a", "I"}, {"entityID", "I"}, {"field_149417_a", "I"}}}, &mappings->fid_s12_entity_id)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s12_packet_entity_velocity, ver, flavor, {{{"b", "I"}, {"field_149415_b", "I"}, {"field_149415_b", "I"}}, {{"b", "I"}, {"motionX", "I"}, {"field_149415_b", "I"}}}, &mappings->fid_s12_motion_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s12_packet_entity_velocity, ver, flavor, {{{"c", "I"}, {"field_149416_c", "I"}, {"field_149416_c", "I"}}, {{"c", "I"}, {"motionY", "I"}, {"field_149416_c", "I"}}}, &mappings->fid_s12_motion_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s12_packet_entity_velocity, ver, flavor, {{{"d", "I"}, {"field_149414_d", "I"}, {"field_149414_d", "I"}}, {{"d", "I"}, {"motionZ", "I"}, {"field_149414_d", "I"}}}, &mappings->fid_s12_motion_z)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bnn", "net/minecraft/client/renderer/entity/RenderManager", "biu", "net/minecraft/client/renderer/entity/RenderManager"}, &mappings->cls_render_manager)) return err;
    if (ver == 0) {
        if (int err = resolve_static_field_1v(env, mappings->cls_render_manager, flavor, {{"a", "Lbnn;"}, {"instance", "Lnet/minecraft/client/renderer/entity/RenderManager;"}, {"field_78727_a", "Lnet/minecraft/client/renderer/entity/RenderManager;"}}, &mappings->sfid_render_manager_instance_1_7)) return err;
    }
    if (int err = resolve_field_2v(env, mappings->cls_render_manager, ver, flavor, {{{"k", "F"}, {"playerViewX", "F"}, {"field_78732_j", "F"}}, {{"f", "F"}, {"playerViewX", "F"}, {"field_78732_j", "F"}}}, &mappings->fid_render_manager_player_view_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_render_manager, ver, flavor, {{{"j", "F"}, {"playerViewY", "F"}, {"field_78735_i", "F"}}, {{"e", "F"}, {"playerViewY", "F"}, {"field_78735_i", "F"}}}, &mappings->fid_render_manager_player_view_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_render_manager, ver, flavor, {{{"r", "Lbbu;"}, {"textRenderer", "Lnet/minecraft/client/gui/FontRenderer;"}, {"field_78736_p", "Lnet/minecraft/client/gui/FontRenderer;"}}, {{"n", "Lavn;"}, {"textRenderer", "Lnet/minecraft/client/gui/FontRenderer;"}, {"field_78736_p", "Lnet/minecraft/client/gui/FontRenderer;"}}}, &mappings->fid_render_manager_font_renderer)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"hf", "net/minecraft/network/play/server/S14PacketEntity", "gv", "net/minecraft/network/play/server/S14PacketEntity"}, &mappings->cls_s14_packet_entity)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"a", "I"}, {"field_149074_a", "I"}, {"field_149074_a", "I"}}, {{"a", "I"}, {"entityId", "I"}, {"field_149074_a", "I"}}}, &mappings->fid_s14_entity_id)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"b", "B"}, {"field_149072_b", "B"}, {"field_149072_b", "B"}}, {{"b", "B"}, {"posX", "B"}, {"field_149072_b", "B"}}}, &mappings->fid_s14_pos_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"c", "B"}, {"field_149073_c", "B"}, {"field_149073_c", "B"}}, {{"c", "B"}, {"posY", "B"}, {"field_149073_c", "B"}}}, &mappings->fid_s14_pos_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"d", "B"}, {"field_149070_d", "B"}, {"field_149070_d", "B"}}, {{"d", "B"}, {"posZ", "B"}, {"field_149070_d", "B"}}}, &mappings->fid_s14_pos_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"e", "B"}, {"field_149071_e", "B"}, {"field_149071_e", "B"}}, {{"e", "B"}, {"yaw", "B"}, {"field_149071_e", "B"}}}, &mappings->fid_s14_yaw)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"f", "B"}, {"field_149068_f", "B"}, {"field_149068_f", "B"}}, {{"f", "B"}, {"pitch", "B"}, {"field_149068_f", "B"}}}, &mappings->fid_s14_pitch)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s14_packet_entity, ver, flavor, {{{"g", "Z"}, {"field_149069_g", "Z"}, {"field_149069_g", "Z"}}, {{"h", "Z"}, {"field_149069_g", "Z"}, {"field_149069_g", "Z"}}}, &mappings->fid_s14_has_rot)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ik", "net/minecraft/network/play/server/S18PacketEntityTeleport", "hz", "net/minecraft/network/play/server/S18PacketEntityTeleport"}, &mappings->cls_s18_packet_entity_teleport)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s18_packet_entity_teleport, ver, flavor, {{{"a", "I"}, {"field_149458_a", "I"}, {"field_149458_a", "I"}}, {{"a", "I"}, {"entityId", "I"}, {"field_149458_a", "I"}}}, &mappings->fid_s18_entity_id)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s18_packet_entity_teleport, ver, flavor, {{{"b", "I"}, {"field_149456_b", "I"}, {"field_149456_b", "I"}}, {{"b", "I"}, {"posX", "I"}, {"field_149456_b", "I"}}}, &mappings->fid_s18_pos_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s18_packet_entity_teleport, ver, flavor, {{{"c", "I"}, {"field_149457_c", "I"}, {"field_149457_c", "I"}}, {{"c", "I"}, {"posY", "I"}, {"field_149457_c", "I"}}}, &mappings->fid_s18_pos_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s18_packet_entity_teleport, ver, flavor, {{{"d", "I"}, {"field_149454_d", "I"}, {"field_149454_d", "I"}}, {{"d", "I"}, {"posZ", "I"}, {"field_149454_d", "I"}}}, &mappings->fid_s18_pos_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s18_packet_entity_teleport, ver, flavor, {{{"e", "B"}, {"field_149455_e", "B"}, {"field_149455_e", "B"}}, {{"e", "B"}, {"yaw", "B"}, {"field_149455_e", "B"}}}, &mappings->fid_s18_yaw)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s18_packet_entity_teleport, ver, flavor, {{{"f", "B"}, {"field_149453_f", "B"}, {"field_149453_f", "B"}}, {{"f", "B"}, {"pitch", "B"}, {"field_149453_f", "B"}}}, &mappings->fid_s18_pitch)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"gt", "net/minecraft/network/play/server/S19PacketEntityStatus", "gi", "net/minecraft/network/play/server/S19PacketEntityStatus"}, &mappings->cls_s19_packet_entity_status)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s19_packet_entity_status, ver, flavor, {{{"a", "I"}, {"field_149164_a", "I"}, {"field_149164_a", "I"}}, {{"a", "I"}, {"entityId", "I"}, {"field_149164_a", "I"}}}, &mappings->fid_s19_entity_id)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bll", "net/minecraft/client/entity/EntityOtherPlayerMP", "bex", "net/minecraft/client/entity/EntityOtherPlayerMP"}, &mappings->cls_entity_other_player_mp)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_other_player_mp, ver, flavor, {{{"c", "I"}, {"otherPlayerMPPosRotationIncrements", "I"}, {"field_71184_b", "I"}}, {{"b", "I"}, {"otherPlayerMPPosRotationIncrements", "I"}, {"field_71184_b", "I"}}}, &mappings->fid_other_player_pos_rot_increments)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_other_player_mp, ver, flavor, {{{"d", "D"}, {"otherPlayerMPX", "D"}, {"field_71185_c", "D"}}, {{"c", "D"}, {"otherPlayerMPX", "D"}, {"field_71185_c", "D"}}}, &mappings->fid_other_player_mp_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_other_player_mp, ver, flavor, {{{"e", "D"}, {"otherPlayerMPY", "D"}, {"field_71182_d", "D"}}, {{"d", "D"}, {"otherPlayerMPY", "D"}, {"field_71182_d", "D"}}}, &mappings->fid_other_player_mp_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_other_player_mp, ver, flavor, {{{"f", "D"}, {"otherPlayerMPZ", "D"}, {"field_71183_e", "D"}}, {{"e", "D"}, {"otherPlayerMPZ", "D"}, {"field_71183_e", "D"}}}, &mappings->fid_other_player_mp_z)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_other_player_mp, ver, flavor, {{{"g", "D"}, {"otherPlayerMPYaw", "D"}, {"field_71180_f", "D"}}, {{"f", "D"}, {"otherPlayerMPYaw", "D"}, {"field_71180_f", "D"}}}, &mappings->fid_other_player_mp_yaw)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"blk", "net/minecraft/client/entity/EntityPlayerSP", "bew", "net/minecraft/client/entity/EntityPlayerSP"}, &mappings->cls_entity_player_sp)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"g", "F"}, {"renderArmYaw", "F"}, {"field_71154_f", "F"}}, {{"f", "F"}, {"renderArmYaw", "F"}, {"field_71154_f", "F"}}}, &mappings->fid_player_sp_render_arm_yaw)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"h", "F"}, {"renderArmPitch", "F"}, {"field_71155_g", "F"}}, {{"g", "F"}, {"renderArmPitch", "F"}, {"field_71155_g", "F"}}}, &mappings->fid_player_sp_render_arm_pitch)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"i", "F"}, {"prevRenderArmYaw", "F"}, {"field_71163_h", "F"}}, {{"h", "F"}, {"prevRenderArmYaw", "F"}, {"field_71163_h", "F"}}}, &mappings->fid_player_sp_prev_render_arm_yaw)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"bL", "F"}, {"prevRenderArmPitch", "F"}, {"field_71164_i", "F"}}, {{"i", "F"}, {"prevRenderArmPitch", "F"}, {"field_71164_i", "F"}}}, &mappings->fid_player_sp_prev_render_arm_pitch)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"bM", "F"}, {"timeInPortal", "F"}, {"field_71086_bY", "F"}}, {{"bH", "F"}, {"timeInPortal", "F"}, {"field_71086_bY", "F"}}}, &mappings->fid_player_sp_time_in_portal)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"bN", "F"}, {"prevTimeInPortal", "F"}, {"field_71080_cy", "F"}}, {{"bI", "F"}, {"prevTimeInPortal", "F"}, {"field_71080_cy", "F"}}}, &mappings->fid_player_sp_prev_time_in_portal)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_player_sp, ver, flavor, {{{"a", "(Lfj;)V"}, {"addChatMessage", "(Lnet/minecraft/util/IChatComponent;)V"}, {"func_145747_a", "(Lnet/minecraft/util/IChatComponent;)V"}}, {{"a", "(Leu;)V"}, {"addChatMessage", "(Lnet/minecraft/util/IChatComponent;)V"}, {"func_145747_a", "(Lnet/minecraft/util/IChatComponent;)V"}}}, &mappings->mid_player_sp_add_chat_message)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bfu", "net/minecraft/client/gui/inventory/GuiInventory", "azc", "net/minecraft/client/gui/inventory/GuiInventory"}, &mappings->cls_gui_inventory)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ib", "net/minecraft/network/play/server/S06PacketUpdateHealth", "hp", "net/minecraft/network/play/server/S06PacketUpdateHealth"}, &mappings->cls_s06_packet_update_health)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"hc", "net/minecraft/network/play/server/S29PacketSoundEffect", "gs", "net/minecraft/network/play/server/S29PacketSoundEffect"}, &mappings->cls_s29_packet_sound_effect)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"blt", "net/minecraft/client/renderer/EntityRenderer", "bfk", "net/minecraft/client/renderer/EntityRenderer"}, &mappings->cls_entity_renderer)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_renderer, ver, flavor, {{{"a", "(F)V"}, {"getMouseOver", "(F)V"}, {"func_78473_a", "(F)V"}}, {{"a", "(F)V"}, {"getMouseOver", "(F)V"}, {"func_78473_a", "(F)V"}}}, &mappings->mid_entity_renderer_get_mouse_over)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_renderer, ver, flavor, {{{"b", "(FI)V"}, {"renderHand", "(FI)V"}, {"func_78476_b", "(FI)V"}}, {{"b", "(FI)V"}, {"renderHand", "(FI)V"}, {"func_78476_b", "(FI)V"}}}, &mappings->mid_entity_renderer_render_hand)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_renderer, ver, flavor, {{{"h", "(F)V"}, {"orientCamera", "(F)V"}, {"func_78467_g", "(F)V"}}, {{"f", "(F)V"}, {"orientCamera", "(F)V"}, {"func_78467_g", "(F)V"}}}, &mappings->mid_entity_renderer_orient_camera)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_renderer, ver, flavor, {{{"a", "(FZ)F"}, {"getFOVModifier", "(FZ)F"}, {"func_78481_a", "(FZ)F"}}, {{"a", "(FZ)F"}, {"getFOVModifier", "(FZ)F"}, {"func_78481_a", "(FZ)F"}}}, &mappings->mid_entity_renderer_get_fov_modifier)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_renderer, ver, flavor, {{{"b", "(F)V"}, {"updateCameraAndRender", "(F)V"}, {"func_78480_b", "(F)V"}}, {{"a", "(FJ)V"}, {"updateCameraAndRender", "(FJ)V"}, {"func_181560_a", "(FJ)V"}}}, &mappings->mid_entity_renderer_setup_camera_transform)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bmg", "net/minecraft/client/shader/Framebuffer", "bfw", "net/minecraft/client/shader/Framebuffer"}, &mappings->cls_framebuffer)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_framebuffer, ver, flavor, {{{"a", "(Z)V"}, {"bindFramebuffer", "(Z)V"}, {"func_147610_a", "(Z)V"}}, {{"a", "(Z)V"}, {"bindFramebuffer", "(Z)V"}, {"func_147610_a", "(Z)V"}}}, &mappings->mid_framebuffer_bind)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bma", "net/minecraft/client/renderer/RenderGlobal", "bfr", "net/minecraft/client/renderer/RenderGlobal"}, &mappings->cls_render_global)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_render_global, ver, flavor, {{{"R", "Z"}, {"displayListEntitiesDirty", "Z"}, {"field_147595_R", "Z"}}, {{"ac", "Z"}, {"displayListEntitiesDirty", "Z"}, {"field_147595_R", "Z"}}}, &mappings->fid_render_global_entities_dirty)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"baj", "net/minecraft/client/renderer/ActiveRenderInfo", "auz", "net/minecraft/client/renderer/ActiveRenderInfo"}, &mappings->cls_active_render_info)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_active_render_info, ver, flavor, {{{"a", "(Lyz;Z)V"}, {"updateRenderInfo", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"}, {"func_74583_a", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"}}, {{"a", "(Lwn;Z)V"}, {"updateRenderInfo", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"}, {"func_74583_a", "(Lnet/minecraft/entity/player/EntityPlayer;Z)V"}}}, &mappings->smid_active_render_info_update)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"boh", "net/minecraft/client/renderer/entity/RendererLivingEntity", "bjl", "net/minecraft/client/renderer/entity/RendererLivingEntity"}, &mappings->cls_renderer_living_entity)) return err;
    mappings->hook_rle_render_name_method_name = select_flavor_str_2v(ver, flavor, {"b", "passSpecialRender", "func_77033_b"}, {"b", "renderName", "func_177067_a"});
    mappings->hook_rle_render_name_method_desc = select_flavor_str_2v(ver, flavor, {"(Lsv;DDD)V", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V"}, {"(Lpr;DDD)V", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V"});
    mappings->hook_rle_can_render_name_method_name = select_flavor_str_2v(ver, flavor, {"b", "canRenderName", "func_110813_b"}, {"a", "canRenderName", "func_177070_b"});
    mappings->hook_rle_can_render_name_method_desc = select_flavor_str_2v(ver, flavor, {"(Lsv;)Z", "(Lnet/minecraft/entity/EntityLivingBase;)Z", "(Lnet/minecraft/entity/EntityLivingBase;)Z"}, {"(Lpr;)Z", "(Lnet/minecraft/entity/EntityLivingBase;)Z", "(Lnet/minecraft/entity/EntityLivingBase;)Z"});
    mappings->hook_rle_do_render_method_name = select_flavor_str_2v(ver, flavor, {"a", "doRender", "func_76986_a"}, {"a", "doRender", "func_76986_a"});
    mappings->hook_rle_do_render_method_desc = select_flavor_str_2v(ver, flavor, {"(Lsv;DDDFF)V", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"}, {"(Lpr;DDDFF)V", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"});
    if (int err = resolve_method_2v(env, mappings->cls_renderer_living_entity, ver, flavor, {{{"b", "(Lsv;DDD)V"}, {"passSpecialRender", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V"}, {"func_77033_b", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V"}}, {{"b", "(Lpr;DDD)V"}, {"renderName", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V"}, {"func_177067_a", "(Lnet/minecraft/entity/EntityLivingBase;DDD)V"}}}, &mappings->mid_rle_render_name)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_renderer_living_entity, ver, flavor, {{{"b", "(Lsv;)Z"}, {"canRenderName", "(Lnet/minecraft/entity/EntityLivingBase;)Z"}, {"func_110813_b", "(Lnet/minecraft/entity/EntityLivingBase;)Z"}}, {{"a", "(Lpr;)Z"}, {"canRenderName", "(Lnet/minecraft/entity/EntityLivingBase;)Z"}, {"func_177070_b", "(Lnet/minecraft/entity/EntityLivingBase;)Z"}}}, &mappings->mid_rle_can_render_name)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_renderer_living_entity, ver, flavor, {{{"a", "(Lsv;DDDFF)V"}, {"doRender", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"}, {"func_76986_a", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"}}, {{"a", "(Lpr;DDDFF)V"}, {"doRender", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"}, {"func_76986_a", "(Lnet/minecraft/entity/EntityLivingBase;DDDFF)V"}}}, &mappings->mid_rle_do_render)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bbu", "net/minecraft/client/gui/FontRenderer", "avn", "net/minecraft/client/gui/FontRenderer"}, &mappings->cls_font_renderer)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_font_renderer, ver, flavor, {{{"a", "(Ljava/lang/String;)I"}, {"getStringWidth", "(Ljava/lang/String;)I"}, {"func_78256_a", "(Ljava/lang/String;)I"}}, {{"a", "(Ljava/lang/String;)I"}, {"getStringWidth", "(Ljava/lang/String;)I"}, {"func_78256_a", "(Ljava/lang/String;)I"}}}, &mappings->mid_font_renderer_get_string_width)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_font_renderer, ver, flavor, {{{"a", "(Ljava/lang/String;IIIZ)I"}, {"drawString", "(Ljava/lang/String;IIIZ)I"}, {"func_85187_a", "(Ljava/lang/String;IIIZ)I"}}, {{"a", "(Ljava/lang/String;FFIZ)I"}, {"drawString", "(Ljava/lang/String;FFIZ)I"}, {"func_175065_a", "(Ljava/lang/String;FFIZ)I"}}}, &mappings->mid_font_renderer_draw_string)) return err;
    if (ver != 0) {
        if (int err = resolve_class_1v(env, loader, is_deobf, "bfl", "net/minecraft/client/renderer/GlStateManager", &mappings->cls_gl_state_manager)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"l", "()V"}, {"enableBlend", "()V"}, {"func_179147_l", "()V"}}, &mappings->smid_gl_enable_blend)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"k", "()V"}, {"disableBlend", "()V"}, {"func_179084_k", "()V"}}, &mappings->smid_gl_disable_blend)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"j", "()V"}, {"enableDepth", "()V"}, {"func_179126_j", "()V"}}, &mappings->smid_gl_enable_depth)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"i", "()V"}, {"disableDepth", "()V"}, {"func_179097_i", "()V"}}, &mappings->smid_gl_disable_depth)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"e", "()V"}, {"enableLighting", "()V"}, {"func_179145_e", "()V"}}, &mappings->smid_gl_enable_lighting)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"f", "()V"}, {"disableLighting", "()V"}, {"func_179140_f", "()V"}}, &mappings->smid_gl_disable_lighting)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"w", "()V"}, {"enableTexture2D", "()V"}, {"func_179098_w", "()V"}}, &mappings->smid_gl_enable_texture_2d)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"x", "()V"}, {"disableTexture2D", "()V"}, {"func_179090_x", "()V"}}, &mappings->smid_gl_disable_texture_2d)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"o", "()V"}, {"enableCull", "()V"}, {"func_179089_o", "()V"}}, &mappings->smid_gl_enable_cull)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"p", "()V"}, {"disableCull", "()V"}, {"func_179129_p", "()V"}}, &mappings->smid_gl_disable_cull)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"d", "()V"}, {"enableAlpha", "()V"}, {"func_179141_d", "()V"}}, &mappings->smid_gl_enable_alpha)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"c", "()V"}, {"disableAlpha", "()V"}, {"func_179118_c", "()V"}}, &mappings->smid_gl_disable_alpha)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"b", "(II)V"}, {"blendFunc", "(II)V"}, {"func_179112_b", "(II)V"}}, &mappings->smid_gl_blend_func)) return err;
        if (int err = resolve_static_method_1v(env, mappings->cls_gl_state_manager, flavor, {{"c", "(FFFF)V"}, {"color", "(FFFF)V"}, {"func_179131_c", "(FFFF)V"}}, &mappings->smid_gl_color)) return err;
    }
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ja", "net/minecraft/network/play/client/C02PacketUseEntity", "in", "net/minecraft/network/play/client/C02PacketUseEntity"}, &mappings->cls_c02_packet_use_entity)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"jj", "net/minecraft/network/play/client/C0BPacketEntityAction", "is", "net/minecraft/network/play/client/C0BPacketEntityAction"}, &mappings->cls_c0b_packet_entity_action)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c0b_packet_entity_action, ver, flavor, {{{"b", "I"}, {"field_149515_b", "I"}, {"field_149515_b", "I"}}, {{"b", "Lis$a;"}, {"action", "Lnet/minecraft/network/play/client/C0BPacketEntityAction$Action;"}, {"field_149515_b", "Lnet/minecraft/network/play/client/C0BPacketEntityAction$Action;"}}}, &mappings->fid_c0b_action)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"fu", "net/minecraft/network/play/server/S08PacketPlayerPosLook", "fi", "net/minecraft/network/play/server/S08PacketPlayerPosLook"}, &mappings->cls_s08_packet_player_pos_look)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"gu", "net/minecraft/network/play/server/S27PacketExplosion", "gk", "net/minecraft/network/play/server/S27PacketExplosion"}, &mappings->cls_s27_packet_explosion)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s27_packet_explosion, ver, flavor, {{{"f", "F"}, {"field_149152_f", "F"}, {"field_149152_f", "F"}}, {{"f", "F"}, {"field_149152_f", "F"}, {"field_149152_f", "F"}}}, &mappings->fid_s27_motion_x)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s27_packet_explosion, ver, flavor, {{{"g", "F"}, {"field_149153_g", "F"}, {"field_149153_g", "F"}}, {{"g", "F"}, {"field_149153_g", "F"}, {"field_149153_g", "F"}}}, &mappings->fid_s27_motion_y)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s27_packet_explosion, ver, flavor, {{{"h", "F"}, {"field_149159_h", "F"}, {"field_149159_h", "F"}}, {{"h", "F"}, {"field_149159_h", "F"}, {"field_149159_h", "F"}}}, &mappings->fid_s27_motion_z)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"gw", "net/minecraft/network/play/server/S00PacketKeepAlive", "gn", "net/minecraft/network/play/server/S00PacketKeepAlive"}, &mappings->cls_s00_packet_keep_alive)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"jc", "net/minecraft/network/play/client/C00PacketKeepAlive", "io", "net/minecraft/network/play/client/C00PacketKeepAlive"}, &mappings->cls_c00_packet_keep_alive)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"rv", "net/minecraft/potion/Potion", "pe", "net/minecraft/potion/Potion"}, &mappings->cls_potion)) return err;
    if (int err = resolve_static_field_2v(env, mappings->cls_potion, ver, flavor, {{{"a", "[Lrv;"}, {"potionTypes", "[Lnet/minecraft/potion/Potion;"}, {"field_76425_a", "[Lnet/minecraft/potion/Potion;"}}, {{"a", "[Lpe;"}, {"potionTypes", "[Lnet/minecraft/potion/Potion;"}, {"field_76425_a", "[Lnet/minecraft/potion/Potion;"}}}, &mappings->sfid_potion_types)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bop", "net/minecraft/client/renderer/entity/RenderPlayer", "bln", "net/minecraft/client/renderer/entity/RenderPlayer"}, &mappings->cls_render_player)) return err;
    mappings->hook_render_player_do_render_name = select_flavor_str_2v(ver, flavor, {"a", "doRender", "func_76986_a"}, {"a", "doRender", "func_76986_a"});
    std::snprintf(s_render_player_do_render_desc, sizeof(s_render_player_do_render_desc), "(L%s;DDDFF)V", is_deobf ? "net/minecraft/client/entity/AbstractClientPlayer" : (ver == 0 ? "blg" : "bet"));
    mappings->hook_render_player_do_render_desc = s_render_player_do_render_desc;
    if (int err = resolve_method_2v(env, mappings->cls_render_player, ver, flavor, {{{"a", "(Lblg;DDDFF)V"}, {"doRender", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"}, {"func_76986_a", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"}}, {{"a", "(Lbet;DDDFF)V"}, {"doRender", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"}, {"func_76986_a", "(Lnet/minecraft/client/entity/AbstractClientPlayer;DDDFF)V"}}}, &mappings->mid_render_player_do_render)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bny", "net/minecraft/client/renderer/entity/RenderItem", "bjh", "net/minecraft/client/renderer/entity/RenderItem"}, &mappings->cls_render_item)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_render_item, ver, flavor, {{{"f", "F"}, {"zLevel", "F"}, {"field_77023_b", "F"}}, {{"a", "F"}, {"zLevel", "F"}, {"field_77023_b", "F"}}}, &mappings->fid_render_item_z_level)) return err;
    if (ver == 0) {
        if (int err = resolve_method_1v(env, mappings->cls_render_item, flavor, {{"<init>", "()V"}, {"<init>", "()V"}, {"<init>", "()V"}}, &mappings->mid_render_item_init_1_7)) return err;
    }
    if (int err = resolve_method_2v(env, mappings->cls_render_item, ver, flavor, {{{"b", "(Lbbu;Lbqf;Ladd;II)V"}, {"renderItemAndEffectIntoGUI", "(Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/client/renderer/texture/TextureManager;Lnet/minecraft/item/ItemStack;II)V"}, {"func_82406_b", "(Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/client/renderer/texture/TextureManager;Lnet/minecraft/item/ItemStack;II)V"}}, {{"b", "(Lzx;II)V"}, {"renderItemAndEffectIntoGUI", "(Lnet/minecraft/item/ItemStack;II)V"}, {"func_180450_b", "(Lnet/minecraft/item/ItemStack;II)V"}}}, &mappings->mid_render_item_into_gui)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_render_item, ver, flavor, {{{"a", "(Lbbu;Lbqf;Ladd;IILjava/lang/String;)V"}, {"renderItemOverlayIntoGUI", "(Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/client/renderer/texture/TextureManager;Lnet/minecraft/item/ItemStack;IILjava/lang/String;)V"}, {"func_94148_a", "(Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/client/renderer/texture/TextureManager;Lnet/minecraft/item/ItemStack;IILjava/lang/String;)V"}}, {{"a", "(Lavn;Lzx;IILjava/lang/String;)V"}, {"renderItemOverlayIntoGUI", "(Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/item/ItemStack;IILjava/lang/String;)V"}, {"func_180453_a", "(Lnet/minecraft/client/gui/FontRenderer;Lnet/minecraft/item/ItemStack;IILjava/lang/String;)V"}}}, &mappings->mid_render_item_overlay_into_gui)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bam", "net/minecraft/client/renderer/RenderHelper", "avc", "net/minecraft/client/renderer/RenderHelper"}, &mappings->cls_render_helper)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_render_helper, ver, flavor, {{{"b", "()V"}, {"enableStandardItemLighting", "()V"}, {"func_74519_b", "()V"}}, {{"b", "()V"}, {"enableStandardItemLighting", "()V"}, {"func_74519_b", "()V"}}}, &mappings->smid_render_helper_enable_item_lighting)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_render_helper, ver, flavor, {{{"a", "()V"}, {"disableStandardItemLighting", "()V"}, {"func_74518_a", "()V"}}, {{"a", "()V"}, {"disableStandardItemLighting", "()V"}, {"func_74518_a", "()V"}}}, &mappings->smid_render_helper_disable_item_lighting)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"sg", "net/minecraft/entity/EntityList", "pm", "net/minecraft/entity/EntityList"}, &mappings->cls_entity_list)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_entity_list, ver, flavor, {{{"a", "(Lsa;)I"}, {"getEntityID", "(Lnet/minecraft/entity/Entity;)I"}, {"func_75619_a", "(Lnet/minecraft/entity/Entity;)I"}}, {{"a", "(Lpk;)I"}, {"getEntityID", "(Lnet/minecraft/entity/Entity;)I"}, {"func_75619_a", "(Lnet/minecraft/entity/Entity;)I"}}}, &mappings->smid_entity_list_get_entity_id)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"apx", "net/minecraft/world/chunk/Chunk", "amy", "net/minecraft/world/chunk/Chunk"}, &mappings->cls_chunk)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_chunk, ver, flavor, {{{"u", "[Lapz;"}, {"storageArrays", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"}, {"field_76652_q", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"}}, {{"d", "[Lamz;"}, {"storageArrays", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"}, {"field_76652_q", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"}}}, &mappings->fid_chunk_storage_arrays)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_chunk, ver, flavor, {{{"g", "I"}, {"xPosition", "I"}, {"field_76635_g", "I"}}, {{"a", "I"}, {"xPosition", "I"}, {"field_76635_g", "I"}}}, &mappings->fid_chunk_x_position)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_chunk, ver, flavor, {{{"h", "I"}, {"zPosition", "I"}, {"field_76647_h", "I"}}, {{"b", "I"}, {"zPosition", "I"}, {"field_76647_h", "I"}}}, &mappings->fid_chunk_z_position)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"apz", "net/minecraft/world/chunk/storage/ExtendedBlockStorage", "amz", "net/minecraft/world/chunk/storage/ExtendedBlockStorage"}, &mappings->cls_extended_block_storage)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_extended_block_storage, ver, flavor, {{{"d", "[B"}, {"blockLSBArray", "[B"}, {"field_76680_d", "[B"}}, {{"d", "[C"}, {"data", "[C"}, {"field_177488_d", "[C"}}}, &mappings->fid_ebs_block_data)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bjf", "net/minecraft/client/multiplayer/WorldClient", "bdb", "net/minecraft/client/multiplayer/WorldClient"}, &mappings->cls_world_client)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bjd", "net/minecraft/client/multiplayer/ChunkProviderClient", "bcz", "net/minecraft/client/multiplayer/ChunkProviderClient"}, &mappings->cls_chunk_provider_client)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_chunk_provider_client, ver, flavor, {{{"d", "Ljava/util/List;"}, {"chunkListing", "Ljava/util/List;"}, {"field_73237_c", "Ljava/util/List;"}}, {{"d", "Ljava/util/List;"}, {"chunkListing", "Ljava/util/List;"}, {"field_73237_c", "Ljava/util/List;"}}}, &mappings->fid_chunk_provider_chunk_listing)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_chunk_provider_client, ver, flavor, {{{"b", "(II)V"}, {"unloadChunk", "(II)V"}, {"func_73234_b", "(II)V"}}, {{"b", "(II)V"}, {"unloadChunk", "(II)V"}, {"func_73234_b", "(II)V"}}}, &mappings->mid_chunk_provider_unload_chunk)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"xk", "net/minecraft/entity/item/EntityItem", "uz", "net/minecraft/entity/item/EntityItem"}, &mappings->cls_entity_item)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_item, ver, flavor, {{{"f", "()Ladd;"}, {"getEntityItem", "()Lnet/minecraft/item/ItemStack;"}, {"func_92059_d", "()Lnet/minecraft/item/ItemStack;"}}, {{"l", "()Lzx;"}, {"getEntityItem", "()Lnet/minecraft/item/ItemStack;"}, {"func_92059_d", "()Lnet/minecraft/item/ItemStack;"}}}, &mappings->mid_entity_item_get_item_stack)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_entity_item, ver, flavor, {{{"k", "()V"}, {"searchForOtherItemsNearby", "()V"}, {"func_85054_d", "()V"}}, {{"w", "()V"}, {"searchForOtherItemsNearby", "()V"}, {"func_85054_d", "()V"}}}, &mappings->mid_entity_item_search_nearby)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"gr", "net/minecraft/network/play/server/S3FPacketCustomPayload", "gg", "net/minecraft/network/play/server/S3FPacketCustomPayload"}, &mappings->cls_s3f_packet_custom_payload)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s3f_packet_custom_payload, ver, flavor, {{{"a", "Ljava/lang/String;"}, {"channel", "Ljava/lang/String;"}, {"field_149172_a", "Ljava/lang/String;"}}, {{"a", "Ljava/lang/String;"}, {"channel", "Ljava/lang/String;"}, {"field_149172_a", "Ljava/lang/String;"}}}, &mappings->fid_s3f_channel)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_s3f_packet_custom_payload, ver, flavor, {{{"b", "[B"}, {"data", "[B"}, {"field_149171_b", "[B"}}, {{"b", "Lem;"}, {"data", "Lnet/minecraft/network/PacketBuffer;"}, {"field_149171_b", "Lnet/minecraft/network/PacketBuffer;"}}}, &mappings->fid_s3f_data)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"et", "net/minecraft/network/PacketBuffer", "em", "net/minecraft/network/PacketBuffer"}, &mappings->cls_packet_buffer)) return err;
    if (int err = resolve_method_const(env, mappings->cls_packet_buffer, "readableBytes", "()I", &mappings->mid_packet_buffer_readable_bytes)) return err;
    if (int err = resolve_method_const(env, mappings->cls_packet_buffer, "readerIndex", "()I", &mappings->mid_packet_buffer_reader_index)) return err;
    if (int err = resolve_method_const(env, mappings->cls_packet_buffer, "getBytes", "(I[B)Lio/netty/buffer/ByteBuf;", &mappings->mid_packet_buffer_get_bytes)) return err;
    if (int err = resolve_class_const(env, loader, "java/util/UUID", &mappings->cls_java_util_uuid)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_uuid, "getLeastSignificantBits", "()J", &mappings->mid_uuid_get_least_sig_bits)) return err;
    if (int err = resolve_method_const(env, mappings->cls_java_util_uuid, "getMostSignificantBits", "()J", &mappings->mid_uuid_get_most_sig_bits)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"fq", "net/minecraft/util/ChatComponentText", "fa", "net/minecraft/util/ChatComponentText"}, &mappings->cls_chat_component_text)) return err;
    if (int err = resolve_method_const(env, mappings->cls_chat_component_text, "<init>", "(Ljava/lang/String;)V", &mappings->mid_chat_component_text_init)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bje", "net/minecraft/client/multiplayer/PlayerControllerMP", "bda", "net/minecraft/client/multiplayer/PlayerControllerMP"}, &mappings->cls_player_controller_mp)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_player_controller_mp, ver, flavor, {{{"g", "F"}, {"curBlockDamageMP", "F"}, {"field_78770_f", "F"}}, {{"e", "F"}, {"curBlockDamageMP", "F"}, {"field_78770_f", "F"}}}, &mappings->fid_controller_cur_block_damage_mp)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_player_controller_mp, ver, flavor, {{{"i", "I"}, {"blockHitDelay", "I"}, {"field_78781_i", "I"}}, {{"g", "I"}, {"blockHitDelay", "I"}, {"field_78781_i", "I"}}}, &mappings->fid_controller_block_hit_delay)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_player_controller_mp, ver, flavor, {{{"j", "Z"}, {"isHittingBlock", "Z"}, {"field_78778_j", "Z"}}, {{"h", "Z"}, {"isHittingBlock", "Z"}, {"field_78778_j", "Z"}}}, &mappings->fid_controller_is_hitting_block)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_player_controller_mp, ver, flavor, {{{"e", "()V"}, {"updateController", "()V"}, {"func_78765_e", "()V"}}, {{"e", "()V"}, {"updateController", "()V"}, {"func_78765_e", "()V"}}}, &mappings->mid_controller_update_controller)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ix", "net/minecraft/network/play/client/C0EPacketClickWindow", "ik", "net/minecraft/network/play/client/C0EPacketClickWindow"}, &mappings->cls_c0e_packet_click_window)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"dq", "net/minecraft/nbt/NBTTagList", "du", "net/minecraft/nbt/NBTTagList"}, &mappings->cls_nbt_tag_list)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_nbt_tag_list, ver, flavor, {{{"c", "()I"}, {"tagCount", "()I"}, {"func_74745_c", "()I"}}, {{"c", "()I"}, {"tagCount", "()I"}, {"func_74745_c", "()I"}}}, &mappings->mid_nbt_tag_list_tag_count)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_nbt_tag_list, ver, flavor, {{{"b", "(I)Ldh;"}, {"getCompoundTagAt", "(I)Lnet/minecraft/nbt/NBTTagCompound;"}, {"func_150305_b", "(I)Lnet/minecraft/nbt/NBTTagCompound;"}}, {{"b", "(I)Ldn;"}, {"getCompoundTagAt", "(I)Lnet/minecraft/nbt/NBTTagCompound;"}, {"func_150305_b", "(I)Lnet/minecraft/nbt/NBTTagCompound;"}}}, &mappings->mid_nbt_tag_list_get_compound_tag_at)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"dh", "net/minecraft/nbt/NBTTagCompound", "dn", "net/minecraft/nbt/NBTTagCompound"}, &mappings->cls_nbt_tag_compound)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_nbt_tag_compound, ver, flavor, {{{"e", "(Ljava/lang/String;)S"}, {"getShort", "(Ljava/lang/String;)S"}, {"func_74765_d", "(Ljava/lang/String;)S"}}, {{"e", "(Ljava/lang/String;)S"}, {"getShort", "(Ljava/lang/String;)S"}, {"func_74765_d", "(Ljava/lang/String;)S"}}}, &mappings->mid_nbt_tag_compound_get_short)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"zc", "net/minecraft/entity/projectile/EntityArrow", "wq", "net/minecraft/entity/projectile/EntityArrow"}, &mappings->cls_entity_arrow)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_entity_arrow, ver, flavor, {{{"i", "Z"}, {"inGround", "Z"}, {"field_70254_i", "Z"}}, {{"i", "Z"}, {"inGround", "Z"}, {"field_70254_i", "Z"}}}, &mappings->fid_entity_arrow_in_ground)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"rw", "net/minecraft/potion/PotionEffect", "pf", "net/minecraft/potion/PotionEffect"}, &mappings->cls_potion_effect)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_potion_effect, ver, flavor, {{{"b", "I"}, {"duration", "I"}, {"field_76460_b", "I"}}, {{"c", "I"}, {"duration", "I"}, {"field_76460_b", "I"}}}, &mappings->fid_potion_effect_duration)) return err;
    if (ver != 0) {
        if (int err = resolve_class_1v(env, loader, is_deobf, "bdc", "net/minecraft/client/network/NetworkPlayerInfo", &mappings->cls_network_player_info)) return err;
        if (int err = resolve_field_1v(env, mappings->cls_network_player_info, flavor, {{"h", "Leu;"}, {"displayName", "Lnet/minecraft/util/IChatComponent;"}, {"field_178872_h", "Lnet/minecraft/util/IChatComponent;"}}, &mappings->fid_network_player_info_display_name)) return err;
        if (int err = resolve_method_1v(env, mappings->cls_network_player_info, flavor, {{"i", "()Laul;"}, {"getPlayerTeam", "()Lnet/minecraft/scoreboard/ScorePlayerTeam;"}, {"func_178850_i", "()Lnet/minecraft/scoreboard/ScorePlayerTeam;"}}, &mappings->mid_network_player_info_get_player_team)) return err;
    }
    if (int err = resolve_class_const(env, loader, "io/netty/util/concurrent/GenericFutureListener", &mappings->cls_netty_generic_future_listener)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"is", "net/minecraft/network/play/client/C16PacketClientStatus", "ig", "net/minecraft/network/play/client/C16PacketClientStatus"}, &mappings->cls_c16_packet_client_status)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c16_packet_client_status, ver, flavor, {{{"a", "Lit;"}, {"status", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}, {"field_149437_a", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}}, {{"a", "Lig$a;"}, {"status", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}, {"field_149437_a", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}}}, &mappings->fid_c16_packet_status)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_c16_packet_client_status, ver, flavor, {{{"<init>", "(Lit;)V"}, {"<init>", "(Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;)V"}, {"<init>", "(Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;)V"}}, {{"<init>", "(Lig$a;)V"}, {"<init>", "(Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;)V"}, {"<init>", "(Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;)V"}}}, &mappings->mid_c16_packet_init)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"it", "net/minecraft/network/play/client/C16PacketClientStatus$EnumState", "ig$a", "net/minecraft/network/play/client/C16PacketClientStatus$EnumState"}, &mappings->cls_c16_enum_state)) return err;
    if (int err = resolve_static_field_2v(env, mappings->cls_c16_enum_state, ver, flavor, {{{"c", "Lit;"}, {"OPEN_INVENTORY_ACHIEVEMENT", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}, {"OPEN_INVENTORY_ACHIEVEMENT", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}}, {{"c", "Lig$a;"}, {"OPEN_INVENTORY_ACHIEVEMENT", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}, {"OPEN_INVENTORY_ACHIEVEMENT", "Lnet/minecraft/network/play/client/C16PacketClientStatus$EnumState;"}}}, &mappings->sfid_c16_open_inventory_achievement)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"iy", "net/minecraft/network/play/client/C0DPacketCloseWindow", "il", "net/minecraft/network/play/client/C0DPacketCloseWindow"}, &mappings->cls_c0d_packet_close_window)) return err;
    if (int err = resolve_method_const(env, mappings->cls_c0d_packet_close_window, "<init>", "(I)V", &mappings->mid_c0d_packet_init)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"aap", "net/minecraft/inventory/ContainerPlayer", "xy", "net/minecraft/inventory/ContainerPlayer"}, &mappings->cls_container_player)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_container_player, ver, flavor, {{{"a", "Laae;"}, {"craftMatrix", "Lnet/minecraft/inventory/InventoryCrafting;"}, {"field_75181_e", "Lnet/minecraft/inventory/InventoryCrafting;"}}, {{"a", "Lxp;"}, {"craftMatrix", "Lnet/minecraft/inventory/InventoryCrafting;"}, {"field_75181_e", "Lnet/minecraft/inventory/InventoryCrafting;"}}}, &mappings->fid_container_player_craft_matrix)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bdw", "net/minecraft/client/gui/GuiScreen", "axu", "net/minecraft/client/gui/GuiScreen"}, &mappings->cls_gui_screen)) return err;
    if (int err = resolve_method_2v(env, mappings->cls_gui_screen, ver, flavor, {{{"a", "(CI)V"}, {"keyTyped", "(CI)V"}, {"func_73869_a", "(CI)V"}}, {{"a", "(CI)V"}, {"keyTyped", "(CI)V"}, {"func_73869_a", "(CI)V"}}}, &mappings->mid_gui_screen_key_typed)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"bfi", "net/minecraft/client/gui/inventory/GuiChest", "ayr", "net/minecraft/client/gui/inventory/GuiChest"}, &mappings->cls_gui_chest)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_gui_chest, ver, flavor, {{{"w", "Lrb;"}, {"lowerChestInventory", "Lnet/minecraft/inventory/IInventory;"}, {"field_147015_w", "Lnet/minecraft/inventory/IInventory;"}}, {{"w", "Log;"}, {"lowerChestInventory", "Lnet/minecraft/inventory/IInventory;"}, {"field_147015_w", "Lnet/minecraft/inventory/IInventory;"}}}, &mappings->fid_gui_chest_lower_chest_inventory)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"rh", "net/minecraft/inventory/InventoryBasic", "oq", "net/minecraft/inventory/InventoryBasic"}, &mappings->cls_inventory_basic)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_inventory_basic, ver, flavor, {{{"a", "Ljava/lang/String;"}, {"inventoryTitle", "Ljava/lang/String;"}, {"field_70483_a", "Ljava/lang/String;"}}, {{"a", "Ljava/lang/String;"}, {"inventoryTitle", "Ljava/lang/String;"}, {"field_70483_a", "Ljava/lang/String;"}}}, &mappings->fid_inventory_basic_title)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"dd", "net/minecraft/util/StatCollector", "di", "net/minecraft/util/StatCollector"}, &mappings->cls_stat_collector)) return err;
    if (int err = resolve_static_method_2v(env, mappings->cls_stat_collector, ver, flavor, {{{"a", "(Ljava/lang/String;)Ljava/lang/String;"}, {"translateToLocal", "(Ljava/lang/String;)Ljava/lang/String;"}, {"func_74838_a", "(Ljava/lang/String;)Ljava/lang/String;"}}, {{"a", "(Ljava/lang/String;)Ljava/lang/String;"}, {"translateToLocal", "(Ljava/lang/String;)Ljava/lang/String;"}, {"func_74838_a", "(Ljava/lang/String;)Ljava/lang/String;"}}}, &mappings->smid_stat_collector_translate_to_local)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"ir", "net/minecraft/network/play/client/C01PacketChatMessage", "ie", "net/minecraft/network/play/client/C01PacketChatMessage"}, &mappings->cls_c01_packet_chat_message)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c01_packet_chat_message, ver, flavor, {{{"a", "Ljava/lang/String;"}, {"message", "Ljava/lang/String;"}, {"field_149440_a", "Ljava/lang/String;"}}, {{"a", "Ljava/lang/String;"}, {"message", "Ljava/lang/String;"}, {"field_149440_a", "Ljava/lang/String;"}}}, &mappings->fid_c01_message)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"iq", "net/minecraft/network/play/client/C14PacketTabComplete", "id", "net/minecraft/network/play/client/C14PacketTabComplete"}, &mappings->cls_c14_packet_tab_complete)) return err;
    if (int err = resolve_field_2v(env, mappings->cls_c14_packet_tab_complete, ver, flavor, {{{"a", "Ljava/lang/String;"}, {"message", "Ljava/lang/String;"}, {"field_149420_a", "Ljava/lang/String;"}}, {{"a", "Ljava/lang/String;"}, {"message", "Ljava/lang/String;"}, {"field_149420_a", "Ljava/lang/String;"}}}, &mappings->fid_c14_message)) return err;
    if (int err = resolve_class_2v(env, loader, ver, is_deobf, {"hb", "net/minecraft/network/play/server/S2APacketParticles", "gr", "net/minecraft/network/play/server/S2APacketParticles"}, &mappings->cls_s2a_packet_particles)) return err;

    return kMappingsOk;
}

// ============================================================================
// sub_18003FE20 - Delete all global JNI class references in Mappings
// ============================================================================
void destroy_mappings(Mappings* mappings, JNIEnv* env) noexcept {
    if (mappings == nullptr || env == nullptr) {
        return;
    }
    destroy_class_loader_handle(&mappings->client_class_loader, env);
    delete_global_class_ref(env, &mappings->cls_netty_channel);
    delete_global_class_ref(env, &mappings->cls_player_controller_mp);
    delete_global_class_ref(env, &mappings->cls_c0e_packet_click_window);
    delete_global_class_ref(env, &mappings->cls_nbt_tag_list);
    delete_global_class_ref(env, &mappings->cls_nbt_tag_compound);
    delete_global_class_ref(env, &mappings->cls_entity_arrow);
    delete_global_class_ref(env, &mappings->cls_potion_effect);
    delete_global_class_ref(env, &mappings->cls_network_player_info);
    delete_global_class_ref(env, &mappings->cls_netty_generic_future_listener);
    delete_global_class_ref(env, &mappings->cls_c16_packet_client_status);
    delete_global_class_ref(env, &mappings->cls_c16_enum_state);
    delete_global_class_ref(env, &mappings->cls_c0d_packet_close_window);
    delete_global_class_ref(env, &mappings->cls_container_player);
    delete_global_class_ref(env, &mappings->cls_gui_screen);
    delete_global_class_ref(env, &mappings->cls_gui_chest);
    delete_global_class_ref(env, &mappings->cls_inventory_basic);
    delete_global_class_ref(env, &mappings->cls_netty_channel_pipeline);
    delete_global_class_ref(env, &mappings->cls_java_lang_enum);
    delete_global_class_ref(env, &mappings->cls_minecraft);
    delete_global_class_ref(env, &mappings->cls_entity);
    delete_global_class_ref(env, &mappings->cls_world);
    delete_global_class_ref(env, &mappings->cls_timer);
    delete_global_class_ref(env, &mappings->cls_game_settings);
    delete_global_class_ref(env, &mappings->cls_moving_object_position);
    delete_global_class_ref(env, &mappings->cls_iblock_state);
    delete_global_class_ref(env, &mappings->cls_block);
    delete_global_class_ref(env, &mappings->cls_block_pos);
    delete_global_class_ref(env, &mappings->cls_key_binding);
    delete_global_class_ref(env, &mappings->cls_java_util_list);
    delete_global_class_ref(env, &mappings->cls_java_util_map);
    delete_global_class_ref(env, &mappings->cls_lwjgl_keyboard);
    delete_global_class_ref(env, &mappings->cls_lwjgl_mouse);
    delete_global_class_ref(env, &mappings->cls_lwjgl_display);
    delete_global_class_ref(env, &mappings->cls_java_lang_object);
    delete_global_class_ref(env, &mappings->cls_secure_class_loader);
    delete_global_class_ref(env, &mappings->cls_axis_aligned_bb);
    delete_global_class_ref(env, &mappings->cls_gui_container);
    delete_global_class_ref(env, &mappings->cls_entity_living_base);
    delete_global_class_ref(env, &mappings->cls_entity_player);
    delete_global_class_ref(env, &mappings->cls_inventory_player);
    delete_global_class_ref(env, &mappings->cls_item_stack);
    delete_global_class_ref(env, &mappings->cls_slot);
    delete_global_class_ref(env, &mappings->cls_iinventory);
    delete_global_class_ref(env, &mappings->cls_network_manager);
    delete_global_class_ref(env, &mappings->cls_c03_packet_player);
    delete_global_class_ref(env, &mappings->cls_c07_packet_player_digging);
    delete_global_class_ref(env, &mappings->cls_c08_packet_player_block_placement);
    delete_global_class_ref(env, &mappings->cls_tile_entity);
    delete_global_class_ref(env, &mappings->cls_s12_packet_entity_velocity);
    delete_global_class_ref(env, &mappings->cls_render_manager);
    delete_global_class_ref(env, &mappings->cls_s14_packet_entity);
    delete_global_class_ref(env, &mappings->cls_s18_packet_entity_teleport);
    delete_global_class_ref(env, &mappings->cls_s19_packet_entity_status);
    delete_global_class_ref(env, &mappings->cls_s29_packet_sound_effect);
    delete_global_class_ref(env, &mappings->cls_s06_packet_update_health);
    delete_global_class_ref(env, &mappings->cls_entity_other_player_mp);
    delete_global_class_ref(env, &mappings->cls_gui_inventory);
    delete_global_class_ref(env, &mappings->cls_entity_player_sp);
    delete_global_class_ref(env, &mappings->cls_entity_renderer);
    delete_global_class_ref(env, &mappings->cls_framebuffer);
    delete_global_class_ref(env, &mappings->cls_render_global);
    delete_global_class_ref(env, &mappings->cls_active_render_info);
    delete_global_class_ref(env, &mappings->cls_item);
    delete_global_class_ref(env, &mappings->cls_vec3);
    delete_global_class_ref(env, &mappings->cls_extended_block_storage);
    delete_global_class_ref(env, &mappings->cls_world_client);
    delete_global_class_ref(env, &mappings->cls_chunk_provider_client);
    delete_global_class_ref(env, &mappings->cls_entity_item);
    delete_global_class_ref(env, &mappings->cls_s3f_packet_custom_payload);
    delete_global_class_ref(env, &mappings->cls_packet_buffer);
    delete_global_class_ref(env, &mappings->cls_java_util_uuid);
    delete_global_class_ref(env, &mappings->cls_chat_component_text);
    delete_global_class_ref(env, &mappings->cls_stat_collector);
    delete_global_class_ref(env, &mappings->cls_c01_packet_chat_message);
    delete_global_class_ref(env, &mappings->cls_c14_packet_tab_complete);
    delete_global_class_ref(env, &mappings->cls_score_player_team);
    delete_global_class_ref(env, &mappings->cls_net_handler_play_client);
    delete_global_class_ref(env, &mappings->cls_ichat_component);
    delete_global_class_ref(env, &mappings->cls_ichat_component_serializer);
    delete_global_class_ref(env, &mappings->cls_renderer_living_entity);
    delete_global_class_ref(env, &mappings->cls_font_renderer);
    delete_global_class_ref(env, &mappings->cls_gl_state_manager);
    delete_global_class_ref(env, &mappings->cls_c02_packet_use_entity);
    delete_global_class_ref(env, &mappings->cls_c0b_packet_entity_action);
    delete_global_class_ref(env, &mappings->cls_s08_packet_player_pos_look);
    delete_global_class_ref(env, &mappings->cls_s27_packet_explosion);
    delete_global_class_ref(env, &mappings->cls_s00_packet_keep_alive);
    delete_global_class_ref(env, &mappings->cls_c00_packet_keep_alive);
    delete_global_class_ref(env, &mappings->cls_potion);
    delete_global_class_ref(env, &mappings->cls_render_player);
    delete_global_class_ref(env, &mappings->cls_render_item);
    delete_global_class_ref(env, &mappings->cls_render_helper);
    delete_global_class_ref(env, &mappings->cls_entity_list);
    delete_global_class_ref(env, &mappings->cls_chunk);
    delete_global_class_ref(env, &mappings->cls_s2a_packet_particles);
}

} // namespace slinky::minecraft
