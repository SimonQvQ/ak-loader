#include "hook.h"

#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

#define TAG "AKL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

constexpr size_t kStub = 16;   // ldr x16,#8 ; br x16 ; .quad addr

// 需要搬迁时必须特殊处理的 PC 相对指令（保守起见直接拒绝）
bool is_pc_relative(uint32_t i) {
    if ((i & 0x7C000000u) == 0x14000000u) return true;  // B / BL
    if ((i & 0x7E000000u) == 0x34000000u) return true;  // CBZ / CBNZ
    if ((i & 0x7E000000u) == 0x36000000u) return true;  // TBZ / TBNZ
    if ((i & 0x1F000000u) == 0x10000000u) return true;  // ADR / ADRP
    if ((i & 0x3B000000u) == 0x18000000u) return true;  // LDR (literal)，V 位无关，覆盖 SIMD
    return false;
}

void emit_jump(uint8_t* p, void* dst) {
    uint32_t* w = reinterpret_cast<uint32_t*>(p);
    w[0] = 0x58000050u;                       // ldr x16, #8
    w[1] = 0xD61F0200u;                       // br  x16
    std::memcpy(p + 8, &dst, sizeof(void*));  // .quad dst
}

bool write_code(void* dst, const void* src, size_t len) {
    long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<uintptr_t>(dst) & ~(uintptr_t)(page - 1);
    auto end   = (reinterpret_cast<uintptr_t>(dst) + len + page - 1) & ~(uintptr_t)(page - 1);
    if (mprotect(reinterpret_cast<void*>(start), end - start,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("mprotect RWX 失败 @%p", dst);
        return false;
    }
    std::memcpy(dst, src, len);
    __builtin___clear_cache(reinterpret_cast<char*>(dst),
                            reinterpret_cast<char*>(dst) + len);
    mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_EXEC);
    return true;
}

}  // namespace

bool hook_install(InlineHook& h, void* target, void* replace) {
    if (!target || !replace) return false;

    uint32_t prologue[4] = {};
    std::memcpy(prologue, target, kStub);
    for (uint32_t insn : prologue) {
        if (is_pc_relative(insn)) {
            LOGE("序言含 PC 相对指令 0x%08x，放弃 hook @%p", insn, target);
            return false;
        }
    }

    void* mem = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { LOGE("mmap 失败"); return false; }

    // 跳板 = 原序言 + 跳回 target+16
    std::memcpy(mem, prologue, kStub);
    emit_jump(static_cast<uint8_t*>(mem) + kStub, static_cast<uint8_t*>(target) + kStub);
    mprotect(mem, 4096, PROT_READ | PROT_EXEC);

    uint8_t stub[kStub];
    emit_jump(stub, replace);
    if (!write_code(target, stub, kStub)) return false;

    h.target = target;
    h.tramp  = mem;
    h.ok     = true;
    LOGI("hook 安装成功 target=%p replace=%p tramp=%p", target, replace, mem);
    return true;
}
