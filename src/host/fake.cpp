#include "fake.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "log.h"

namespace host {

// Thunk: rcx = rcx_value, edx = edx_value, dann Sprung nach handler (32 Byte je Stück).
uint8_t* make_call_thunk(uint64_t rcx_value, uint32_t edx_value, const void* handler) {
    static uint8_t* mem = nullptr;
    static size_t used = 0;
    constexpr size_t kSize = 1 << 20;
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    if (!mem) mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem || used + 32 > kSize) return nullptr;
    uint8_t* p = mem + used;
    uint8_t* q = p;
    *q++ = 0x48; *q++ = 0xB9; std::memcpy(q, &rcx_value, 8); q += 8;           // mov rcx, imm64
    *q++ = 0xBA; std::memcpy(q, &edx_value, 4); q += 4;                         // mov edx, imm32
    *q++ = 0x48; *q++ = 0xB8; uint64_t h = uint64_t(handler); std::memcpy(q, &h, 8); q += 8;  // mov rax, imm64
    *q++ = 0xFF; *q++ = 0xE0;                                                   // jmp rax
    used += 32;
    return p;
}

struct Fake {
    std::string name;
    void** vtbl;
    uint8_t* object;  // [0] = vtbl, danach Felder
    int slots;
};

namespace {
extern "C" uint64_t host_fake_call(Fake* f, uint32_t offset) {
    static std::mutex m;
    static std::set<std::pair<Fake*, uint32_t>> seen;
    std::lock_guard<std::mutex> g(m);
    if (seen.insert({f, offset}).second) log("[gEnv] %s ->+0x%X aufgerufen (Attrappe, liefert 0)\n", f->name.c_str(), offset);
    return 0;
}
}  // namespace

Fake* make_fake(const char* name, int slots, int fields) {
    auto* f = new Fake{name, nullptr, nullptr, slots};
    f->vtbl = static_cast<void**>(std::calloc(size_t(slots), sizeof(void*)));
    for (int i = 0; i < slots; ++i)
        f->vtbl[i] = make_call_thunk(uint64_t(f), uint32_t(i * 8), reinterpret_cast<const void*>(&host_fake_call));
    f->object = static_cast<uint8_t*>(std::calloc(1, sizeof(void*) + size_t(fields)));
    *reinterpret_cast<void***>(f->object) = f->vtbl;
    return f;
}

void* fake_object(Fake* f) { return f->object; }

void fake_set(Fake* f, uint32_t offset, const void* fn) { f->vtbl[offset / 8] = const_cast<void*>(fn); }

void fake_returns(Fake* f, uint32_t offset, const void* value) {
    // mov rax, imm64 ; ret
    static uint8_t* mem = nullptr;
    static size_t used = 0;
    if (!mem) mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, 1 << 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    uint8_t* p = mem + used;
    p[0] = 0x48;
    p[1] = 0xB8;
    uint64_t v = uint64_t(value);
    std::memcpy(p + 2, &v, 8);
    p[10] = 0xC3;
    used += 16;
    fake_set(f, offset, p);
}

}  // namespace host
