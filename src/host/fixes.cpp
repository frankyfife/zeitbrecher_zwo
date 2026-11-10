#include "fixes.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "log.h"
#include "replace.h"

namespace host {

namespace {

#include "fixes.inc"

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// Seiten, die ts2.bin nicht enthält (nur reserviert), anlegen
bool ensure_committed(uint8_t* at, size_t len) {
    for (uint8_t* page = reinterpret_cast<uint8_t*>(uintptr_t(at) & ~uintptr_t(0xFFF)); page < at + len; page += 0x1000) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(page, &mbi, sizeof mbi)) return false;
        if (mbi.State == MEM_RESERVE) {
            if (!VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return false;
            std::memset(page, 0xCC, 0x1000);
        } else if (mbi.State != MEM_COMMIT) {
            return false;
        }
    }
    return true;
}

void write(uint8_t* at, const void* src, size_t len) {
    DWORD old;
    VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, src, len);
    VirtualProtect(at, len, old, &old);
}

// eigene Seite für die umgelenkten Konstanten: in Homefronts Code-Bereich,
// den ts2.bin nicht belegt (innerhalb ±2 GB der Befehle)
constexpr uint32_t kConstPoolRva = 0x00040000;

}  // namespace

void fixes_install(const Ts2Image& img, const std::filesystem::path& game) {
    std::map<std::string, int> applied, skipped;
    for (const auto& f : kFixBytes) {
        uint8_t* at = img.rva(f.rva);
        if (f.rva + f.len > img.size || !ensure_committed(at, f.len)) {
            ++skipped[f.group];
            continue;
        }
        if (f.crc && crc32(at, f.len) != f.crc) {
            log("Fehlerbehebung %s: Stelle 0x%X hat andere Bytes, ausgelassen\n", f.group, 0x140000000u + f.rva);
            ++skipped[f.group];
            continue;
        }
        write(at, f.bytes, f.len);
        ++applied[f.group];
    }
    // Konstanten: Wert in die eigene Seite, Verschiebung des Befehls darauf
    uint8_t* pool = img.rva(kConstPoolRva);
    if (ensure_committed(pool, 0x1000)) {
        size_t used = 0;
        for (const auto& c : kFixConsts) {
            uint8_t* slot = pool + used;
            write(slot, c.value, sizeof c.value);
            used += sizeof c.value;
            const int64_t disp = int64_t(kConstPoolRva + (slot - pool)) - int64_t(c.next_rva);
            const int32_t d32 = int32_t(disp);
            write(img.rva(c.disp_rva), &d32, 4);
            ++applied[c.group];
        }
        DWORD old;
        VirtualProtect(pool, 0x1000, PAGE_READONLY, &old);
    }
    // Spielspeicher: TS2 legt beim Start eine Arena fester Größe an
    // (0x141069DA0 → 0x1411A98F8, Größe in 0x1424E23D8 = 58 MB wie auf der
    // Konsole) und ruft exit(1), wenn sie voll ist (0x1411A9A10). Mit
    // hochaufgelösten Texturen reichte das in Chicago nicht. 192 MB; alle
    // Größen dort sind 32 Bit, also weit unter der Grenze.
    {
        auto* arena = img.va<uint32_t>(0x1424E23D8);
        if (*arena == 0x3A00000u) {
            *arena = 0xC000000u;
            log("Spielspeicher: 192 MB statt 58 MB\n");
        }
    }
    FlushInstructionCache(GetCurrentProcess(), img.base, img.size);
    for (const auto& [g, n] : applied)
        log("Fehlerbehebung %s: %d Stellen%s\n", g.c_str(), n, skipped[g] ? " (einige ausgelassen)" : "");
    // hochaufgelöste Texturen (tools/re/hd_textures.py nach game/hd)
    if (std::filesystem::exists(game / L"hd" / L"ts2")) {
        set_file_overlay("hd/");
        log("Texturen: hochaufgelöste aus game/hd zuerst\n");
    }
}

}  // namespace host
