#include "redux.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "log.h"

namespace host {

// Redux schreibt in Homefront2_Release.exe; dieselben Abstände gelten für
// ts2.bin (gleiches Bild, gleiche RVAs). Einige Änderungen sind Code-Stücke
// in Homefronts Füllbereichen (z. B. 0x140016EC8, 0x140025908), die ts2.bin
// nicht enthält; diese Seiten sind nur reserviert und werden hier angelegt.
void redux_install(const Ts2Image& img, const std::filesystem::path& game, const std::filesystem::path& ini) {
    std::ifstream f(game / L"redux.bin", std::ios::binary);
    if (!f) {
        log("Redux: game/redux.bin fehlt (tools/re/redux_patches.py), keine Redux-Fehlerbehebungen\n");
        return;
    }
    const std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t p = 0;
    auto take = [&](void* out, size_t n) {
        if (p + n > d.size()) return false;
        std::memcpy(out, d.data() + p, n);
        p += n;
        return true;
    };
    char magic[4];
    uint32_t groups = 0;
    if (!take(magic, 4) || std::memcmp(magic, "TS2R", 4) != 0 || !take(&groups, 4)) {
        log("Redux: game/redux.bin unlesbar\n");
        return;
    }
    for (uint32_t g = 0; g < groups; ++g) {
        uint8_t nlen = 0;
        uint32_t count = 0;
        std::string name(64, '\0');
        if (!take(&nlen, 1) || !take(name.data(), nlen) || !take(&count, 4)) break;
        name.resize(nlen);
        const std::wstring wname(name.begin(), name.end());
        const bool on = GetPrivateProfileIntW(L"redux", wname.c_str(), 1, ini.c_str()) != 0;
        int applied = 0, created = 0;
        for (uint32_t e = 0; e < count; ++e) {
            uint32_t rva = 0;
            uint16_t len = 0;
            if (!take(&rva, 4) || !take(&len, 2) || p + len > d.size() || rva + len > img.size) {
                log("Redux: Eintrag %u von %s kaputt\n", e, name.c_str());
                return;
            }
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(d.data() + p);
            p += len;
            if (!on) continue;
            uint8_t* at = img.rva(rva);
            // Seiten, die nur reserviert sind, anlegen (Code-Stücke)
            for (uint8_t* page = reinterpret_cast<uint8_t*>(uintptr_t(at) & ~uintptr_t(0xFFF)); page < at + len;
                 page += 0x1000) {
                MEMORY_BASIC_INFORMATION mbi{};
                VirtualQuery(page, &mbi, sizeof mbi);
                if (mbi.State == MEM_RESERVE) {
                    VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                    std::memset(page, 0xCC, 0x1000);
                    ++created;
                }
            }
            DWORD old;
            VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old);
            std::memcpy(at, bytes, len);
            VirtualProtect(at, len, old, &old);
            ++applied;
        }
        FlushInstructionCache(GetCurrentProcess(), img.base, img.size);
        log("Redux %s: %s (%d Stellen%s)\n", name.c_str(), on ? "an" : "aus", on ? applied : int(count),
            created ? ", Code-Seiten angelegt" : "");
    }
}

}  // namespace host
