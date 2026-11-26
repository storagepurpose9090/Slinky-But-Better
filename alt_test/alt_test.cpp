#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <jni.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "account/alt_manager.hpp"
#include "events/events.hpp"
#include "minecraft/mappings.hpp"
#include "util/random.hpp"

using CreateJavaVMFn = jint(JNICALL*)(JavaVM**, void**, void*);

static const char* session_username(JNIEnv* env, jobject mc, jclass mc_cls) {
    static char buffer[128];
    buffer[0] = '\0';

    jmethodID get_session = env->GetMethodID(
        mc_cls, "getSession", "()Lnet/minecraft/util/Session;");
    if (get_session == nullptr) {
        env->ExceptionClear();
        return buffer;
    }
    jobject session = env->CallObjectMethod(mc, get_session);
    if (session == nullptr) {
        env->ExceptionClear();
        return buffer;
    }
    jclass session_cls = env->GetObjectClass(session);
    jmethodID get_username = env->GetMethodID(
        session_cls, "getUsername", "()Ljava/lang/String;");
    if (get_username != nullptr) {
        auto* name = static_cast<jstring>(env->CallObjectMethod(session, get_username));
        if (name != nullptr) {
            const char* utf = env->GetStringUTFChars(name, nullptr);
            if (utf != nullptr) {
                std::snprintf(buffer, sizeof(buffer), "%s", utf);
                env->ReleaseStringUTFChars(name, utf);
            }
            env->DeleteLocalRef(name);
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(session_cls);
    env->DeleteLocalRef(session);
    return buffer;
}

static bool session_field_dump(JNIEnv* env, jobject mc, jclass mc_cls, const char* label) {
    std::printf("%s username=%s\n", label, session_username(env, mc, mc_cls));

    jmethodID get_session = env->GetMethodID(
        mc_cls, "getSession", "()Lnet/minecraft/util/Session;");
    jobject session = env->CallObjectMethod(mc, get_session);
    jclass session_cls = env->GetObjectClass(session);

    const char* getters[] = {"getToken", "getSessionID", "getPlayerID", "getSessionType"};
    for (const char* getter : getters) {
        jmethodID mid = env->GetMethodID(session_cls, getter, "()Ljava/lang/String;");
        if (mid == nullptr) {
            env->ExceptionClear();
            continue;
        }
        auto* value = static_cast<jstring>(env->CallObjectMethod(session, mid));
        if (value != nullptr) {
            const char* utf = env->GetStringUTFChars(value, nullptr);
            if (utf != nullptr) {
                std::printf("  %s=%s\n", getter, utf);
                env->ReleaseStringUTFChars(value, utf);
            }
            env->DeleteLocalRef(value);
        }
    }

    env->DeleteLocalRef(session_cls);
    env->DeleteLocalRef(session);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 6) {
        std::fwprintf(stderr, L"usage: alt_test <jvm.dll> <classpath> <userprofile> <set|verify> <name>\n");
        return 2;
    }

    const wchar_t* jvm_path = argv[1];
    const wchar_t* classpath = argv[2];
    const wchar_t* userprofile = argv[3];
    const bool do_set = std::wcscmp(argv[4], L"set") == 0;
    const wchar_t* name = argv[5];

    _wputenv_s(L"USERPROFILE", userprofile);
    slinky::random::seed_rng();

    HMODULE jvm_module = LoadLibraryW(jvm_path);
    if (jvm_module == nullptr) {
        std::printf("LoadLibrary(jvm) failed: %lu\n", GetLastError());
        return 3;
    }
    auto create_vm = reinterpret_cast<CreateJavaVMFn>(
        reinterpret_cast<void*>(GetProcAddress(jvm_module, "JNI_CreateJavaVM")));
    if (create_vm == nullptr) {
        std::printf("JNI_CreateJavaVM not found\n");
        return 3;
    }

    char classpath_opt[4096]{};
    std::snprintf(
        classpath_opt, sizeof(classpath_opt), "-Djava.class.path=%ls", classpath);

    JavaVMOption options[2]{};
    options[0].optionString = classpath_opt;
    options[1].optionString = const_cast<char*>("-Xmx128m");
    JavaVMInitArgs vm_args{};
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = 2;
    vm_args.options = options;

    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    if (create_vm(&vm, reinterpret_cast<void**>(&env), &vm_args) != JNI_OK || env == nullptr) {
        std::printf("JNI_CreateJavaVM failed\n");
        return 3;
    }

    jclass minecraft_class = env->FindClass("net/minecraft/client/Minecraft");
    if (minecraft_class == nullptr) {
        env->ExceptionClear();
        std::printf("FindClass(Minecraft) failed\n");
        return 4;
    }
    jfieldID instance_field = env->GetStaticFieldID(
        minecraft_class, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (instance_field == nullptr) {
        env->ExceptionClear();
        std::printf("GetStaticFieldID(theMinecraft) failed\n");
        return 4;
    }
    jobject minecraft = env->GetStaticObjectField(minecraft_class, instance_field);

    slinky::mc::Mappings mappings{};
    mappings.cls_minecraft = static_cast<jclass>(env->NewGlobalRef(minecraft_class));
    mappings.sfid_mc_the_minecraft = instance_field;

    slinky::events::JniContext jni{&mappings, env};

    slinky::account::initialize();
    if (do_set) {
        slinky::account::apply_saved_to_game(&jni);
    }
    std::printf("loaded_name=%s\n", slinky::account::get_username());

    int rc = 0;
    if (do_set) {
        char expected_initial[128]{};
        std::snprintf(expected_initial, sizeof(expected_initial), "LauncherName");
        std::printf("initial_ok=%d\n",
            std::strcmp(slinky::account::get_username(), expected_initial) == 0 ? 1 : 0);
        if (std::strcmp(slinky::account::get_username(), expected_initial) != 0) {
            rc = 1;
        }

        char expected[128]{};
        std::snprintf(expected, sizeof(expected), "%ls", name);
        const int err = slinky::account::set_username(expected, &jni);
        std::printf("set_result=%d\n", err);
        if (err != 0) {
            rc = 1;
        }
        if (std::strcmp(slinky::account::get_username(), expected) != 0) {
            std::printf("stored_name_mismatch\n");
            rc = 1;
        }
        if (std::strcmp(session_username(env, minecraft, minecraft_class), expected) != 0) {
            std::printf("session_name_mismatch\n");
            rc = 1;
        }
    } else {
        char expected[128]{};
        std::snprintf(expected, sizeof(expected), "%ls", name);
        if (std::strcmp(slinky::account::get_username(), expected) != 0) {
            std::printf("persisted_name_mismatch: got '%s' expected '%s'\n",
                slinky::account::get_username(), expected);
            rc = 1;
        }
        const bool applied = slinky::account::apply_saved_to_game(&jni);
        std::printf("apply_result=%d\n", applied ? 1 : 0);
        if (!applied) {
            rc = 1;
        }
        if (std::strcmp(session_username(env, minecraft, minecraft_class), expected) != 0) {
            std::printf("session_name_mismatch\n");
            rc = 1;
        }
    }

    session_field_dump(env, minecraft, minecraft_class, "session");

    char account_path[1024]{};
    std::snprintf(account_path, sizeof(account_path), "%ls\\.slinky\\account.txt", userprofile);
    FILE* fp = std::fopen(account_path, "rb");
    if (fp != nullptr) {
        char text[64]{};
        const size_t n = std::fread(text, 1, sizeof(text) - 1, fp);
        text[n] = '\0';
        std::printf("account_file=%s\n", text);
        std::fclose(fp);
    } else {
        std::printf("account_file_missing\n");
        rc = 1;
    }

    vm->DestroyJavaVM();
    return rc;
}
