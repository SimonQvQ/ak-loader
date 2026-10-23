// arm64 execution side, using the same BNM/Dobby architecture as the reference.
#include "engine_status.h"
#include "battle_policy.h"
#include <BNM/Loading.hpp>
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/Field.hpp>
#include <BNM/Image.hpp>
#include <android/log.h>
#include <sys/system_properties.h>
#include <jni.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dobby.h>
#if !defined(__aarch64__)
#error "ak_core.cpp is the arm64 guest; x64 uses waydroid_loader.cpp"
#endif
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AKL", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AKL", __VA_ARGS__)
bool akl_hook_smoke();
namespace {
using Object = BNM::IL2CPP::Il2CppObject;
using Info = BNM::IL2CPP::MethodInfo;
using Klass = BNM::IL2CPP::Il2CppClass;
using Field = BNM::IL2CPP::FieldInfo;
using Type = BNM::IL2CPP::Il2CppType;
struct Config {
    int mask = 0;
    int speed = 100;
    int damage = 10;
} g_config;
std::atomic<bool> g_started{false};
void *g_il2cpp = nullptr;
const Type *(*g_param)(const Info *, uint32_t) = nullptr;
const Type *(*g_return)(const Info *) = nullptr;
char *(*g_type_name)(const Type *) = nullptr;
void (*g_free)(void *) = nullptr;
const Info *(*g_methods)(Klass *, void **) = nullptr;
Field *(*g_fields)(Klass *, void **) = nullptr;
Klass *(*g_parent)(Klass *) = nullptr;
const char *(*g_method_name)(const Info *) = nullptr;
uint32_t (*g_count)(const Info *) = nullptr;
void (*g_static_get)(Field *, void *) = nullptr;
uint32_t (*g_field_flags)(Field *) = nullptr;
const char *(*g_field_name)(Field *) = nullptr;
const Type *(*g_field_type)(Field *) = nullptr;
size_t (*g_field_offset)(Field *) = nullptr;
std::string type_name(const Type *type) {
    if (!type || !g_type_name)
        return "?";
    char *raw = g_type_name(type);
    std::string value = raw ? raw : "?";
    if (raw && g_free)
        g_free(raw);
    return value;
}
std::string signature(const Info *info) {
    if (!info)
        return "missing";
    std::string value = type_name(g_return(info)) + " " + g_method_name(info) + "(";
    for (unsigned i = 0; i < g_count(info); ++i) {
        if (i)
            value += ",";
        value += type_name(g_param(info, i));
    }
    return value + ")";
}
bool interesting(const char *name) {
    std::string lower = name ? name : "";
    if (lower.starts_with("__Hotfix"))
        return false;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const char *word : {"damage", "hurt", "attack", "hit", "hp", "life", "cost", "game",
                             "init", "move", "health", "scheduler", "side"})
        if (lower.find(word) != std::string::npos)
            return true;
    return false;
}
void dump_class(const BNM::Class &klass) {
    if (!klass.IsValid()) {
        LOGE("METADATA missing class");
        return;
    }
    LOGI("METADATA class=%s", klass.str().c_str());
    Klass *current = klass.GetClass();
    for (int parent = 0; current && parent < 10; ++parent, current = g_parent(current)) {
        LOGI("METADATA raw class=%p methods=%p count=%u parent=%p", current, current->methods,
             current->method_count, current->parent);
        void *iter = nullptr;
        for (int n = 0; n < 2048; ++n) {
            const Info *method = g_methods(current, &iter);
            if (!method)
                break;
            if (interesting(g_method_name(method)) ||
                klass.GetClass()->name == std::string("Modifier") ||
                klass.GetClass()->name == std::string("FP") ||
                klass.GetClass()->name == std::string("BattleFormula") ||
                klass.GetClass()->name == std::string("Scheduler") ||
                klass.GetClass()->name == std::string("SideType") ||
                klass.GetClass()->name == std::string("PlayerSide") ||
                klass.GetClass()->name == std::string("GameResult") ||
                klass.GetClass()->name == std::string("AttackInfo"))
                LOGI("METADATA method %s::%s @%p info=%p", klass.str().c_str(),
                     signature(method).c_str(), reinterpret_cast<void *>(method->methodPointer),
                     method);
        }
        iter = nullptr;
        for (int n = 0; n < 2048; ++n) {
            Field *field = g_fields(current, &iter);
            if (!field)
                break;
            if ((g_field_flags(field) & 0x40) &&
                type_name(g_field_type(field)) != "System.String") {
                int64_t value = 0;
                g_static_get(field, &value);
                LOGI("METADATA constant %s::%s=%lld", klass.str().c_str(), g_field_name(field),
                     (long long)value);
            }
            if (interesting(g_field_name(field)) ||
                klass.GetClass()->name == std::string("Modifier") ||
                klass.GetClass()->name == std::string("FP") ||
                klass.GetClass()->name == std::string("Scheduler") ||
                klass.GetClass()->name == std::string("SideType") ||
                klass.GetClass()->name == std::string("PlayerSide") ||
                klass.GetClass()->name == std::string("GameResult") ||
                klass.GetClass()->name == std::string("AttackInfo"))
                LOGI("METADATA field %s::%s type=%s offset=%zu", klass.str().c_str(),
                     g_field_name(field), type_name(g_field_type(field)).c_str(),
                     g_field_offset(field));
        }
    }
}
// Resolve complete managed signatures. The reference's selectors are checked
// against this game's exported metadata rather than only name/argument count.
const Info *exact_method(const BNM::Class &klass, const char *name, const char *result,
                         std::initializer_list<const char *> params) {
    const Info *found = nullptr;
    for (Klass *current = klass.GetClass(); current; current = g_parent(current)) {
        void *iter = nullptr;
        while (const Info *method = g_methods(current, &iter)) {
            if (strcmp(g_method_name(method), name) || g_count(method) != params.size() ||
                type_name(g_return(method)) != result)
                continue;
            unsigned index = 0;
            bool matches = true;
            for (auto type : params)
                if (type_name(g_param(method, index++)) != type)
                    matches = false;
            if (!matches)
                continue;
            if (found && found != method) {
                LOGE("ambiguous method %s", name);
                return nullptr;
            }
            found = method;
        }
        if (found)
            break;
    }
    if (found)
        LOGI("TARGET %s @%p", signature(found).c_str(),
             reinterpret_cast<void *>(found->methodPointer));
    else
        LOGE("TARGET missing %s::%s", klass.IsValid() ? klass.GetClass()->name : "?", name);
    return found;
}
Field *exact_field(const BNM::Class &klass, const char *name, const char *type) {
    for (Klass *current = klass.GetClass(); current; current = g_parent(current)) {
        void *iter = nullptr;
        while (Field *field = g_fields(current, &iter)) {
            if (!strcmp(g_field_name(field), name) && type_name(g_field_type(field)) == type &&
                !(g_field_flags(field) & 0x10))
                return field;
        }
    }
    LOGE("TARGET field missing %s type=%s", name, type);
    return nullptr;
}
template <typename Fn> Fn callable(const Info *method) {
    return method ? reinterpret_cast<Fn>(method->methodPointer) : nullptr;
}
template <typename Fn>
bool install(const char *feature, const Info *method, Fn replacement, Fn &original) {
    const int result =
        method && method->methodPointer
            ? DobbyHook(reinterpret_cast<void *>(method->methodPointer),
                        reinterpret_cast<void *>(replacement), reinterpret_cast<void **>(&original))
            : -1;
    LOGI("FEATURE %s hook=%s status=%d", feature,
         result == 0 && original ? "installed" : "unavailable", result);
    return result == 0 && original;
}
void (*g_field_get)(Object *, Field *, void *) = nullptr;
void (*g_field_set)(Object *, Field *, void *) = nullptr;
Object *(*g_invoke)(const Info *, void *, void **, Object **) = nullptr;
void *(*g_unbox)(Object *) = nullptr;
int32_t (*g_value_size)(Klass *, uint32_t *) = nullptr;
using StartFn = void (*)(Object *, const Info *);
using FinishFn = void (*)(Object *, int32_t, bool, Object *, const Info *);
using SideFn = int32_t (*)(Object *, const Info *);
using CostFn = int32_t (*)(Object *, int32_t, int32_t, const Info *);
using ModifyCostFn = bool (*)(Object *, int32_t, Object *, int32_t, bool, bool, bool, bool, bool,
                              const Info *);
using GetCostFn = int32_t (*)(Object *, int32_t, const Info *);
using LifeFn = bool (*)(Object *, int32_t, Object *, int32_t, bool, const Info *);
using SpeedFn = void (*)(float, const Info *);
using ScaleFn = float (*)(const Info *);
struct Vector2 {
    float x, y;
};
using MoveFn = Vector2 (*)(Object *, float, bool *, const Info *);
using ResultFn = int32_t (*)(Object *, const Info *);
StartFn old_start = nullptr, old_ready = nullptr;
FinishFn old_finish = nullptr;
CostFn old_cost = nullptr;
ModifyCostFn old_modify_cost = nullptr;
GetCostFn get_cost = nullptr;
const Info *get_cost_info = nullptr;
LifeFn old_life = nullptr;
SpeedFn old_speed = nullptr;
ScaleFn read_scale = nullptr;
const Info *scale_info = nullptr;
MoveFn old_move = nullptr;
ResultFn old_result = nullptr;
SideFn controller_side = nullptr, entity_side = nullptr;
const Info *controller_side_info = nullptr, *entity_side_info = nullptr, *cost_info = nullptr,
           *finish_info = nullptr, *speed_info = nullptr;
Object *active_controller = nullptr;
bool battle_active = false;
bool instant_requested = false;
float requested_scale = 1.f;
Field *scheduler_field = nullptr;
Field *total_enemies = nullptr;
Field *finish_counters[4]{};
const Info *obscured_from_int = nullptr;
constexpr const char *kObscuredInt = "CodeStage.AntiCheat.ObscuredTypes.ObscuredInt";
bool sync_finish_counters(Object *controller) {
    if (!scheduler_field || !total_enemies || !obscured_from_int)
        return false;
    Object *scheduler = nullptr;
    g_field_get(controller, scheduler_field, &scheduler);
    if (!scheduler)
        return false;
    int32_t total = 0;
    g_field_get(scheduler, total_enemies, &total);
    if (total < 0 || total > 100000)
        return false;
    for (Field *field : finish_counters) {
        if (!field)
            return false;
        void *args[]{&total};
        Object *exception = nullptr;
        Object *boxed = g_invoke(obscured_from_int, nullptr, args, &exception);
        if (!boxed || exception) {
            LOGE("FINISH counter conversion failed exception=%p", exception);
            return false;
        }
        g_field_set(scheduler, field, g_unbox(boxed));
    }
    LOGI("FINISH synchronized scheduler counters total=%d", total);
    return true;
}
int32_t hook_cost(Object *self, int32_t value, int32_t side, const Info *method) {
    int32_t applied = value;
    if (controller_side && side == controller_side(self, controller_side_info))
        applied = 999;
    int32_t result = old_cost(self, applied, side, method);
    static int logs = 0;
    if (logs++ < 8)
        LOGI("HIT cost requested=%d applied=%d side=%d result=%d", value, applied, side, result);
    return result;
}
bool hook_modify_cost(Object *self, int32_t delta, Object *source, int32_t side, bool a, bool b,
                      bool c, bool d, bool e, const Info *method) {
    static thread_local bool replenishing = false;
    if (replenishing || !controller_side || side != controller_side(self, controller_side_info))
        return old_modify_cost(self, delta, source, side, a, b, c, d, e, method);
    struct Guard {
        bool &flag;
        Guard(bool &f) : flag(f) {
            flag = true;
        }
        ~Guard() {
            flag = false;
        }
    } guard(replenishing);
    old_cost(self, 999, side, cost_info);
    const bool result = old_modify_cost(self, delta, source, side, a, b, c, d, e, method);
    const int spent = get_cost(self, side, get_cost_info);
    old_cost(self, 999, side, cost_info);
    static std::atomic<unsigned> logs{0};
    if (logs.fetch_add(1, std::memory_order_relaxed) < 16)
        LOGI("HIT cost delta=%d after-operation=%d replenished=%d result=%d", delta, spent,
             get_cost(self, side, get_cost_info), result);
    return result;
}
bool hook_life(Object *self, int32_t value, Object *source, int32_t side, bool show,
               const Info *method) {
    const int32_t applied = akl::protected_life_delta(
        value, controller_side && side == controller_side(self, controller_side_info));
    bool result = old_life(self, applied, source, side, show, method);
    LOGI("HIT life requested=%d applied=%d side=%d result=%d", value, applied, side, result);
    return result;
}
void hook_speed(float value, const Info *method) {
    requested_scale = value;
    const float applied = akl::battle_scale(value, g_config.speed, battle_active);
    old_speed(applied, method);
    static int logs = 0;
    if (logs++ < 24)
        LOGI("HIT speed requested=%.3f applied=%.3f observed=%.3f battle=%d", value, applied,
             read_scale ? read_scale(scale_info) : -1.f, battle_active);
}
Vector2 hook_move(Object *self, float delta, bool *reached, const Info *method) {
    const bool enemy = entity_side && entity_side(self, entity_side_info) == 2;
    Vector2 result = old_move(self, enemy ? 0.f : delta, reached, method);
    static int logs = 0;
    if (enemy && logs++ < 8)
        LOGI("HIT freeze delta=%.4f->0 position=%.3f,%.3f", delta, result.x, result.y);
    return result;
}
int32_t hook_result(Object *self, const Info *method) {
    const int32_t result = old_result(self, method);
    return akl::winning_result(result); // Preserve NOT_YET; WIN=1, LOSE=2 from metadata.
}
void hook_finish(Object *self, int32_t result, bool immediate, Object *callback,
                 const Info *method) {
    if ((g_config.mask & (16 | 64)) && result != 0) {
        if (g_config.mask & 16)
            result = 1;
        if (!sync_finish_counters(self))
            LOGE("FINISH counters not synchronized");
    }
    LOGI("HIT finish result=%d immediate=%d", result, immediate);
    old_finish(self, result, immediate, callback, method);
    battle_active = false;
    active_controller = nullptr;
    if (old_speed)
        old_speed(requested_scale, speed_info);
}
void hook_start(Object *self, const Info *method) {
    instant_requested = false;
    battle_active = true;
    active_controller = self;
    old_start(self, method); // Original module also starts before auto-finish.
    LOGI("HIT start mask=0x%x", g_config.mask);
    if (old_speed && battle_active) {
        old_speed(requested_scale * (g_config.speed / 100.f), speed_info);
        LOGI("HIT speed battle-start observed=%.3f", read_scale ? read_scale(scale_info) : -1.f);
    }
    if ((g_config.mask & 2) && old_cost && battle_active)
        old_cost(self, 999, controller_side(self, controller_side_info), cost_info);
}
void hook_ready(Object *self, const Info *method) {
    old_ready(self, method);
    if (!(g_config.mask & 64) || instant_requested || !old_finish)
        return;
    if (!sync_finish_counters(self)) {
        LOGE("INSTANT aborted: scheduler/counter conversion not ready");
        return;
    }
    instant_requested = true;
    LOGI("INSTANT requesting WIN after game start callback");
    hook_finish(self, 1, false, nullptr, finish_info);
}
// Modifier is a managed value type. Its methodPointer takes unboxed storage,
// not an Il2CppObject header. FP is an eight-byte Q32.32 integer (x0 ABI).
struct Fixed {
    int64_t raw;
};
static_assert(sizeof(Fixed) == 8);
using DeltaFn = Fixed (*)(void *, const Info *);
using DamageKindFn = bool (*)(void *, const Info *);
using EntityGetterFn = Object *(*)(void *, const Info *);
using ApplyFn = bool (*)(Object *, void *, bool, const Info *);
using HpFn = Fixed (*)(Object *, const Info *);
DeltaFn old_delta = nullptr;
ApplyFn old_apply = nullptr;
DamageKindFn modifier_is_damage = nullptr;
EntityGetterFn modifier_source = nullptr, modifier_target = nullptr;
HpFn entity_hp = nullptr;
const Info *is_damage_info = nullptr, *source_info = nullptr, *target_info = nullptr,
           *hp_info = nullptr;
struct DamageScope {
    Object *entity;
    void *modifier;
    DamageScope *previous;
    bool observed = false, changed = false, is_damage = false;
    int source_side = -1, target_side = -1;
    int64_t original = 0, scaled = 0;
};
thread_local DamageScope *damage_scope = nullptr;
struct ScopeGuard {
    DamageScope state;
    ScopeGuard(Object *entity, void *modifier) : state{entity, modifier, damage_scope} {
        damage_scope = &state;
    }
    ~ScopeGuard() {
        damage_scope = state.previous;
    }
};
Fixed hook_delta(void *modifier, const Info *method) {
    Fixed result = old_delta(modifier, method);
    DamageScope *scope = damage_scope;
    if (!scope || scope->modifier != modifier || !modifier)
        return result;
    scope->observed = true;
    scope->is_damage = modifier_is_damage(modifier, is_damage_info);
    scope->original = scope->scaled = result.raw;
    Object *source = modifier_source(modifier, source_info);
    Object *target = modifier_target(modifier, target_info);
    if (!source || !target || target != scope->entity)
        return result;
    scope->source_side = entity_side(source, entity_side_info);
    scope->target_side = entity_side(target, entity_side_info);
    if (!akl::eligible_damage(scope->is_damage, scope->source_side, scope->target_side, result.raw))
        return result;
    result.raw = akl::scale_damage(result.raw, (g_config.mask & 32) ? g_config.damage : 10);
    scope->scaled = result.raw;
    scope->changed = true;
    return result;
}
bool hook_apply(Object *self, void *modifier, bool argument, const Info *method) {
    ScopeGuard guard(self, modifier);
    Fixed before = entity_hp ? entity_hp(self, hp_info) : Fixed{};
    const bool applied = old_apply(self, modifier, argument, method);
    if (guard.state.observed) {
        Fixed after = entity_hp ? entity_hp(self, hp_info) : Fixed{};
        // Full-health regeneration should not exhaust the healing evidence budget.
        if (!guard.state.is_damage && before.raw == after.raw)
            return applied;
        static std::atomic<unsigned> eligible_logs{0}, heal_logs{0}, other_logs{0};
        auto &counter = guard.state.changed     ? eligible_logs
                        : guard.state.is_damage ? other_logs
                                                : heal_logs;
        if (counter.fetch_add(1, std::memory_order_relaxed) < 24) {
            constexpr double unit = 4294967296.0;
            LOGI("HIT modifier damage=%d sides=%d->%d eligible=%d delta=%.4f scaled=%.4f "
                 "hp=%.4f->%.4f applied=%d",
                 guard.state.is_damage, guard.state.source_side, guard.state.target_side,
                 guard.state.changed, guard.state.original / unit, guard.state.scaled / unit,
                 before.raw / unit, after.raw / unit, applied);
        }
    }
    return applied;
}
void install_damage(const BNM::Class &entity, const BNM::Image &image) {
    BNM::Class modifier("Torappu.Battle", "Modifier", image);
    auto delta = exact_method(modifier, "get_finalDelta", "Torappu.FP", {});
    auto apply = exact_method(entity, "DoApplyModifier", "System.Boolean",
                              {"Torappu.Battle.Modifier&", "System.Boolean"});
    is_damage_info = exact_method(modifier, "get_isDamage", "System.Boolean", {});
    source_info = exact_method(modifier, "get_source", "Torappu.Battle.Entity", {});
    target_info = exact_method(modifier, "get_target", "Torappu.Battle.Entity", {});
    hp_info = exact_method(entity, "get_hp", "Torappu.FP", {});
    modifier_is_damage = callable<DamageKindFn>(is_damage_info);
    modifier_source = callable<EntityGetterFn>(source_info);
    modifier_target = callable<EntityGetterFn>(target_info);
    entity_hp = callable<HpFn>(hp_info);
    uint32_t alignment = 0;
    const bool valid_fp =
        delta && g_value_size(BNM::Class(g_return(delta)).GetClass(), &alignment) == sizeof(Fixed);
    if (!valid_fp || !apply || !modifier_is_damage || !modifier_source || !modifier_target ||
        !entity_side || !entity_hp) {
        LOGE("FEATURE final-damage unavailable: metadata/FP ABI validation failed");
        return;
    }
    if (install("damage-final-delta", delta, hook_delta, old_delta))
        install("damage-apply-scope", apply, hook_apply, old_apply);
}

void install_features() {
    const BNM::Image image("Assembly-CSharp");
    const BNM::Class controller("Torappu.Battle", "BattleController", image);
    const BNM::Class enemy("Torappu.Battle", "Enemy", image);
    const BNM::Class entity("Torappu.Battle", "Entity", image);
    const BNM::Class scheduler("Torappu.Battle", "Scheduler", image);
    controller_side_info = exact_method(controller, "get_playerSide", "Torappu.PlayerSide", {});
    controller_side = callable<SideFn>(controller_side_info);
    entity_side_info = exact_method(entity, "get_side", "Torappu.Battle.SideType", {});
    entity_side = callable<SideFn>(entity_side_info);
    cost_info =
        exact_method(controller, "SetCost", "System.Int32", {"System.Int32", "Torappu.PlayerSide"});
    finish_info = exact_method(
        controller, "FinishGame", "System.Void",
        {"Torappu.Battle.BattleController.GameResult", "System.Boolean", "System.Action"});
    auto start = exact_method(controller, "StartGame", "System.Void", {});
    if ((g_config.mask & 2) && controller_side && install("cost", cost_info, hook_cost, old_cost)) {
        get_cost_info = exact_method(controller, "GetCost", "System.Int32", {"Torappu.PlayerSide"});
        get_cost = callable<GetCostFn>(get_cost_info);
        if (get_cost)
            install("cost-replenish",
                    exact_method(controller, "ModifyCost", "System.Boolean",
                                 {"System.Int32", "Torappu.Battle.Entity", "Torappu.PlayerSide",
                                  "System.Boolean", "System.Boolean", "System.Boolean",
                                  "System.Boolean", "System.Boolean"}),
                    hook_modify_cost, old_modify_cost);
    }
    if ((g_config.mask & 4) && controller_side)
        install("life",
                exact_method(controller, "ModifyLifePoint", "System.Boolean",
                             {"System.Int32", "Torappu.Battle.Entity", "Torappu.PlayerSide",
                              "System.Boolean"}),
                hook_life, old_life);
    if ((g_config.mask & 8) && entity_side)
        install("freeze",
                exact_method(enemy, "_MoveByRoute", "UnityEngine.Vector2",
                             {"System.Single", "System.Boolean&"}),
                hook_move, old_move);
    if (g_config.mask & 16)
        install("win",
                exact_method(controller, "get_gameResult",
                             "Torappu.Battle.BattleController.GameResult", {}),
                hook_result, old_result);
    if (g_config.mask & (16 | 64)) {
        scheduler_field = exact_field(controller, "_scheduler", "Torappu.Battle.Scheduler");
        total_enemies = exact_field(scheduler, "<totalEnemiesCnt>k__BackingField", "System.Int32");
        const char *names[]{"m_finishedEnemiesCnt", "m_killedEnemiesCnt",
                            "m_validFinishedEnemiesCnt", "m_validKilledEnemiesCnt"};
        for (int i = 0; i < 4; ++i)
            finish_counters[i] = exact_field(scheduler, names[i], kObscuredInt);
        if (finish_counters[0]) {
            BNM::Class obscured(g_field_type(finish_counters[0]));
            uint32_t alignment = 0;
            LOGI("FINISH obscured value size=%d", g_value_size(obscured.GetClass(), &alignment));
            obscured_from_int =
                exact_method(obscured, "op_Implicit", kObscuredInt, {"System.Int32"});
        }
    }
    if (g_config.mask & 1) {
        BNM::Class time("UnityEngine", "Time", BNM::Image("UnityEngine.CoreModule"));
        speed_info = exact_method(time, "set_timeScale", "System.Void", {"System.Single"});
        scale_info = exact_method(time, "get_timeScale", "System.Single", {});
        read_scale = callable<ScaleFn>(scale_info);
        install("speed", speed_info, hook_speed, old_speed);
    }
    char trace_damage[PROP_VALUE_MAX]{};
    __system_property_get("debug.akl.damage_trace", trace_damage);
    if ((g_config.mask & 32) || !strcmp(trace_damage, "1"))
        install_damage(entity, image);
    install("finish-lifecycle", finish_info, hook_finish, old_finish);
    install("start-lifecycle", start, hook_start, old_start);
    if (g_config.mask & 64)
        install("instant-ready", exact_method(controller, "<StartGame>b__392_0", "System.Void", {}),
                hook_ready, old_ready);
    LOGI("FEATURE install-complete mask=0x%x; effects require battle verification", g_config.mask);
}

static_assert(offsetof(Klass, method_count) == 0x120);
static_assert(offsetof(Klass, field_count) == 0x124);
static_assert(offsetof(Klass, rank) == 0x132);
static_assert(offsetof(Klass, vtable) == 0x140);
void on_loaded() {
    LOGI("BNM ready on il2cpp thread; Unity profile=2021.3.39f1");
    const BNM::Image image("Assembly-CSharp");
    LOGI("METADATA image=%s", image.str().data());
    char inspect[PROP_VALUE_MAX]{};
    __system_property_get("debug.akl.metadata", inspect);
    if (!strcmp(inspect, "1")) {
        for (const char *name : {"BattleController", "Enemy", "Entity", "Modifier", "BattleFormula",
                                 "Scheduler", "BattleCostManager"})
            dump_class(BNM::Class("Torappu.Battle", name, image));
        dump_class(BNM::Class("Torappu", "FP"));
        dump_class(BNM::Class("Torappu", "PlayerSide"));
        dump_class(BNM::Class("Torappu.Battle", "SideType", image));
        dump_class(
            BNM::Class("Torappu.Battle", "BattleController", image).GetInnerClass("GameResult"));
        dump_class(
            BNM::Class("Torappu.Battle", "BattleFormula", image).GetInnerClass("AttackInfo"));
        dump_class(BNM::Class("UnityEngine", "Time", BNM::Image("UnityEngine.CoreModule")));
    }
    install_features();
}
void *find_symbol(const char *name, void *handle) {
    return dlsym(handle, name);
}
void load_runtime() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (!g_il2cpp && std::chrono::steady_clock::now() < deadline) {
        g_il2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
        if (!g_il2cpp)
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (!g_il2cpp) {
        LOGE("FEATURE state=failed reason=il2cpp-not-loaded");
        return;
    }
    bool api_ok = true;
#define API(var, name)                                                                             \
    var = reinterpret_cast<decltype(var)>(dlsym(g_il2cpp, name));                                  \
    if (!var) {                                                                                    \
        LOGE("Missing il2cpp API: %s", name);                                                      \
        api_ok = false;                                                                            \
    }
    API(g_param, "il2cpp_method_get_param");
    API(g_return, "il2cpp_method_get_return_type");
    API(g_type_name, "il2cpp_type_get_name");
    API(g_free, "il2cpp_free");
    API(g_methods, "il2cpp_class_get_methods");
    API(g_fields, "il2cpp_class_get_fields");
    API(g_parent, "il2cpp_class_get_parent");
    API(g_method_name, "il2cpp_method_get_name");
    API(g_count, "il2cpp_method_get_param_count");
    API(g_field_name, "il2cpp_field_get_name");
    API(g_field_type, "il2cpp_field_get_type");
    API(g_field_offset, "il2cpp_field_get_offset");
    API(g_static_get, "il2cpp_field_static_get_value");
    API(g_field_flags, "il2cpp_field_get_flags");
    API(g_field_get, "il2cpp_field_get_value");
    API(g_field_set, "il2cpp_field_set_value");
    API(g_invoke, "il2cpp_runtime_invoke");
    API(g_unbox, "il2cpp_object_unbox");
    API(g_value_size, "il2cpp_class_value_size");
#undef API
    if (!api_ok) {
        LOGE("FEATURE state=failed reason=metadata-api-missing");
        return;
    }
    BNM::Loading::SetMethodFinder(find_symbol, g_il2cpp);
    BNM::Loading::AllowLateInitHook();
    BNM::Loading::AddOnLoadedEvent(on_loaded);
    const bool pending = BNM::Loading::TryLoadByUsersFinder();
    LOGI("BNM setup scheduled=%d; awaiting managed runtime callback", pending);
}
int start_engine(jint mask, jint speed, jint damage) {
    if (!akl_hook_smoke())
        return akl::kNoHooks;
    if (g_started.exchange(true))
        return akl::kPending;
    g_config = {mask, std::clamp<int>(speed, 100, 1000), std::clamp<int>(damage, 10, 200)};
    LOGI("arm64 engine config mask=0x%x speed=%d final-damage=%d", mask, speed, damage);
    std::thread(load_runtime).detach();
    return akl::kPending;
}
void native_start(JNIEnv *, jclass, jint mask, jint speed, jint damage) {
    LOGI("arm64 engine scheduled status=%d", start_engine(mask, speed, damage));
}
} // namespace
extern "C" JNIEXPORT jint JNICALL Java_dev_ak_ak_1loader_GuestEngine_probe(JNIEnv *, jclass) {
    LOGI("AKL-GUEST: arm64 probe executed; checking Dobby relocation and translated code "
         "invalidation");
    return akl_hook_smoke() ? akl::kGuestProbe : 0;
}
extern "C" JNIEXPORT jint JNICALL Java_dev_ak_ak_1loader_GuestEngine_run(JNIEnv *, jclass,
                                                                         jint mask, jint speed,
                                                                         jint damage) {
    return start_engine(mask, speed, damage);
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    jclass bridge = env->FindClass("dev/ak/ak_loader/NativeBridge");
    if (!bridge)
        return JNI_ERR;
    const JNINativeMethod methods[] = {{"start", "(III)V", reinterpret_cast<void *>(native_start)}};
    if (env->RegisterNatives(bridge, methods, 1) != JNI_OK)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
