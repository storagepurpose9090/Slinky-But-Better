#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <jni.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

#include "account/alt_manager.hpp"
#include "events/events.hpp"
#include "minecraft/mappings.hpp"
#include "util/random.hpp"

using CreateJavaVMFn = jint(JNICALL*)(JavaVM**, void**, void*);

static void call_string(JNIEnv* env, jobject obj, const char* name, char* out, size_t out_size) {
    out[0] = '\0';
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, name, "()Ljava/lang/String;");
    if (mid == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return;
    }
    auto* value = static_cast<jstring>(env->CallObjectMethod(obj, mid));
    if (value != nullptr) {
        const char* utf = env->GetStringUTFChars(value, nullptr);
        if (utf != nullptr) {
            std::snprintf(out, out_size, "%s", utf);
            env->ReleaseStringUTFChars(value, utf);
        }
        env->DeleteLocalRef(value);
    }
    env->DeleteLocalRef(cls);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fwprintf(stderr, L"usage: alt_vanilla_test <jvm.dll> <classpath> <userprofile>\n");
        return 2;
    }

    _wputenv_s(L"USERPROFILE", argv[3]);
    slinky::random::seed_rng();

    HMODULE jvm_module = LoadLibraryW(argv[1]);
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
        classpath_opt, sizeof(classpath_opt), "-Djava.class.path=%ls", argv[2]);

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

    jclass mc_class = env->FindClass("ave");
    if (mc_class == nullptr) {
        env->ExceptionClear();
        std::printf("FindClass(ave) failed\n");
        return 4;
    }
    jfieldID instance_field = env->GetStaticFieldID(mc_class, "S", "Lave;");
    if (instance_field == nullptr) {
        env->ExceptionClear();
        std::printf("GetStaticFieldID(ave.S) failed\n");
        return 4;
    }
    jobject minecraft = env->GetStaticObjectField(mc_class, instance_field);

    slinky::mc::Mappings mappings{};
    mappings.version = 1;        // 1.8.9
    mappings.mapping_flavor = 0; // vanilla obfuscated names
    mappings.cls_minecraft = static_cast<jclass>(env->NewGlobalRef(mc_class));
    mappings.sfid_mc_the_minecraft = instance_field;

    slinky::events::JniContext jni{&mappings, env};

    slinky::account::initialize();
    const int err = slinky::account::set_username("VanillaAlt18", &jni);
    std::printf("set_result=%d\n", err);

    jfieldID session_field = env->GetFieldID(mc_class, "ae", "Lavm;");
    if (session_field == nullptr) {
        env->ExceptionClear();
        std::printf("GetFieldID(ave.ae) failed\n");
        return 4;
    }
    jobject session = env->GetObjectField(minecraft, session_field);
    char username[256]{};
    char player_id[256]{};
    char token[256]{};
    char session_type[256]{};
    call_string(env, session, "c", username, sizeof(username));
    call_string(env, session, "b", player_id, sizeof(player_id));
    call_string(env, session, "d", token, sizeof(token));
    call_string(env, session, "f", session_type, sizeof(session_type));

    std::printf("session username=%s\n", username);
    std::printf("session playerID=%s\n", player_id);
    std::printf("session token=%s\n", token);
    std::printf("session type=%s\n", session_type);

    const bool ok = err == 0 &&
                    std::strcmp(username, "VanillaAlt18") == 0 &&
                    std::strcmp(session_type, "legacy") == 0;
    std::printf("vanilla_ok=%d\n", ok ? 1 : 0);

    vm->DestroyJavaVM();
    return ok ? 0 : 1;
}
