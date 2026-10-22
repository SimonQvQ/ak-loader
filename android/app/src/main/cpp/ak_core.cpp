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
#include <pthread.h>

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <cstring>

#include "hook.h"
#include "engine_status.h"

#if !defined(__aarch64__)
#error "ak_core.cpp is the arm64 guest; build waydroid_loader.cpp for x86_64"
#endif

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
using fn_class_get_fields      = void* (*)(void*, void**);
using fn_object_get_class      = void* (*)(void*);
using fn_field_get_name        = const char* (*)(void*);
using fn_field_get_offset      = size_t (*)(void*);
using fn_field_get_type        = void* (*)(void*);
using fn_type_get_type         = int (*)(void*);

using fn_runtime_invoke = void* (*)(void* method, void* obj, void** params, void** exc);
using fn_thread_attach  = void* (*)(void* domain);

struct Il2Cpp {
    fn_domain_get            domain_get            = nullptr;
    fn_domain_get_assemblies domain_get_assemblies = nullptr;
    fn_assembly_get_image    assembly_get_image    = nullptr;
    fn_class_from_name       class_from_name       = nullptr;
    fn_method_from_name      method_from_name      = nullptr;
    fn_class_get_fields      class_get_fields      = nullptr;
    fn_object_get_class      object_get_class      = nullptr;
    fn_field_get_name        field_get_name        = nullptr;
    fn_field_get_offset      field_get_offset      = nullptr;
    fn_field_get_type        field_get_type        = nullptr;
    fn_type_get_type         type_get_type         = nullptr;
    fn_runtime_invoke        runtime_invoke        = nullptr;
    fn_thread_attach         thread_attach         = nullptr;

    bool complete() const {
        return domain_get && domain_get_assemblies && assembly_get_image &&
               class_from_name && method_from_name &&
               class_get_fields && object_get_class && field_get_name && field_get_offset &&
               field_get_type && type_get_type && runtime_invoke;
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
    g_api.class_get_fields = reinterpret_cast<fn_class_get_fields>(
            dlsym(handle, "il2cpp_class_get_fields"));
    g_api.object_get_class = reinterpret_cast<fn_object_get_class>(
            dlsym(handle, "il2cpp_object_get_class"));
    g_api.field_get_name = reinterpret_cast<fn_field_get_name>(
            dlsym(handle, "il2cpp_field_get_name"));
    g_api.field_get_offset = reinterpret_cast<fn_field_get_offset>(
            dlsym(handle, "il2cpp_field_get_offset"));
    g_api.field_get_type = reinterpret_cast<fn_field_get_type>(
            dlsym(handle, "il2cpp_field_get_type"));
    g_api.type_get_type = reinterpret_cast<fn_type_get_type>(
            dlsym(handle, "il2cpp_type_get_type"));
    g_api.runtime_invoke = reinterpret_cast<fn_runtime_invoke>(
            dlsym(handle, "il2cpp_runtime_invoke"));
    g_api.thread_attach = reinterpret_cast<fn_thread_attach>(
            dlsym(handle, "il2cpp_thread_attach"));
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
    bool instant  = false;   // 一键通关：主动调用 FinishGame 结算
    bool atk      = false;   // 攻击/伤害字段倍率
    float atkMul  = 1.0f;
    float scale   = 1.0f;
};

Config g_cfg;

constexpr int kSpeed  = 1 << 0;
constexpr int kCost   = 1 << 1;
constexpr int kLife   = 1 << 2;
constexpr int kFreeze = 1 << 3;
constexpr int kWin    = 1 << 4;
constexpr int kAtk    = 1 << 5;
constexpr int kInstant = 1 << 6;

// --- 倍速：UnityEngine.Time.set_timeScale
void (*g_orig_time_scale)(float) = nullptr;
void hk_set_time_scale(float v) {
    if (g_orig_time_scale) g_orig_time_scale(v * g_cfg.scale);
}

// --- 无限费用：BattleController.SetCost(int, int)
void (*g_orig_set_cost)(void*, int32_t, int32_t) = nullptr;
void hk_set_cost(void* self, int32_t, int32_t) {
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
void hk_move_by_route(void* self, float, void* flag) {
    if (g_orig_move_by_route) g_orig_move_by_route(self, 0.0f, flag);
}

// --- 强制胜利：BattleController.get_gameResult() -> int
int32_t (*g_orig_game_result)(void*) = nullptr;
int32_t hk_game_result(void* self) {
    if (g_orig_game_result) g_orig_game_result(self);   // 让游戏自己先跑一遍
    return 3;                                           // 3 = 三星通关
}

// --- 攻击/伤害倍率：BattleEntity.Init 之后按字段名放大数值
struct Vec2 { float x, y; };

bool name_has(const char* n, const char* const* keys) {
    for (size_t i = 0; keys[i]; ++i) {
        const char* k = keys[i];
        for (const char* p = n; *p; ++p) {
            size_t j = 0;
            while (k[j] && (p[j] == k[j] || p[j] == static_cast<char>(k[j] - 32))) ++j;
            if (!k[j]) return true;
        }
    }
    return false;
}

void scale_fields(void* obj) {
    if (!obj) return;
    static const char* const kKeys[] = {"atk", "attack", "damage", "dmg", nullptr};
    void* klass = g_api.object_get_class(obj);
    void* iter = nullptr;
    void* f = nullptr;
    int touched = 0;
    while ((f = g_api.class_get_fields(klass, &iter))) {
        const char* name = g_api.field_get_name(f);
        if (!name || !name_has(name, kKeys)) continue;
        int t = g_api.type_get_type(g_api.field_get_type(f));
        size_t off = g_api.field_get_offset(f);
        uint8_t* p = static_cast<uint8_t*>(obj) + off;
        if (t == 0x08) {           // I4
            auto* v = reinterpret_cast<int32_t*>(p);
            *v = static_cast<int32_t>(*v * g_cfg.atkMul);
            ++touched;
        } else if (t == 0x0c) {    // R4
            auto* v = reinterpret_cast<float*>(p);
            *v *= g_cfg.atkMul;
            ++touched;
        } else if (t == 0x0a) {    // I8
            auto* v = reinterpret_cast<int64_t*>(p);
            *v = static_cast<int64_t>(*v * g_cfg.atkMul);
            ++touched;
        }
    }
    if (touched) LOGI("属性倍率 x%.2f 命中 %d 个字段", g_cfg.atkMul, touched);
}

// BattleEntity.Init(obj, obj, int, int, int, Vector2, float, obj)
void (*g_orig_entity_init)(void*, void*, int32_t, int32_t, int32_t, Vec2, float, void*) = nullptr;
void hk_entity_init(void* self, void* p1, int32_t a, int32_t b, int32_t c,
                    Vec2 v, float f, void* p8) {
    if (g_orig_entity_init) g_orig_entity_init(self, p1, a, b, c, v, f, p8);
    if (g_cfg.atk && g_cfg.atkMul > 1.0f) scale_fields(self);
}

int apply_features() {
    int installed = 0;
    if (g_cfg.speed) {
        void* k = wait_class("UnityEngine", "Time");
        if (k) installed += patch_method(k, "set_timeScale", 1,
                            reinterpret_cast<void*>(&hk_set_time_scale),
                            reinterpret_cast<void**>(&g_orig_time_scale), "倍速");
    }
    if (g_cfg.cost || g_cfg.life || g_cfg.win) {
        void* k = wait_class("", "BattleController");
        if (k) {
            if (g_cfg.cost)
                installed += patch_method(k, "SetCost", 2, reinterpret_cast<void*>(&hk_set_cost),
                             reinterpret_cast<void**>(&g_orig_set_cost), "无限费用");
            if (g_cfg.life)
                installed += patch_method(k, "ModifyLifePoint", 4,
                             reinterpret_cast<void*>(&hk_modify_life),
                             reinterpret_cast<void**>(&g_orig_modify_life), "生命不减");
            if (g_cfg.win)
                installed += patch_method(k, "get_gameResult", 0,
                             reinterpret_cast<void*>(&hk_game_result),
                             reinterpret_cast<void**>(&g_orig_game_result), "强制胜利");
        }
    }
    if (g_cfg.atk) {
        void* k = wait_class("", "BattleEntity");
        if (k) installed += patch_method(k, "Init", 8, reinterpret_cast<void*>(&hk_entity_init),
                            reinterpret_cast<void**>(&g_orig_entity_init), "属性倍率");
    }
    if (g_cfg.freeze) {
        void* k = wait_class("", "BattleEnemy");
        if (k) installed += patch_method(k, "MoveByRoute", 2,
                            reinterpret_cast<void*>(&hk_move_by_route),
                            reinterpret_cast<void**>(&g_orig_move_by_route), "敌人停滞");
    }
    return installed;
}

constexpr unsigned kHostMachine = 0xB7;   // EM_AARCH64

const char* machine_name(unsigned m) {
    switch (m) {
        case 0xB7: return "arm64";
        case 0x3E: return "x86_64";
        case 0x28: return "arm";
        case 0x03: return "x86";
        default:   return "unknown";
    }
}

// 读 libil2cpp.so 的 ELF e_machine，用来判断它是不是被翻译执行的另一套架构
unsigned elf_machine_of_il2cpp() {
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    unsigned machine = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (!std::strstr(line, "libil2cpp.so")) continue;
        if (!std::strstr(line, "r--p")) continue;   // 只读段对应文件偏移 0
        char* path = std::strchr(line, '/');
        if (!path) continue;
        char clean[400];
        std::snprintf(clean, sizeof clean, "%s", path);
        clean[std::strcspn(clean, "\n")] = 0;
        int fd = ::open(clean, O_RDONLY);
        if (fd >= 0) {
            unsigned char h[20] = {};
            if (::read(fd, h, sizeof h) == static_cast<ssize_t>(sizeof h))
                machine = static_cast<unsigned>(h[18]) | (static_cast<unsigned>(h[19]) << 8);
            ::close(fd);
        }
        break;
    }
    std::fclose(f);
    return machine;
}


// ---------------------------------------------------------------- 一键通关

// 主动调用 BattleController.get_instance() 拿到战斗实例，再用
// FinishGame(3, true, null) 让这一局立刻结算成三星胜利。
void* instant_win_thread(void*) {
#if defined(__aarch64__)
    if (g_api.thread_attach && g_api.domain_get)
        g_api.thread_attach(g_api.domain_get());

    void* klass = nullptr;
    for (int i = 0; i < 120 && !klass; ++i) {
        klass = find_class("", "BattleController");
        if (!klass) usleep(500 * 1000);
    }
    if (!klass) { LOGE("[一键通关] 找不到 BattleController"); return nullptr; }

    void* get_instance = g_api.method_from_name(klass, "get_instance", 0);
    void* finish       = g_api.method_from_name(klass, "FinishGame", 3);
    LOGI("[一键通关] get_instance=%p FinishGame=%p", get_instance, finish);
    if (!get_instance || !finish) { LOGE("[一键通关] 方法解析失败"); return nullptr; }

    for (int i = 0; i < 600; ++i) {
        void* exc = nullptr;
        void* ctrl = g_api.runtime_invoke(get_instance, nullptr, nullptr, &exc);
        if (!exc && ctrl) {
            int32_t result = 3;      // 3 星
            bool win = true;
            void* arg2 = nullptr;    // 第三个参数按引用类型传 null
            void* params[3] = {&result, &win, &arg2};
            void* exc2 = nullptr;
            g_api.runtime_invoke(finish, ctrl, params, &exc2);
            if (exc2) {
                LOGE("[一键通关] FinishGame 抛异常，参数签名可能不是 (int,bool,obj)");
                return nullptr;
            }
            LOGI("[一键通关] 已请求结算（result=3）");
            return nullptr;
        }
        usleep(2000 * 1000);   // 2s 轮询一次，等玩家进入战斗
    }
    LOGE("[一键通关] 等了 20 分钟也没进战斗");
#endif
    return nullptr;
}

// ---------------------------------------------------------------- JNI

int start_engine(jint mask, jint speedX100, jint atkX10) {
    g_cfg.speed  = mask & kSpeed;
    g_cfg.cost   = mask & kCost;
    g_cfg.life   = mask & kLife;
    g_cfg.freeze = mask & kFreeze;
    g_cfg.win    = mask & kWin;
    g_cfg.atk    = mask & kAtk;
    g_cfg.instant = mask & kInstant;
    g_cfg.atkMul = atkX10 / 10.0f;
    if (g_cfg.atkMul < 1.0f) g_cfg.atkMul = 1.0f;
    g_cfg.scale  = speedX100 / 100.0f;
    if (g_cfg.scale < 1.0f) g_cfg.scale = 1.0f;

    LOGI("引擎启动 mask=0x%x scale=x%.2f", mask, g_cfg.scale);

    void* handle = nullptr;
    for (int i = 0; i < 120 && !handle; ++i) {
        handle = dlopen("libil2cpp.so", RTLD_NOW);
        if (!handle) usleep(500 * 1000);
    }
    if (!handle) {
        const char* error = dlerror();
        LOGE("guest 无法加载 libil2cpp.so（等了 60s）: %s", error ? error : "unknown");
        return akl::kIl2cppUnavailable;
    }
    if (!find_module("libil2cpp.so", g_il2cpp)) { LOGE("取不到代码段"); return akl::kCodeUnavailable; }

    const unsigned game_machine = elf_machine_of_il2cpp();
    LOGI("架构检查：引擎=%s il2cpp=%s", machine_name(kHostMachine), machine_name(game_machine));
    if (game_machine && game_machine != kHostMachine) {
        LOGE("il2cpp 是 %s、guest 是 %s，架构不匹配，停止调用。",
             machine_name(game_machine), machine_name(kHostMachine));
        return akl::kWrongArchitecture;
    }
    if (!resolve_api(handle)) { LOGE("il2cpp 导出符号不全"); return akl::kApiUnavailable; }
    void* domain = nullptr;
    for (int i = 0; i < 120 && !domain; ++i) {
        domain = g_api.domain_get();
        if (!domain) usleep(500 * 1000);
    }
    if (!domain || !g_api.thread_attach || !g_api.thread_attach(domain)) {
        LOGE("无法附加到 il2cpp 线程");
        return akl::kThreadUnavailable;
    }

    const int installed = apply_features();
    LOGI("功能安装结果：成功安装 %d 个 hook；战斗效果仍需验证", installed);

    bool pending = false;
    if (g_cfg.instant) {
        pthread_t t;
        const int error = pthread_create(&t, nullptr, instant_win_thread, nullptr);
        if (error == 0) {
            pthread_detach(t);
            pending = true;
            LOGI("一键通关工作线程已启动；尚未确认结算成功");
        } else {
            LOGE("一键通关线程创建失败: %d", error);
        }
    }
    return installed > 0 ? akl::kStarted : pending ? akl::kPending : akl::kNoHooks;
}

void native_start(JNIEnv*, jclass, jint mask, jint speed, jint attack) {
    LOGI("arm64 引擎返回 status=%d", start_engine(mask, speed, attack));
}

const JNINativeMethod kMethods[] = {
        {"start", "(III)V", reinterpret_cast<void*>(native_start)},
};

}  // namespace

// JNI-shaped guest endpoints. The x86 host calls only NativeBridge trampolines,
// not these AArch64 addresses. Loading through libnativeloader does not call
// JNI_OnLoad, so it cannot overwrite the host NativeBridge's registrations.
extern "C" JNIEXPORT jint JNICALL
Java_dev_ak_ak_1loader_GuestEngine_probe(JNIEnv*, jclass) {
    LOGI("AKL-GUEST: arm64 探针已执行");
    return akl::kGuestProbe;
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_ak_ak_1loader_GuestEngine_run(JNIEnv*, jclass, jint mask, jint speed, jint attack) {
    LOGI("AKL-GUEST: 已进入 arm64 功能引擎");
    return start_engine(mask, speed, attack);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return -1;
    jclass c = env->FindClass("dev/ak/ak_loader/NativeBridge");
    if (!c) { LOGE("找不到 NativeBridge"); return -1; }
    if (env->RegisterNatives(c, kMethods, sizeof(kMethods) / sizeof(kMethods[0])) != JNI_OK) {
        LOGE("NativeBridge JNI 注册失败");
        return JNI_ERR;
    }
    LOGI("akl_core 已加载");
    return JNI_VERSION_1_6;
}
