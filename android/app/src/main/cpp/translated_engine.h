#pragma once
#include <jni.h>
#include <cstdint>
#include <mutex>
#include <string>

namespace akl {
// Opaque namespace pointers are passed only to libnativeloader, never unpacked.
struct BridgeApi {
    bool (*initialized)() = nullptr;
    void* (*find_namespace)(JNIEnv*, jobject) = nullptr;
    void* (*open_library)(void*, const char*, bool*, char**) = nullptr;
    void (*free_error)(char*) = nullptr;
    void* (*trampoline)(void*, const char*, const char*, uint32_t) = nullptr;
    const char* (*bridge_error)() = nullptr;
    bool complete() const;
};

struct BridgeResult {
    int code;
    std::string stage;
    std::string detail;
};

class TranslatedEngine {
public:
    BridgeResult start(const BridgeApi&, JNIEnv*, jobject game_loader,
                       jclass bridge_class, const char* guest_path,
                       jint mask, jint speed, jint attack);
private:
    std::mutex mutex_;
    void* guest_handle_ = nullptr;
    bool invoked_ = false;
    BridgeResult last_{-30, "not-started", ""};
};
}
