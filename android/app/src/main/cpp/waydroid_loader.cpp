// x86_64 host implementation. It calls an arm64 guest via ART's NativeBridge.
// Private platform APIs are resolved at runtime; their visibility is device-dependent.
#include "translated_engine.h"
#include "engine_status.h"
#include "loaded_export.h"
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <mutex>
#include <cstring>
#include <sys/system_properties.h>

#define HOST_LOG(...) __android_log_print(ANDROID_LOG_INFO, "AKL-X64", __VA_ARGS__)
#define HOST_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "AKL-X64", __VA_ARGS__)

namespace {
akl::TranslatedEngine g_engine;
akl::BridgeApi g_api;
std::once_flag g_api_once;

void *lookup(const char *name, const char *const *paths, size_t count) {
    auto checked = [name](void *symbol) -> void * {
        Dl_info info{};
        if (!symbol || !dladdr(symbol, &info) || !info.dli_fname)
            return nullptr;
        // The system *_lazy forwarding libraries can resolve their own symbol
        // in an app namespace and recurse. Never accept their entry points.
        if (std::strstr(info.dli_fname, "_lazy.so"))
            return nullptr;
        HOST_LOG("resolved %s from %s", name, info.dli_fname);
        return symbol;
    };
    for (size_t i = 0; i < count; ++i) {
        // dlopen references intentionally stay alive while the engine uses their functions.
        void *library = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (!library)
            continue;
        if (void *symbol = checked(dlsym(library, name)))
            return symbol;
        dlclose(library);
    }
    if (void *symbol = checked(dlsym(RTLD_DEFAULT, name)))
        return symbol;
    for (size_t i = 0; i < count; ++i) {
        if (paths[i][0] != '/')
            continue;
        const char *reason = nullptr;
        if (void *symbol = find_loaded_export(paths[i], name, &reason)) {
            // The ELF resolver already validates exact file identity and an
            // executable mapping. dladdr can itself hide other namespaces.
            HOST_LOG("resolved %s from mapped ELF %s", name, paths[i]);
            return symbol;
        }
        HOST_ERR("mapped ELF lookup %s: %s", name, reason ? reason : "unknown");
    }
    HOST_ERR("platform symbol unavailable: %s (linker namespace may restrict access)", name);
    return nullptr;
}

void resolve_api() {
    const char *loader_paths[] = {"/apex/com.android.art/lib64/libnativeloader.so",
                                  "libnativeloader.so"};
    const char *bridge_paths[] = {"/apex/com.android.art/lib64/libnativebridge.so",
                                  "libnativebridge.so"};
#define LOAD_LOADER(field, symbol)                                                                 \
    g_api.field = reinterpret_cast<decltype(g_api.field)>(lookup(symbol, loader_paths, 2))
#define LOAD_BRIDGE(field, symbol)                                                                 \
    g_api.field = reinterpret_cast<decltype(g_api.field)>(lookup(symbol, bridge_paths, 2))
    LOAD_LOADER(find_namespace, "FindNativeLoaderNamespaceByClassLoader");
    LOAD_LOADER(open_library, "OpenNativeLibraryInNamespace");
    LOAD_LOADER(free_error, "NativeLoaderFreeErrorMessage");
    LOAD_BRIDGE(initialized, "NativeBridgeInitialized");
    LOAD_BRIDGE(trampoline, "NativeBridgeGetTrampoline");
    LOAD_BRIDGE(bridge_error, "NativeBridgeGetError");
#undef LOAD_LOADER
#undef LOAD_BRIDGE
}

jint start_translated(JNIEnv *env, jobject self, jstring path, jobject game_loader, jint mask,
                      jint speed, jint attack) {
    std::call_once(g_api_once, resolve_api);
    if (!path || !game_loader)
        return akl::kInvalidGuest;
    const char *guest_path = env->GetStringUTFChars(path, nullptr);
    if (!guest_path)
        return akl::kInvalidGuest; // Preserve pending JNI exception.
    jclass bridge_class = env->GetObjectClass(self);
    if (!bridge_class) {
        env->ReleaseStringUTFChars(path, guest_path);
        return akl::kEntryUnavailable;
    }
    HOST_LOG("loading arm64 guest: %s", guest_path);
    char probe_only[PROP_VALUE_MAX]{};
    __system_property_get("debug.akl.probe_only", probe_only);
    const auto result = g_engine.start(g_api, env, game_loader, bridge_class, guest_path, mask,
                                       speed, attack, std::strcmp(probe_only, "1") == 0);
    env->DeleteLocalRef(bridge_class);
    env->ReleaseStringUTFChars(path, guest_path);
    if (result.code >= 0)
        HOST_LOG("stage=%s status=%d: %s", result.stage.c_str(), result.code,
                 result.detail.c_str());
    else
        HOST_ERR("stage=%s status=%d: %s", result.stage.c_str(), result.code,
                 result.detail.c_str());
    return result.code;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    jclass bridge = env->FindClass("dev/ak/ak_loader/NativeBridge");
    if (!bridge)
        return JNI_ERR;
    const JNINativeMethod methods[] = {{"startTranslated",
                                        "(Ljava/lang/String;Ljava/lang/ClassLoader;III)I",
                                        reinterpret_cast<void *>(start_translated)}};
    if (env->RegisterNatives(bridge, methods, 1) != JNI_OK)
        return JNI_ERR;
    HOST_LOG("x86_64 host loader registered; awaiting guest probe");
    return JNI_VERSION_1_6;
}
