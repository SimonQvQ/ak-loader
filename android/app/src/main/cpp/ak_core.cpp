// akl_core —— 明日方舟战斗向功能引擎（从零实现）
//
// 设计：
//   * 全部符号在运行时通过 il2cpp 导出接口按“类名 / 方法名 / 参数个数”解析，
//     不硬编码任何偏移，也不依赖原模块的任何二进制。
//   * 每个功能是独立的 hook 单元，由配置掩码决定安装哪些。
//   * 只包一层“参数/返回值改写”，其余逻辑原样放行，最大限度兼容游戏更新。

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
        return 1;
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
    g_api.domain_get = reinterpret_cast<fn_domain_get>(dlsym(handle, "il2cpp_domain_get"));
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

void* pick_method_pointer(void* method_info) {
    static const size_t kCandidates[] = {0x00, 0x08, 0x18};
    for (size_t off : kCandidates) {
        void* p = *reinterpret_cast<void**>(static_cast<uint8_t*>(method_info) + off);
        auto addr = reinterpret_cast<uintptr_t>(p);
        if (addr >= g_il2cpp.start && addr + 16 < g_il2cpp.end) {
            LOGI("MethodInfo+0x%02zx -> %p", off, p);
            return p;
        }
    }
    return nullptr;
}

/** 在全部已加载程序集里找“含目标类”的那个 image（游戏本体在 Assembly-CSharp） */
void* find_class(const char* ns, const char* name) {
    size_t count = 0;
    const void* asms = g_api.domain_get_assemblies(g_api.domain_get(), &count);
    for (size_t i = 0; i < count; ++i) {
        auto asm_ptr = *reinterpret_cast<const void*const*>(
                static_cast<const uint8_t*>(asms) + i * sizeof(void*));
        void* image = g_api.assembly_get_image(asm_ptr);
        if (!image) continue;
        if (void* klass = g_api.class_from_name(image, ns, name)) return klass;
    }
    return nullptr;
}

/** 轮询等待某个类出现（战斗类多半在开局后才加载） */
void* wait_class(const char* ns, const char* name, int seconds = 60) {
    for (int i = 0; i < seconds * 2; ++i) {
        if (void* k = find_class(ns, name)) {
            LOGI("找到类 %s::%s -> %p", ns, name, k);
            return k;
        }
        usleep(500 * 1000);
    }
    LOGE("超时未找到类 %s::%s", ns, name);
    return nullptr;
}

bool patch_method(void* klass, const char* name, int argc, void* replace,
                  void** tramp_out, const char* tag) {
    void* mi = g_api.method_from_name(klass, name, argc);
    if (!mi) { LOGE("%s: 找不到方法 %s/%d", tag, name, argc); return false; }
    void* entry = pick_method_pointer(mi);
    if (!entry) { LOGE("%s: MethodInfo 无合法代码指针", tag); return false; }
    auto h = new InlineHook();
    if (!hook_install(*h, entry, replace)) { LOGE("%s: hook 失败", tag); return false; }
    if (tramp_out) *tramp_out = h->tramp;
    LOGI("%s 已挂钩 @%p", tag, entry);
    return true;
}

// ---------------------------------------------------------------- 功能单元

struct Config {
    bool speed    = false;   // 倍速
    bool cost     = false;   // 无限费用
    bool life     = false;   // 生命不减
    bool freeze   = false;   // 敌人停滞
    bool win      = false;   // 强制结算胜利
    float scale   = 1.0f;
};

Config g_cfg;

constexpr int kSpeed  = 1 << 0;
constexpr int kCost   = 1 << 1;
constexpr int kLife   = 1 << 2;
constexpr int kFreeze = 1 << 3;
constexpr int kWin    = 1 << 4;

// --- 倍速：UnityEngine.Time.set_timeScale
void (*g_orig_time_scale)(float) = nullptr;
void hk_set_time_scale(float v) {
    if (g_orig_time_scale) g_orig_time_scale(v * g_cfg.scale);
}

// --- 无限费用：BattleController.SetCost(int, int)
void (*g_orig_set_cost)(void*, int32_t, int32_t) = nullptr;
void hk_set_cost(void* self, int32_t a, int32_t b) {
    if (g_orig_set_cost) g_orig_set_cost(self, 9999, 9999);
}

// --- 生命不减：BattleController.ModifyLifePoint(int, obj, int, bool)
void (*g_orig_modify_life)(void*, int32_t, void*, int32_t, bool) = nullptr;
void hk_modify_life(void* self, int32_t type, void* src, int32_t value, bool show) {
    if (value < 0) value = 0;          // 扣血变 0，扣血动画与提示照常
    if (g_orig_modify_life) g_orig_modify_life(self, type, src, value, show);
}

// --- 敌人停滞：BattleEnemy.MoveByRoute(float, bool*)
void (*g_orig_move_by_route)(void*, float, void*) = nullptr;
void hk_move_by_route(void* self, float speed, void* flag) {
    if (g_orig_move_by_route) g_orig_move_by_route(self, 0.0f, flag);
}

// --- 强制胜利：BattleController.get_gameResult() -> int
int32_t (*g_orig_game_result)(void*) = nullptr;
int32_t hk_game_result(void* self) {
    if (g_orig_game_result) g_orig_game_result(self);   // 让游戏自己先跑一遍
    return 3;                                           // 3 = 三星通关
}

void apply_features() {
    if (g_cfg.speed) {
        void* k = wait_class("UnityEngine", "Time");
        if (k) patch_method(k, "set_timeScale", 1,
                            reinterpret_cast<void*>(&hk_set_time_scale),
                            reinterpret_cast<void**>(&g_orig_time_scale), "倍速");
    }
    if (g_cfg.cost || g_cfg.life || g_cfg.win) {
        void* k = wait_class("", "BattleController");
        if (k) {
            if (g_cfg.cost)
                patch_method(k, "SetCost", 2, reinterpret_cast<void*>(&hk_set_cost),
                             reinterpret_cast<void**>(&g_orig_set_cost), "无限费用");
            if (g_cfg.life)
                patch_method(k, "ModifyLifePoint", 4,
                             reinterpret_cast<void*>(&hk_modify_life),
                             reinterpret_cast<void**>(&g_orig_modify_life), "生命不减");
            if (g_cfg.win)
                patch_method(k, "get_gameResult", 0,
                             reinterpret_cast<void*>(&hk_game_result),
                             reinterpret_cast<void**>(&g_orig_game_result), "强制胜利");
        }
    }
    if (g_cfg.freeze) {
        void* k = wait_class("", "BattleEnemy");
        if (k) patch_method(k, "MoveByRoute", 2,
                            reinterpret_cast<void*>(&hk_move_by_route),
                            reinterpret_cast<void**>(&g_orig_move_by_route), "敌人停滞");
    }
}

// ---------------------------------------------------------------- JNI

void native_start(JNIEnv*, jclass, jint mask, jint speedX100) {
    g_cfg.speed  = mask & kSpeed;
    g_cfg.cost   = mask & kCost;
    g_cfg.life   = mask & kLife;
    g_cfg.freeze = mask & kFreeze;
    g_cfg.win    = mask & kWin;
    g_cfg.scale  = speedX100 / 100.0f;
    if (g_cfg.scale < 1.0f) g_cfg.scale = 1.0f;

    LOGI("引擎启动 mask=0x%x scale=x%.2f", mask, g_cfg.scale);

    void* handle = nullptr;
    for (int i = 0; i < 120 && !handle; ++i) {
        handle = dlopen("libil2cpp.so", RTLD_NOW);
        if (!handle) usleep(500 * 1000);
    }
    if (!handle) { LOGE("libil2cpp.so 未加载"); return; }
    if (!find_module("libil2cpp.so", g_il2cpp)) { LOGE("取不到代码段"); return; }
    if (!resolve_api(handle)) { LOGE("il2cpp 导出符号不全"); return; }

    apply_features();
}

const JNINativeMethod kMethods[] = {
        {"start", "(II)V", reinterpret_cast<void*>(native_start)},
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
