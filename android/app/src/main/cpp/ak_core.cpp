// ak_core —— 明日方舟专用载入期引擎（从零实现，不依赖任何第三方 hook 库）
//
// 职责：
//   1. 定位 Unity il2cpp 运行时（libil2cpp.so）
//   2. 通过 il2cpp 导出的 C 接口找到 UnityEngine.Time.set_timeScale
//   3. 用自写 arm64 行内 hook 改写它，实现倍速
//
// 所有符号解析都是"运行时探测 + 校验"，绝不对固定偏移硬编码。

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <link.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include "hook.h"

#define TAG "AKL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

// ---------------------------------------------------------------- 模块定位

struct ModuleRange {
    uintptr_t start = 0;
    uintptr_t end   = 0;
    bool valid() const { return end > start; }
};

struct SearchCtx {
    const char* name;
    ModuleRange range;
};

int find_cb(dl_phdr_info* info, size_t, void* data) {
    auto* ctx = static_cast<SearchCtx*>(data);
    if (!info->dlpi_name || !std::strstr(info->dlpi_name, ctx->name)) return 0;

    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        if (!(ph->p_flags & PF_X)) continue;
        uintptr_t s = info->dlpi_addr + ph->p_vaddr;
        if (s < lo) lo = s;
        if (s + ph->p_memsz > hi) hi = s + ph->p_memsz;
    }
    if (hi > lo) {
        ctx->range.start = lo;
        ctx->range.end   = hi;
        return 1;   // 找到即停
    }
    return 0;
}

bool find_module(const char* name, ModuleRange& out) {
    SearchCtx ctx{name, {}};
    dl_iterate_phdr(find_cb, &ctx);
    out = ctx.range;
    return out.valid();
}

// ---------------------------------------------------------------- il2cpp API

using fn_domain_get            = void* (*)();
using fn_domain_get_assemblies = const void* (*)(const void*, size_t*);
using fn_assembly_get_image    = void* (*)(const void*);
using fn_class_from_name       = void* (*)(const void*, const char*, const char*);
using fn_method_from_name      = void* (*)(void*, const char*, int);

struct Il2Cpp {
    fn_domain_get            domain_get            = nullptr;
    fn_domain_get_assemblies domain_get_assemblies = nullptr;
    fn_assembly_get_image    assembly_get_image    = nullptr;
    fn_class_from_name       class_from_name       = nullptr;
    fn_method_from_name      method_from_name      = nullptr;

    bool complete() const {
        return domain_get && domain_get_assemblies && assembly_get_image &&
               class_from_name && method_from_name;
    }
};

Il2Cpp g_api;
ModuleRange g_il2cpp;

bool resolve_api(void* handle) {
    g_api.domain_get      = reinterpret_cast<fn_domain_get>(dlsym(handle, "il2cpp_domain_get"));
    g_api.domain_get_assemblies = reinterpret_cast<fn_domain_get_assemblies>(
            dlsym(handle, "il2cpp_domain_get_assemblies"));
    g_api.assembly_get_image = reinterpret_cast<fn_assembly_get_image>(
            dlsym(handle, "il2cpp_assembly_get_image"));
    g_api.class_from_name = reinterpret_cast<fn_class_from_name>(
            dlsym(handle, "il2cpp_class_from_name"));
    g_api.method_from_name = reinterpret_cast<fn_method_from_name>(
            dlsym(handle, "il2cpp_class_get_method_from_name"));
    return g_api.complete();
}

// MethodInfo 的布局在不同 Unity 版本有差异。这里不猜单一偏移，而是把候选
// 指针逐个做"是否落在 il2cpp 代码段内"的校验，取第一个成立的。
void* pick_method_pointer(void* method_info) {
    static const size_t kCandidates[] = {0x00, 0x08, 0x18};
    for (size_t off : kCandidates) {
        void* p = *reinterpret_cast<void**>(static_cast<uint8_t*>(method_info) + off);
        auto addr = reinterpret_cast<uintptr_t>(p);
        if (addr >= g_il2cpp.start && addr + 16 < g_il2cpp.end) {
            LOGI("MethodInfo+0x%02zx -> %p 通过代码段校验", off, p);
            return p;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- 倍速 hook

float g_scale = 1.0f;
InlineHook g_hook;
void (*g_orig_time_scale)(float) = nullptr;

void hk_set_time_scale(float value) {
    if (g_orig_time_scale) g_orig_time_scale(value * g_scale);
}

void* find_arknights_image() {
    size_t count = 0;
    const void* asms = g_api.domain_get_assemblies(g_api.domain_get(), &count);
    LOGI("程序集数量 = %zu", count);
    for (size_t i = 0; i < count; ++i) {
        auto asm_ptr = *reinterpret_cast<const void*const*>(
                static_cast<const uint8_t*>(asms) + i * sizeof(void*));
        void* image = g_api.assembly_get_image(asm_ptr);
        if (!image) continue;
        void* klass = g_api.class_from_name(image, "UnityEngine", "Time");
        if (klass) return klass;
    }
    return nullptr;
}

bool apply_speed_hook() {
    void* klass = nullptr;
    for (int i = 0; i < 120 && !klass; ++i) {   // 最多等 ~60s
        klass = find_arknights_image();
        if (!klass) usleep(500 * 1000);
    }
    if (!klass) { LOGE("没找到 UnityEngine.Time，放弃"); return false; }

    void* method = g_api.method_from_name(klass, "set_timeScale", 1);
    if (!method) { LOGE("没找到 set_timeScale"); return false; }

    void* entry = pick_method_pointer(method);
    if (!entry) { LOGE("MethodInfo 中没有合法代码指针"); return false; }

    if (!hook_install(g_hook, entry, reinterpret_cast<void*>(&hk_set_time_scale))) {
        LOGE("hook 安装失败");
        return false;
    }
    g_orig_time_scale = reinterpret_cast<void (*)(float)>(g_hook.tramp);
    LOGI("倍速已生效 x%.2f", g_scale);
    return true;
}

// ---------------------------------------------------------------- JNI

void native_start(JNIEnv*, jclass, jint speedX100) {
    g_scale = static_cast<float>(speedX100) / 100.0f;
    if (g_scale < 1.0f) g_scale = 1.0f;
    LOGI("启动引擎 speed=x%.2f", g_scale);

    void* handle = nullptr;
    for (int i = 0; i < 120 && !handle; ++i) {
        handle = dlopen("libil2cpp.so", RTLD_NOW);
        if (!handle) usleep(500 * 1000);
    }
    if (!handle) { LOGE("libil2cpp.so 未加载"); return; }

    if (!find_module("libil2cpp.so", g_il2cpp)) { LOGE("取不到 il2cpp 代码段"); return; }
    LOGI("libil2cpp 代码段 [%p, %p)", (void*)g_il2cpp.start, (void*)g_il2cpp.end);

    if (!resolve_api(handle)) { LOGE("il2cpp 导出符号不全"); return; }

    apply_speed_hook();
}

const JNINativeMethod kMethods[] = {
        {"start", "(I)V", reinterpret_cast<void*>(native_start)},
};

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return -1;
    jclass c = env->FindClass("dev/ak/ak_loader/NativeBridge");
    if (!c) { LOGE("找不到 NativeBridge"); return -1; }
    env->RegisterNatives(c, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    LOGI("akl_core 已加载");
    return JNI_VERSION_1_6;
}
