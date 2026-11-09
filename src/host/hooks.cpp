#include "hooks.h"

#include <windows.h>

#include <cstring>

#include "log.h"

namespace host {

namespace {
uint8_t* g_stubs = nullptr;  // eigene Seite für Sprünge und kleine Code-Stücke
size_t g_used = 0;
constexpr size_t kStubPage = 0x1000;

// Platz in der Stub-Seite (16-Byte-ausgerichtet), mit Inhalt; nullptr bei Fehler
uint8_t* stub_alloc(const Ts2Image& img, const uint8_t* code, size_t n) {
    if (!g_stubs) {
        // hinter der Seite mit den Ersatz-Zeigern (ts2bin.cpp), im .reloc-Bereich,
        // den ts2.bin nicht belegt
        uint8_t* want = img.base + ((img.size - 0x10000 + 0x2000) & ~0xFFFu);
        g_stubs = static_cast<uint8_t*>(VirtualAlloc(want, kStubPage, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!g_stubs) {
            log("[umweg] Seite für Sprünge nicht belegbar\n");
            return nullptr;
        }
    }
    const size_t size = (n + 15) & ~size_t(15);
    if (g_used + size > kStubPage) return nullptr;
    uint8_t* stub = g_stubs + g_used;
    g_used += size;
    DWORD old;
    VirtualProtect(g_stubs, kStubPage, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(stub, code, n);
    VirtualProtect(g_stubs, kStubPage, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), stub, n);
    return stub;
}

void write_code(uint8_t* at, const void* bytes, size_t n) {
    DWORD old;
    VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
}
}  // namespace

void* hook_call(const Ts2Image& img, uint64_t call_va, uint64_t expected_target, const void* fn) {
    uint8_t* site = img.va(call_va);
    int32_t rel;
    std::memcpy(&rel, site + 1, 4);
    uint8_t* target = site + 5 + rel;
    if ((site[0] != 0xE8 && site[0] != 0xE9) || target != img.va(expected_target)) {
        log("[umweg] bei 0x%llX steht kein Aufruf von 0x%llX, nicht umgelenkt\n",
            static_cast<unsigned long long>(call_va), static_cast<unsigned long long>(expected_target));
        return nullptr;
    }
    // jmp [rip+0] mit dem Ziel dahinter
    uint8_t code[14] = {0xFF, 0x25, 0, 0, 0, 0};
    const uint64_t to = reinterpret_cast<uint64_t>(fn);
    std::memcpy(code + 6, &to, 8);
    uint8_t* stub = stub_alloc(img, code, sizeof code);
    if (!stub) return nullptr;
    const int32_t nrel = int32_t(int64_t(stub) - int64_t(site + 5));
    write_code(site + 1, &nrel, 4);  // Aufruf bleibt Aufruf, Sprung bleibt Sprung
    return target;
}

bool patch_with_call(const Ts2Image& img, uint64_t va, const uint8_t* expected, size_t n, const uint8_t* stub_code,
                     size_t stub_n) {
    uint8_t* site = img.va(va);
    if (n < 5 || std::memcmp(site, expected, n) != 0) {
        log("[umweg] bei 0x%llX stehen andere Bytes, nicht ersetzt\n", static_cast<unsigned long long>(va));
        return false;
    }
    uint8_t* stub = stub_alloc(img, stub_code, stub_n);
    if (!stub) return false;
    uint8_t patch[16];
    patch[0] = 0xE8;
    const int32_t rel = int32_t(int64_t(stub) - int64_t(site + 5));
    std::memcpy(patch + 1, &rel, 4);
    std::memset(patch + 5, 0x90, n - 5);
    write_code(site, patch, n);
    return true;
}

}  // namespace host
