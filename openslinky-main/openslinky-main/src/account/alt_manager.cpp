#include "account/alt_manager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "events/events.hpp"
#include "minecraft/mappings.hpp"
#include "util/filesystem.hpp"
#include "util/random.hpp"

#ifdef SLINKY_ALT_DEBUG
#include <cstdarg>
#define ALT_LOG(...) std::fprintf(stderr, __VA_ARGS__)
#else
#define ALT_LOG(...) ((void)0)
#endif

namespace slinky::account {

namespace {

constexpr int kErrorInvalidUsername = 7001;
constexpr int kErrorGameUnavailable = 7002;
constexpr int kErrorSessionApply = 7003;
constexpr int kErrorSaveFailed = 7004;

constexpr size_t kUsernameMin = 3u;
constexpr size_t kUsernameMax = 16u;

char g_username[kUsernameMax + 1u]{};
bool g_initialized = false;
bool g_saved_on_disk = false;

bool is_username_char(char ch) noexcept {
    const auto uch = static_cast<uint8_t>(ch);
    return (uch >= static_cast<uint8_t>('A') && uch <= static_cast<uint8_t>('Z')) ||
           (uch >= static_cast<uint8_t>('a') && uch <= static_cast<uint8_t>('z')) ||
           (uch >= static_cast<uint8_t>('0') && uch <= static_cast<uint8_t>('9')) ||
           ch == '_';
}

bool is_valid_username(const char* name) noexcept {
    if (name == nullptr) {
        return false;
    }
    const size_t len = std::strlen(name);
    if (len < kUsernameMin || len > kUsernameMax) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!is_username_char(name[i])) {
            return false;
        }
    }
    return true;
}

int build_account_path(fs::WidePath* path) noexcept {
    int err = fs::init_user_profile_path(path);
    if (err == 0) {
        err = fs::append_path(path, ".slinky");
    }
    if (err == 0) {
        err = fs::create_directory(path);
    }
    if (err == 0) {
        err = fs::append_path(path, "account.txt");
    }
    return err;
}

void load_username_from_disk() noexcept {
    fs::WidePath path{};
    if (build_account_path(&path) != 0) {
        fs::free_path(&path);
        return;
    }

    char* text = nullptr;
    if (fs::read_text_file(&path, &text) == 0 && text != nullptr) {
        char candidate[kUsernameMax + 1u]{};
        size_t out = 0;
        for (const char* p = text; *p != '\0' && out < kUsernameMax; ++p) {
            const char ch = *p;
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                if (out != 0u) {
                    break;
                }
                continue;
            }
            if (!is_username_char(ch)) {
                out = 0;
                break;
            }
            candidate[out++] = ch;
        }
        candidate[out] = '\0';
        if (is_valid_username(candidate)) {
            std::memcpy(g_username, candidate, out + 1u);
            g_saved_on_disk = true;
        }
        std::free(text);
    }
    fs::free_path(&path);
}

bool save_username_to_disk(const char* name) noexcept {
    fs::WidePath path{};
    int err = build_account_path(&path);
    if (err == 0) {
        err = fs::write_text_file(&path, name, true);
    }
    fs::free_path(&path);
    return err == 0;
}

// ---------------------------------------------------------------------------
// Java reflection helpers. All classes/methods are looked up on demand inside
// a local frame; this runs only on client init and when the user applies a name.
// ---------------------------------------------------------------------------

struct Reflection {
    jclass cls_class = nullptr;
    jclass cls_field = nullptr;
    jclass cls_constructor = nullptr;
    jclass cls_string = nullptr;
    jclass cls_object = nullptr;

    jmethodID mid_declared_fields = nullptr;
    jmethodID mid_declared_constructors = nullptr;
    jmethodID mid_class_get_declared_field = nullptr;
    jmethodID mid_field_get_type = nullptr;
    jmethodID mid_field_get_name = nullptr;
    jmethodID mid_ctor_get_parameter_types = nullptr;
    jmethodID mid_ctor_new_instance = nullptr;
    jmethodID mid_class_get_name = nullptr;
};

jclass find_local_class(JNIEnv* env, const char* name) noexcept {
    jclass cls = env->FindClass(name);
    if (cls == nullptr) {
        env->ExceptionClear();
    }
    return cls;
}

jmethodID find_method(JNIEnv* env, jclass cls, const char* name, const char* signature) noexcept {
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID mid = env->GetMethodID(cls, name, signature);
    if (mid == nullptr) {
        env->ExceptionClear();
    }
    return mid;
}

bool load_reflection(JNIEnv* env, Reflection* ref) noexcept {
    ref->cls_class = find_local_class(env, "java/lang/Class");
    ref->cls_field = find_local_class(env, "java/lang/reflect/Field");
    ref->cls_constructor = find_local_class(env, "java/lang/reflect/Constructor");
    ref->cls_string = find_local_class(env, "java/lang/String");
    ref->cls_object = find_local_class(env, "java/lang/Object");
    if (ref->cls_class == nullptr || ref->cls_field == nullptr || ref->cls_constructor == nullptr ||
        ref->cls_string == nullptr || ref->cls_object == nullptr) {
        return false;
    }

    ref->mid_declared_fields = find_method(
        env, ref->cls_class, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
    ref->mid_declared_constructors = find_method(
        env, ref->cls_class, "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;");
    ref->mid_class_get_declared_field = find_method(
        env, ref->cls_class, "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    ref->mid_field_get_type = find_method(
        env, ref->cls_field, "getType", "()Ljava/lang/Class;");
    ref->mid_field_get_name = find_method(
        env, ref->cls_field, "getName", "()Ljava/lang/String;");
    ref->mid_ctor_get_parameter_types = find_method(
        env, ref->cls_constructor, "getParameterTypes", "()[Ljava/lang/Class;");
    ref->mid_ctor_new_instance = find_method(
        env, ref->cls_constructor, "newInstance", "([Ljava/lang/Object;)Ljava/lang/Object;");
    ref->mid_class_get_name = find_method(
        env, ref->cls_class, "getName", "()Ljava/lang/String;");

    return ref->mid_declared_fields != nullptr &&
           ref->mid_declared_constructors != nullptr &&
           ref->mid_class_get_declared_field != nullptr &&
           ref->mid_field_get_type != nullptr &&
           ref->mid_field_get_name != nullptr &&
           ref->mid_ctor_get_parameter_types != nullptr &&
           ref->mid_ctor_new_instance != nullptr &&
           ref->mid_class_get_name != nullptr;
}

bool class_name_equals(JNIEnv* env, const Reflection& ref, jclass cls, const char* expected) noexcept {
    if (cls == nullptr) {
        return false;
    }
    jstring name = static_cast<jstring>(env->CallObjectMethod(cls, ref.mid_class_get_name));
    if (name == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    const char* utf = env->GetStringUTFChars(name, nullptr);
    const bool equal = utf != nullptr && std::strcmp(utf, expected) == 0;
    if (utf != nullptr) {
        env->ReleaseStringUTFChars(name, utf);
    }
    env->DeleteLocalRef(name);
    return equal;
}

jobject find_declared_field(
    JNIEnv* env,
    const Reflection& ref,
    jclass cls,
    const char* name) noexcept
{
    jstring jname = env->NewStringUTF(name);
    if (jname == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject field = env->CallObjectMethod(cls, ref.mid_class_get_declared_field, jname);
    env->DeleteLocalRef(jname);
    if (field == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return field;
}

// Finds Minecraft's Session field and returns both the field object and the
// Session class. Vanilla clients use obfuscated type names, so those are
// looked up by their known per-version field names first; deobfuscated clients
// (MCP/Lunar/Forge) fall back to matching the type name.
jobject find_session_field(
    JNIEnv* env,
    const Reflection& ref,
    jclass minecraft_class,
    const mc::Mappings* mappings,
    jclass* out_session_class) noexcept
{
    *out_session_class = nullptr;

    if (mappings != nullptr && mappings->mapping_flavor == 0) {
        // 1.7.10: Minecraft.session = "S" (type bbs), 1.8.9: "ae" (type avm).
        const char* names[2];
        if (mappings->version == 0) {
            names[0] = "S";
            names[1] = "ae";
        } else {
            names[0] = "ae";
            names[1] = "S";
        }
        for (const char* name : names) {
            jobject field = find_declared_field(env, ref, minecraft_class, name);
            if (field == nullptr) {
                continue;
            }
            auto* type = static_cast<jclass>(env->CallObjectMethod(field, ref.mid_field_get_type));
            if (type != nullptr && !env->ExceptionCheck()) {
                *out_session_class = type;
                return field;
            }
            env->ExceptionClear();
            if (type != nullptr) {
                env->DeleteLocalRef(type);
            }
            env->DeleteLocalRef(field);
        }
    }

    auto* fields = static_cast<jobjectArray>(
        env->CallObjectMethod(minecraft_class, ref.mid_declared_fields));
    if (fields == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject found = nullptr;
    const jsize count = env->GetArrayLength(fields);
    for (jsize i = 0; i < count && found == nullptr; ++i) {
        jobject field = env->GetObjectArrayElement(fields, i);
        if (field == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }

        auto* type = static_cast<jclass>(env->CallObjectMethod(field, ref.mid_field_get_type));
        if (type != nullptr && !env->ExceptionCheck()) {
            jstring type_name = static_cast<jstring>(env->CallObjectMethod(type, ref.mid_class_get_name));
            if (type_name != nullptr && !env->ExceptionCheck()) {
                const char* utf = env->GetStringUTFChars(type_name, nullptr);
                if (utf != nullptr) {
                    const bool is_session =
                        std::strcmp(utf, "net.minecraft.util.Session") == 0 ||
                        std::strstr(utf, ".Session") != nullptr;
                    env->ReleaseStringUTFChars(type_name, utf);
                    if (is_session) {
                        found = env->NewLocalRef(field);
                        *out_session_class = static_cast<jclass>(env->NewLocalRef(type));
                    }
                }
                env->DeleteLocalRef(type_name);
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(type);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        env->DeleteLocalRef(field);
    }

    env->DeleteLocalRef(fields);
    return found;
}

void make_offline_uuid(char out[37]) noexcept {
    uint8_t bytes[16]{};
    for (size_t i = 0; i < sizeof(bytes); i += 4u) {
        const uint32_t value = random::next_u32();
        bytes[i + 0u] = static_cast<uint8_t>(value & 0xFFu);
        bytes[i + 1u] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        bytes[i + 2u] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        bytes[i + 3u] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3Fu) | 0x80u);

    std::snprintf(
        out,
        37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
}

bool apply_session_name(
    JNIEnv* env,
    jobject minecraft,
    const mc::Mappings* mappings,
    const char* name) noexcept
{
    Reflection ref{};
    if (!load_reflection(env, &ref)) {
        ALT_LOG("alt: load_reflection failed\n");
        return false;
    }
    ALT_LOG("alt: reflection loaded\n");

    jclass minecraft_class = env->GetObjectClass(minecraft);
    if (minecraft_class == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass session_class = nullptr;
    jobject session_field = find_session_field(
        env, ref, minecraft_class, mappings, &session_class);
    env->DeleteLocalRef(minecraft_class);
    ALT_LOG("alt: session_field=%p session_class=%p\n",
        static_cast<void*>(session_field), static_cast<void*>(session_class));
    if (session_field == nullptr || session_class == nullptr) {
        if (session_class != nullptr) {
            env->DeleteLocalRef(session_class);
        }
        return false;
    }

    // 1.7.10 and 1.8.9 both use (String username, String playerID,
    // String token, String sessionType); older/remapped builds may expose
    // 3- or 2-argument variants. Prefer the highest all-String arity.
    jobject chosen_ctor = nullptr;
    jsize chosen_arity = 0;
    auto* constructors = static_cast<jobjectArray>(
        env->CallObjectMethod(session_class, ref.mid_declared_constructors));
    if (constructors != nullptr && !env->ExceptionCheck()) {
        const jsize ctor_count = env->GetArrayLength(constructors);
        ALT_LOG("alt: constructors=%d\n", static_cast<int>(ctor_count));
        for (jsize i = 0; i < ctor_count; ++i) {
            jobject ctor = env->GetObjectArrayElement(constructors, i);
            if (ctor == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                continue;
            }

            auto* params = static_cast<jobjectArray>(
                env->CallObjectMethod(ctor, ref.mid_ctor_get_parameter_types));
            bool all_strings = params != nullptr && !env->ExceptionCheck();
            jsize arity = 0;
            if (all_strings) {
                arity = env->GetArrayLength(params);
                all_strings = (arity >= 2 && arity <= 4);
                for (jsize p = 0; all_strings && p < arity; ++p) {
                    auto* param = static_cast<jclass>(env->GetObjectArrayElement(params, p));
                    if (param == nullptr || !class_name_equals(env, ref, param, "java.lang.String")) {
                        all_strings = false;
                    }
                    if (param != nullptr) {
                        env->DeleteLocalRef(param);
                    }
                }
            }
            if (params != nullptr) {
                env->DeleteLocalRef(params);
            }

            ALT_LOG("alt: ctor %d arity=%d all_strings=%d\n",
                static_cast<int>(i), static_cast<int>(arity), all_strings ? 1 : 0);
            const bool better =
                all_strings && (chosen_ctor == nullptr || arity > chosen_arity);
            if (better) {
                if (chosen_ctor != nullptr) {
                    env->DeleteLocalRef(chosen_ctor);
                }
                chosen_ctor = env->NewLocalRef(ctor);
                chosen_arity = arity;
            }
            env->DeleteLocalRef(ctor);
        }
        env->DeleteLocalRef(constructors);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    ALT_LOG("alt: chosen arity=%d\n", static_cast<int>(chosen_arity));
    if (chosen_ctor == nullptr) {
        env->DeleteLocalRef(session_field);
        env->DeleteLocalRef(session_class);
        return false;
    }

    char uuid[37]{};
    make_offline_uuid(uuid);

    jstring j_name = env->NewStringUTF(name);
    jstring j_uuid = env->NewStringUTF(uuid);
    jstring j_empty = env->NewStringUTF("");
    jstring j_legacy = env->NewStringUTF("legacy");
    jobjectArray args = env->NewObjectArray(chosen_arity, ref.cls_object, nullptr);
    bool args_ok = j_name != nullptr && j_uuid != nullptr && j_empty != nullptr &&
                   j_legacy != nullptr && args != nullptr;
    if (args_ok) {
        env->SetObjectArrayElement(args, 0, j_name);
        env->SetObjectArrayElement(args, 1, chosen_arity >= 3 ? static_cast<jobject>(j_uuid)
                                                             : static_cast<jobject>(j_empty));
        if (chosen_arity >= 3) {
            env->SetObjectArrayElement(args, 2, j_empty);
        }
        if (chosen_arity >= 4) {
            env->SetObjectArrayElement(args, 3, j_legacy);
        }
        args_ok = !env->ExceptionCheck();
    }

    jobject new_session = nullptr;
    ALT_LOG("alt: args_ok=%d\n", args_ok ? 1 : 0);
    if (args_ok) {
        new_session = env->CallObjectMethod(chosen_ctor, ref.mid_ctor_new_instance, args);
        if (new_session == nullptr || env->ExceptionCheck()) {
            ALT_LOG("alt: newInstance threw\n");
            env->ExceptionClear();
            new_session = nullptr;
        }
    }
    ALT_LOG("alt: new_session=%p\n", static_cast<void*>(new_session));

    bool applied = false;
    if (new_session != nullptr) {
        // JNI field writes bypass Java access checks. Reflection's Field.set
        // is rejected on modern JDKs when called from an attached native
        // thread (no Java caller frame), while SetObjectField always works.
        jfieldID session_fid = env->FromReflectedField(session_field);
        if (session_fid != nullptr) {
            env->SetObjectField(minecraft, session_fid, new_session);
            applied = !env->ExceptionCheck();
            if (!applied) {
                ALT_LOG("alt: SetObjectField threw\n");
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
            ALT_LOG("alt: FromReflectedField failed\n");
        }
        env->DeleteLocalRef(new_session);
    }

    ALT_LOG("alt: applied=%d\n", applied ? 1 : 0);
    if (args != nullptr) {
        env->DeleteLocalRef(args);
    }
    if (j_name != nullptr) {
        env->DeleteLocalRef(j_name);
    }
    if (j_uuid != nullptr) {
        env->DeleteLocalRef(j_uuid);
    }
    if (j_empty != nullptr) {
        env->DeleteLocalRef(j_empty);
    }
    if (j_legacy != nullptr) {
        env->DeleteLocalRef(j_legacy);
    }
    env->DeleteLocalRef(chosen_ctor);
    env->DeleteLocalRef(session_field);
    env->DeleteLocalRef(session_class);
    return applied;
}

bool read_session_name(
    JNIEnv* env,
    jobject minecraft,
    const mc::Mappings* mappings,
    char* out,
    size_t out_size) noexcept
{
    if (out == nullptr || out_size == 0u) {
        return false;
    }
    out[0] = '\0';

    Reflection ref{};
    if (!load_reflection(env, &ref)) {
        return false;
    }

    jclass minecraft_class = env->GetObjectClass(minecraft);
    if (minecraft_class == nullptr) {
        env->ExceptionClear();
        return false;
    }

    jclass session_class = nullptr;
    jobject session_field = find_session_field(
        env, ref, minecraft_class, mappings, &session_class);
    env->DeleteLocalRef(minecraft_class);
    if (session_field == nullptr) {
        return false;
    }

    jfieldID session_fid = env->FromReflectedField(session_field);
    jobject session = nullptr;
    if (session_fid != nullptr) {
        session = env->GetObjectField(minecraft, session_fid);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(session_field);
    if (session == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (session_class != nullptr) {
            env->DeleteLocalRef(session_class);
        }
        return false;
    }

    bool found = false;
    const char* getters[] = {"getUsername", "func_111285_a"};
    for (const char* getter : getters) {
        jmethodID mid = find_method(env, session_class, getter, "()Ljava/lang/String;");
        if (mid == nullptr) {
            continue;
        }
        auto* value = static_cast<jstring>(env->CallObjectMethod(session, mid));
        if (value != nullptr && !env->ExceptionCheck()) {
            const char* utf = env->GetStringUTFChars(value, nullptr);
            if (utf != nullptr) {
                if (is_valid_username(utf)) {
                    std::snprintf(out, out_size, "%s", utf);
                    found = true;
                }
                env->ReleaseStringUTFChars(value, utf);
            }
            env->DeleteLocalRef(value);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (found) {
            break;
        }
    }

    if (!found) {
        auto* fields = static_cast<jobjectArray>(
            env->CallObjectMethod(session_class, ref.mid_declared_fields));
        if (fields != nullptr && !env->ExceptionCheck()) {
            const jsize count = env->GetArrayLength(fields);
            for (jsize i = 0; i < count && !found; ++i) {
                jobject field = env->GetObjectArrayElement(fields, i);
                if (field == nullptr || env->ExceptionCheck()) {
                    env->ExceptionClear();
                    continue;
                }

                auto* type = static_cast<jclass>(env->CallObjectMethod(field, ref.mid_field_get_type));
                const bool is_string = type != nullptr && !env->ExceptionCheck() &&
                                       class_name_equals(env, ref, type, "java.lang.String");
                if (type != nullptr) {
                    env->DeleteLocalRef(type);
                } else if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }

                if (is_string) {
                    jfieldID fid = env->FromReflectedField(field);
                    auto* value = fid != nullptr
                        ? static_cast<jstring>(env->GetObjectField(session, fid))
                        : nullptr;
                    if (value != nullptr && !env->ExceptionCheck()) {
                        const char* utf = env->GetStringUTFChars(value, nullptr);
                        if (utf != nullptr) {
                            if (is_valid_username(utf)) {
                                std::snprintf(out, out_size, "%s", utf);
                                found = true;
                            }
                            env->ReleaseStringUTFChars(value, utf);
                        }
                        env->DeleteLocalRef(value);
                    } else if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                    }
                }
                env->DeleteLocalRef(field);
            }
            env->DeleteLocalRef(fields);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(session);
    if (session_class != nullptr) {
        env->DeleteLocalRef(session_class);
    }
    return found;
}

jobject minecraft_instance(JNIEnv* env, const mc::Mappings* mappings) noexcept {
    if (mappings == nullptr || mappings->cls_minecraft == nullptr ||
        mappings->sfid_mc_the_minecraft == nullptr) {
        return nullptr;
    }
    jobject instance = env->GetStaticObjectField(
        mappings->cls_minecraft, mappings->sfid_mc_the_minecraft);
    if (instance == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return instance;
}

} // namespace

void initialize() noexcept {
    if (g_initialized) {
        return;
    }
    g_initialized = true;
    load_username_from_disk();
}

const char* get_username() noexcept {
    return g_username;
}

bool apply_saved_to_game(events::JniContext* jni) noexcept {
    initialize();
    if (jni == nullptr || jni->env == nullptr || jni->mappings == nullptr) {
        return false;
    }

    JNIEnv* env = jni->env;
    if (env->PushLocalFrame(128) < 0) {
        return false;
    }

    bool applied = false;
    jobject minecraft = minecraft_instance(env, jni->mappings);
    if (minecraft != nullptr) {
        if (g_username[0] == '\0') {
            // No alt was configured yet: show the launcher's current name but
            // leave its online session untouched.
            char current[kUsernameMax + 1u]{};
            if (read_session_name(env, minecraft, jni->mappings, current, sizeof(current))) {
                std::memcpy(g_username, current, std::strlen(current) + 1u);
            }
        }
        if (g_saved_on_disk && g_username[0] != '\0') {
            applied = apply_session_name(env, minecraft, jni->mappings, g_username);
        }
    }

    env->PopLocalFrame(nullptr);
    return applied;
}

int set_username(const char* name, events::JniContext* jni) noexcept {
    if (!is_valid_username(name)) {
        return kErrorInvalidUsername;
    }
    initialize();

    const bool saved = save_username_to_disk(name);
    std::snprintf(g_username, sizeof(g_username), "%s", name);
    g_saved_on_disk = saved;

    if (jni == nullptr || jni->env == nullptr || jni->mappings == nullptr) {
        return saved ? kErrorGameUnavailable : kErrorSaveFailed;
    }

    if (jni->env->PushLocalFrame(128) < 0) {
        return saved ? kErrorSessionApply : kErrorSaveFailed;
    }

    jobject minecraft = minecraft_instance(jni->env, jni->mappings);
    const bool applied = minecraft != nullptr &&
                         apply_session_name(jni->env, minecraft, jni->mappings, name);
    jni->env->PopLocalFrame(nullptr);

    if (!saved) {
        return kErrorSaveFailed;
    }
    if (!applied) {
        return kErrorGameUnavailable;
    }
    return 0;
}

} // namespace slinky::account
