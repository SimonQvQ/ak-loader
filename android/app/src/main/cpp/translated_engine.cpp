#include "translated_engine.h"
#include "engine_status.h"
#include <cstdio>
#include <cstring>

namespace akl {
namespace {
constexpr char kProbeName[] = "Java_dev_ak_ak_1loader_GuestEngine_probe";
constexpr char kRunName[] = "Java_dev_ak_ak_1loader_GuestEngine_run";
using Probe = jint (JNICALL *)(JNIEnv*, jclass);
using Run = jint (JNICALL *)(JNIEnv*, jclass, jint, jint, jint);

bool is_arm64_elf(const char* path) {
    if (!path || !*path) return false;
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    unsigned char header[20]{};
    const size_t n = std::fread(header, 1, sizeof header, file);
    std::fclose(file);
    return n == sizeof header && std::memcmp(header, "\177ELF", 4) == 0 &&
           header[4] == 2 && header[5] == 1 && header[18] == 183 && header[19] == 0;
}

std::string bridge_error(const BridgeApi& api) {
    const char* error = api.bridge_error ? api.bridge_error() : nullptr;
    return error ? error : "NativeBridge did not report an error";
}
}

bool BridgeApi::complete() const {
    return initialized && find_namespace && open_library && free_error && trampoline;
}

BridgeResult TranslatedEngine::start(const BridgeApi& api, JNIEnv* env,
        jobject game_loader, jclass bridge_class, const char* path,
        jint mask, jint speed, jint attack) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Even a failed engine call can have installed some hooks. Never run it twice.
    if (invoked_) return last_;
    if (!api.complete() || !api.initialized())
        return {kBridgeUnavailable, "bridge-api", "NativeBridge is missing or not initialized"};
    if (!is_arm64_elf(path))
        return {kInvalidGuest, "guest-elf", "Guest must be an ELF64 little-endian AArch64 library"};
    if (!game_loader)
        return {kNamespaceUnavailable, "namespace", "Game ClassLoader is null"};
    void* ns = api.find_namespace(env, game_loader);
    if (!ns) return {kNamespaceUnavailable, "namespace", "Game native namespace was not found"};
    if (!guest_handle_) {
        bool translated = false;
        char* error = nullptr;
        // libnativeloader selects the actual linker and flags for this namespace.
        guest_handle_ = api.open_library(ns, path, &translated, &error);
        std::string detail = error ? error : "Library could not be opened in the game namespace";
        if (error) api.free_error(error);
        if (!guest_handle_) return {kGuestLoadFailed, "guest-load", detail};
        if (!translated) {
            guest_handle_ = nullptr;
            return {kNotTranslated, "guest-load", "Namespace returned a host library, not a translated guest"};
        }
    }
    void* probe = api.trampoline(guest_handle_, kProbeName, "I", 1);
    if (!probe) return {kProbeFailed, "guest-probe", bridge_error(api)};
    // Only the host-callable trampoline is invoked, with the declared JNI signature.
    if (reinterpret_cast<Probe>(probe)(env, bridge_class) != kGuestProbe)
        return {kProbeFailed, "guest-probe", "Guest probe returned an unexpected value"};
    void* run = api.trampoline(guest_handle_, kRunName, "IIII", 4);
    if (!run) return {kEntryUnavailable, "guest-entry", bridge_error(api)};
    invoked_ = true;
    const int status = reinterpret_cast<Run>(run)(env, bridge_class, mask, speed, attack);
    last_ = {status, "guest-return", "arm64 guest was called through NativeBridge; check AKL-GUEST logs"};
    // Keep the guest loaded for the lifetime of its hooks and background threads.
    return last_;
}
}
