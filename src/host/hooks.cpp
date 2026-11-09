#include "hooks.h"

#include <windows.h>

#include <cstring>

#include "log.h"

namespace host {

namespace {
uint8_t* g_stubs = nullptr;  // Seite mit 16-Byte-Sprüngen
size_t g_used = 0;
}  // namespace

void* hook_call(const Ts2Image& img, uint64_t call_va, uint64_t expected_target, const void* fn) {
    uint8_t* site = img.va(call_va);
    int32_t rel;
    std::memcpy(&rel, site + 1, 4);
    uint8_t* target = site + 5 + rel;
    if (site[0] != 0xE8 || target != img.va(expected_target)) {
        log("[umweg] bei 0x%llX steht kein Aufruf von 0x%llX, nicht umgelenkt\n",
            static_cast<unsigned long long>(call_va), static_cast<unsigned long long>(expected_target));
        return nullptr;
    }
    if (!g_stubs) {
        // hinter der Seite mit den Ersatz-Zeigern (ts2bin.cpp), im .reloc-Bereich,
        // den ts2.bin nicht belegt
        uint8_t* want = img.base + ((img.size - 0x10000 + 0x2000) & ~0xFFFu);
        g_stubs = static_cast<uint8_t*>(VirtualAlloc(want, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!g_stubs) {
            log("[umweg] Seite für Sprünge nicht belegbar\n");
            return nullptr;
        }
    }
    if (g_used + 16 > 0x1000) return nullptr;
    uint8_t* stub = g_stubs + g_used;
    g_used += 16;
    DWORD old;
    VirtualProtect(g_stubs, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    stub[0] = 0xFF;  // jmp [rip+0]
    stub[1] = 0x25;
    std::memset(stub + 2, 0, 4);
    const uint64_t to = reinterpret_cast<uint64_t>(fn);
    std::memcpy(stub + 6, &to, 8);
    VirtualProtect(g_stubs, 0x1000, PAGE_EXECUTE_READ, &old);

    const int32_t nrel = int32_t(int64_t(stub) - int64_t(site + 5));
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(site + 1, &nrel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return target;
}

}  // namespace host
