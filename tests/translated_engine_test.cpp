// Tests host-side dispatch with mock platform APIs; does not emulate Houdini.
#include "translated_engine.h"
#include "engine_status.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {
struct Fake {
    bool initialized = true;
    bool has_namespace = true;
    bool load_ok = true;
    bool translated = true;
    bool probe_symbol = true;
    bool probe_ok = true;
    bool run_symbol = true;
    int guest_status = akl::kStarted;
    int opens = 0, probes = 0, runs = 0, freed_errors = 0;
    jint mask = 0, speed = 0, attack = 0;
} fake;

jobject loader() { return reinterpret_cast<jobject>(0x100); }
void* ns() { return reinterpret_cast<void*>(0x200); }
void* handle() { return reinterpret_cast<void*>(0x300); }
bool initialized() { return fake.initialized; }
void* find_namespace(JNIEnv*, jobject value) {
    assert(value == loader());
    return fake.has_namespace ? ns() : nullptr;
}
void* open_library(void* value, const char*, bool* translated, char** error) {
    assert(value == ns());
    ++fake.opens;
    *translated = fake.translated;
    *error = nullptr;
    if (!fake.load_ok) { *error = strdup("mock load failure"); return nullptr; }
    return handle();
}
void free_error(char* error) { ++fake.freed_errors; std::free(error); }
jint JNICALL probe(JNIEnv*, jclass) { ++fake.probes; return fake.probe_ok ? akl::kGuestProbe : 7; }
jint JNICALL run(JNIEnv*, jclass, jint mask, jint speed, jint attack) {
    ++fake.runs;
    fake.mask = mask; fake.speed = speed; fake.attack = attack;
    return fake.guest_status;
}
void* trampoline(void* value, const char* name, const char* shorty, uint32_t len) {
    assert(value == handle());
    if (std::strcmp(name, "Java_dev_ak_ak_1loader_GuestEngine_probe") == 0) {
        assert(std::strcmp(shorty, "I") == 0 && len == 1);
        return fake.probe_symbol ? reinterpret_cast<void*>(probe) : nullptr;
    }
    assert(std::strcmp(name, "Java_dev_ak_ak_1loader_GuestEngine_run") == 0);
    assert(std::strcmp(shorty, "IIII") == 0 && len == 4);
    return fake.run_symbol ? reinterpret_cast<void*>(run) : nullptr;
}
const char* bridge_error() { return "mock bridge error"; }
akl::BridgeApi api() {
    return {initialized, find_namespace, open_library, free_error, trampoline, bridge_error};
}
akl::BridgeResult start(akl::TranslatedEngine& engine, const char* path) {
    return engine.start(api(), nullptr, loader(), nullptr, path, 0x43, 250, 30);
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string path = std::string(argv[1]) + "/guest.so";
    unsigned char header[20]{};
    std::memcpy(header, "\177ELF", 4);
    header[4] = 2; header[5] = 1; header[18] = 183;
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<char*>(header), sizeof header);
    {
        akl::TranslatedEngine engine;
        auto broken = api(); broken.trampoline = nullptr;
        assert(engine.start(broken, nullptr, loader(), nullptr, path.c_str(), 0, 0, 0).code == akl::kBridgeUnavailable);
        assert(fake.opens == 0 && fake.runs == 0);
    }
    {
        fake = {}; fake.initialized = false;
        akl::TranslatedEngine engine;
        assert(start(engine, path.c_str()).code == akl::kBridgeUnavailable);
        assert(fake.opens == 0);
    }
    {
        fake = {};
        akl::TranslatedEngine engine;
        assert(start(engine, "/nonexistent-zoot-guest.so").code == akl::kInvalidGuest);
        assert(fake.opens == 0);
    }
    {
        fake = {}; fake.has_namespace = false;
        akl::TranslatedEngine engine;
        assert(start(engine, path.c_str()).code == akl::kNamespaceUnavailable);
        assert(fake.opens == 0);
    }
    {
        fake = {}; fake.load_ok = false;
        akl::TranslatedEngine engine;
        auto result = start(engine, path.c_str());
        assert(result.code == akl::kGuestLoadFailed && result.detail == "mock load failure");
        assert(fake.freed_errors == 1 && fake.runs == 0);
    }
    {
        fake = {}; fake.translated = false;
        akl::TranslatedEngine engine;
        assert(start(engine, path.c_str()).code == akl::kNotTranslated);
        assert(fake.probes == 0 && fake.runs == 0);
    }
    for (bool missing : {false, true}) {
        fake = {}; fake.probe_ok = false; fake.probe_symbol = !missing;
        akl::TranslatedEngine engine;
        assert(start(engine, path.c_str()).code == akl::kProbeFailed);
        assert(fake.runs == 0);
    }
    {
        fake = {}; fake.run_symbol = false;
        akl::TranslatedEngine engine;
        assert(start(engine, path.c_str()).code == akl::kEntryUnavailable);
        assert(fake.probes == 1 && fake.runs == 0);
    }
    for (int result : {akl::kStarted, akl::kPending, akl::kNoHooks, akl::kIl2cppUnavailable}) {
        fake = {}; fake.guest_status = result;
        akl::TranslatedEngine engine;
        assert(start(engine, path.c_str()).code == result);
        assert(fake.opens == 1 && fake.probes == 1 && fake.runs == 1);
        assert(fake.mask == 0x43 && fake.speed == 250 && fake.attack == 30);
        assert(start(engine, path.c_str()).code == result);
        assert(fake.runs == 1); // Even failures must not install a second set of hooks.
    }
    {
        fake = {};
        akl::TranslatedEngine engine;
        assert(engine.start(api(), nullptr, loader(), nullptr, path.c_str(), 1, 200, 10, true).code == akl::kProbePassed);
        assert(fake.probes == 1 && fake.runs == 0);
        assert(start(engine, path.c_str()).code == akl::kStarted);
        assert(fake.runs == 1);
    }
    {
        fake = {};
        akl::TranslatedEngine engine;
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&] { assert(start(engine, path.c_str()).code == akl::kStarted); });
        for (auto& thread : threads) thread.join();
        assert(fake.opens == 1 && fake.probes == 1 && fake.runs == 1);
    }
    std::cout << "host dispatch checks passed (mock platform; no Houdini/game execution)\n";
}
