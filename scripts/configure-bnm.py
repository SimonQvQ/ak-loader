"""Configure the pinned upstream BNM in the ignored dependency cache."""
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1])
def write_changed(name, content):
    path = root / name
    if not path.exists() or path.read_text() != content:
        path.write_text(content)
name = "include/BNM/UserSettings/GlobalSettings.hpp"
settings = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
settings = settings.replace("#define UNITY_VER 222", "#define UNITY_VER 213")
settings = settings.replace("#define UNITY_PATCH_VER 32", "#define UNITY_PATCH_VER 39")
settings = settings.replace("#define BNM_CLASSES_MANAGEMENT\n", "// Classes management is not required by ZOOT.\n")
settings = settings.replace("#define BNM_COROUTINE\n", "// Coroutine injection is not required by ZOOT.\n")
settings = settings.replace("#ifndef NDEBUG", "#define BNM_DEBUG\n#define BNM_ERROR\n#define BNM_INFO\n#define BNM_WARNING\n#ifndef NDEBUG", 1)
start = settings.index("// Dobby\n")
end = settings.index("#include <dlfcn.h>", start)
settings = settings[:start] + """// Dobby: the upstream-supported hook adapter, with checked errors.
#include <dobby.h>
#include <android/log.h>
template<typename P, typename N, typename O>
inline void *BasicHook(P ptr, N replacement, O &original) {
    if (!ptr) return nullptr;
    int result = DobbyHook((void *)ptr, (void *)replacement, (void **)&original);
    __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM hook target=%p replacement=%p original=%p result=%d", (void*)ptr, (void*)replacement, (void*)original, result);
    if (result != 0) return nullptr;
    return (void *)ptr;
}
template<typename P, typename N, typename O>
inline void *BasicHook(P ptr, N replacement, O &&original) {
    if (!ptr) return nullptr;
    int result = DobbyHook((void *)ptr, (void *)replacement, (void **)&original);
    __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM hook target=%p replacement=%p original=%p result=%d", (void*)ptr, (void*)replacement, (void*)original, result);
    if (result != 0) return nullptr;
    return (void *)ptr;
}
template<typename P> inline void Unhook(P ptr) { if (ptr) DobbyDestroy((void *)ptr); }

""" + settings[end:]
settings += "\n#define BNM_ALLOW_STR_METHODS\n"
write_changed(name, settings)

# Keep diagnostics source-bound: recreate from the pinned revision each time.
name = "src/Hooks.cpp"
hooks = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
hooks = hooks.replace('    auto ret = old_BNM_il2cpp_init(domain_name);', '    __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM init entry");\n    auto ret = old_BNM_il2cpp_init(domain_name);\n    __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM init returned");')
hooks = hooks.replace('    auto klass = old_BNM_Class$$FromIl2CppType(type);', '    static bool first = true;\n    bool trace = first; first = false;\n    if (trace) __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM late entry type=%p", type);\n    auto klass = old_BNM_Class$$FromIl2CppType(type);\n    if (trace) __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM late original returned=%p", klass);')
hooks = hooks.replace('    auto domain = Internal::il2cppMethods.il2cpp_domain_get();', '    auto domain = Internal::il2cppMethods.il2cpp_domain_get();\n    if (trace) __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM late domain=%p", domain);')
hooks = hooks.replace('    // Will be true after il2cpp_init', '    if (trace) __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM late thread=%p", thread);\n    // Will be true after il2cpp_init')
hooks = hooks.replace('    Load();', '    __android_log_print(ANDROID_LOG_INFO, "AKL", "BNM loading");\n    Load();')
hooks = hooks.replace('#include <Internals.hpp>', '#include <Internals.hpp>\n#include <atomic>')
hooks = hooks.replace('    auto thread = Internal::il2cppMethods.il2cpp_thread_current(domain);', '    if (!domain) return klass;\n    auto thread = Internal::il2cppMethods.il2cpp_thread_current(domain);')
hooks = hooks.replace('    Unhook(BNM_il2cpp_init_origin);\n    Unhook(BNM_Class$$FromIl2CppType_origin);', '    static std::atomic_flag loading = ATOMIC_FLAG_INIT;\n    if (loading.test_and_set()) return klass;\n    Unhook(BNM_il2cpp_init_origin);\n    Unhook(BNM_Class$$FromIl2CppType_origin);')
write_changed(name, hooks)

name = "src/Loading.cpp"
source = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
# On Houdini, avoid interposing the game-protected il2cpp_init entry. The
# reference late-init callback still verifies the current domain context.
pos = source.index("bool Loading::TryLoadByUsersFinder()")
old = "Internal::BNM_il2cpp_init_origin = ::BasicHook(init, Internal::BNM_il2cpp_init, Internal::old_BNM_il2cpp_init);"
source = source[:pos] + source[pos:].replace(old, "// ZOOT: use late-init only; do not patch protected init.", 1)
a = source.index("    for (uint16_t slot = 0; slot < objectClass._data->vtable_count;")
b = source.index("    auto UnityEngineCoreModule", a)
source = source[:a] + "    // No custom objects/finalizers are created by ZOOT.\n" + source[b:]
a = source.index("    auto listClass = vmData.System$$List")
b = source.index("\n}\n\nvoid Loading::AddOnLoadedEvent", a)
source = source[:a] + "    // No custom List template is required.\n" + source[b:]
write_changed(name, source)

# Arknights 2.7.81: exported accessors establish an opaque 4-byte member after
# token; scalar sizes/flags/token retain upstream offsets. See layout report.
name = "include/BNM/Il2CppHeaders/2021.3.h"
source = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
start = source.index("typedef struct Il2CppClass\n")
prefix, body = source[:start], source[start:]
body = body.replace("    uint32_t token;\n    uint16_t method_count;", "    uint32_t token;\n    uint32_t arknights_reserved_11c;\n    uint16_t method_count;")
assert body.count("arknights_reserved_11c") == 2
# Actual game virtual dispatch adds class+0x140 (ELF 0x267fbb0).
body = body.replace("    VirtualInvokeData vtable[0];\n} Il2CppClass;", "    uint32_t arknights_reserved_138;\n    VirtualInvokeData vtable[0];\n} Il2CppClass;")
write_changed(name, prefix + body)
