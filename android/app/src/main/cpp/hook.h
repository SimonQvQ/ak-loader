#pragma once
#include <cstdint>

struct InlineHook {
    void*  target = nullptr;   // 被 hook 的函数
    void*  tramp  = nullptr;   // 跳板（原指令 + 跳回）
    bool   ok     = false;
};

/** 在 arm64 上用 16 字节绝对跳转改写函数序言，返回可调用的跳板 */
bool hook_install(InlineHook& h, void* target, void* replace);
