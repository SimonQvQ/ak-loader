// x86_64 host implementation. It calls an arm64 guest via ART's NativeBridge.
// Private platform APIs are resolved at runtime; their visibility is device-dependent.
#include "translated_engine.h"
#include "engine_status.h"
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <mutex>

#define HOST_LOG(...) __android_log_print(ANDROID_LOG_INFO, "AKL-X64", __VA_ARGS__)
#define HOST_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "AKL-X64", __VA_ARGS__)

namespace {
akl::TranslatedEngine g_engine;
akl::BridgeApi g_api;
std::once_flag g_api_once;

void* lookup(const char* name, const char* const* paths, size_t count) {
    if (void* symbol = dlsym(RTLD_DEFAULT, name)) return symbol;
    for (size_t i = 0; i < count; ++i) {
        // dlopen references intentionally stay alive while the engine uses their functions.
        void* library = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (!library) continue;
        if (void* symbol = dlsym(library, name)) return symbol;
        dlclose(library);
    }
    HOST_ERR("platform symbol unavailable: %s (linker namespace may restrict access)", name);
    return nullptr;
}

void resolve_api() {
    const char* loader_paths[] = {"libnativeloader.so", "libnativeloader_lazy.so",
        "/apex/com.android.art/lib64/libnativeloader.so", "/system/lib64/libnativeloader_lazy.so"};
    const char* bridge_paths[] = {"libnativebridge.so", "libnativebridge_lazy.so",
        "/apex/com.android.art/lib64/libnativebridge.so", "/system/lib64/libnativebridge_lazy.so"};
#define LOAD_LOADER(field, symbol) g_api.field = reinterpret_cast<decltype(g_api.field)>(lookup(symbol, loader_paths, 4))
#define LOAD_BRIDGE(field, symbol) g_api.field = reinterpret_cast<decltype(g_api.field)>(lookup(symbol, bridge_paths, 4))
    LOAD_LOADER(find_namespace, "FindNativeLoaderNamespaceByClassLoader");
    LOAD_LOADER(open_library, "OpenNativeLibraryInNamespace");
    LOAD_LOADER(free_error, "NativeLoaderFreeErrorMessage");
    LOAD_BRIDGE(initialized, "NativeBridgeInitialized");
    LOAD_BRIDGE(trampoline, "NativeBridgeGetTrampoline");
    LOAD_BRIDGE(bridge_error, "NativeBridgeGetError");
#undef LOAD_LOADER
#undef LOAD_BRIDGE
}

jint start_translated(JNIEnv* env, jobject self, jstring path, jobject game_loader,
        jint mask, jint speed, jint attack) {
    std::call_once(g_api_once, resolve_api);
    if (!path || !game_loader) return akl::kInvalidGuest;
    const char* guest_path = env->GetStringUTFChars(path, nullptr);
    if (!guest_path) return akl::kInvalidGuest; // Preserve pending JNI exception.
    jclass bridge_class = env->GetObjectClass(self);
    if (!bridge_class) {
        env->ReleaseStringUTFChars(path, guest_path);
        return akl::kEntryUnavailable;
    }
    HOST_LOG("loading arm64 guest: %s", guest_path);
    const auto result = g_engine.start(g_api, env, game_loader, bridge_class,
                                      guest_path, mask, speed, attack);
    env->DeleteLocalRef(bridge_class);
    env->ReleaseStringUTFChars(path, guest_path);
    if (result.code >= 0) HOST_LOG("stage=%s status=%d: %s", result.stage.c_str(), result.code, result.detail.c_str());
    else HOST_ERR("stage=%s status=%d: %s", result.stage.c_str(), result.code, result.detail.c_str());
    return result.code;
}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass bridge = env->FindClass("dev/ak/ak_loader/NativeBridge");
    if (!bridge) return JNI_ERR;
    const JNINativeMethod methods[] = {{"startTranslated",
        "(Ljava/lang/String;Ljava/lang/ClassLoader;III)I", reinterpret_cast<void*>(start_translated)}};
    if (env->RegisterNatives(bridge, methods, 1) != JNI_OK) return JNI_ERR;
    HOST_LOG("x86_64 host loader registered; guest execution has not been tested yet");
    return JNI_VERSION_1_6;
}
