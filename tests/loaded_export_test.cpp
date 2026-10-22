#include "loaded_export.h"
#include <dlfcn.h>
#include <cassert>
#include <cstdio>

int main(int argc, char** argv) {
    assert(argc == 2);
    assert(find_loaded_export(argv[1], "zoot_fixture_add") == nullptr); // Must already be mapped.
    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(handle);
    void* expected = dlsym(handle, "zoot_fixture_add");
    assert(expected);
    assert(find_loaded_export(argv[1], "zoot_fixture_add") == expected);
    assert(reinterpret_cast<int (*)(int)>(expected)(35) == 42);
    assert(find_loaded_export(argv[1], "no_such_export") == nullptr);
    assert(find_loaded_export(argv[1], "zoot_fixture_data") == nullptr); // Never call a data address.
    assert(find_loaded_export("/no/such/provider.so", "zoot_fixture_add") == nullptr);
    assert(find_loaded_export(nullptr, "zoot_fixture_add") == nullptr);
    assert(find_loaded_export(argv[1], "") == nullptr);
    dlclose(handle);
    assert(find_loaded_export(argv[1], "zoot_fixture_add") == nullptr);
    std::puts("loaded ELF export checks passed");
}
