#include <dobby.h>
#include <android/log.h>
#include <mutex>
extern "C" int akl_hook_fixture(int);
namespace {
int (*original)(int) = nullptr;
int replacement(int value) {
    return original(value) + 100;
}
} // namespace
bool akl_hook_smoke() {
    static std::once_flag once;
    static bool passed = false;
    std::call_once(once, [] {
        int (*volatile invoke)(int) = akl_hook_fixture;
        const int before = invoke(35);
        const int installed =
            DobbyHook(reinterpret_cast<void *>(akl_hook_fixture),
                      reinterpret_cast<void *>(replacement), reinterpret_cast<void **>(&original));
        const int during = installed == 0 && original ? invoke(35) : -1;
        const int removed =
            installed == 0 ? DobbyDestroy(reinterpret_cast<void *>(akl_hook_fixture)) : -1;
        const int after = invoke(35);
        passed = before == 42 && during == 142 && removed == 0 && after == 42;
        __android_log_print(
            passed ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "AKL",
            "Dobby ADRP self-test before=%d hooked=%d restored=%d install=%d remove=%d passed=%d",
            before, during, after, installed, removed, passed);
    });
    return passed;
}
