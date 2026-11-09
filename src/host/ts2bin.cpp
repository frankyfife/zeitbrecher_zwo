#include "ts2bin.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "log.h"
#include "replace.h"

namespace host {
namespace manifest_ns {
#include "ts2_manifest.inc"
}
using namespace manifest_ns::manifest;

namespace {

constexpr uint32_t kMagic = 0x5432425A;  // "ZB2T"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kPage = 0x1000;
enum Kind : uint32_t { kCode = 1, kRead = 2, kReadWrite = 3 };

struct Section {
    uint32_t va, vsize, raw, rawsize, chars;
};

template <class T>
void put(std::vector<uint8_t>& v, T x) {
    const auto* p = reinterpret_cast<const uint8_t*>(&x);
    v.insert(v.end(), p, p + sizeof x);
}
template <class T>
bool get(const std::vector<uint8_t>& v, size_t& o, T* x) {
    if (o + sizeof(T) > v.size()) return false;
    std::memcpy(x, v.data() + o, sizeof(T));
    o += sizeof(T);
    return true;
}

std::vector<uint8_t> read_all(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

}  // namespace

bool write_ts2bin(const std::filesystem::path& exe, const std::filesystem::path& out, std::string* err) {
    auto file = read_all(exe);
    if (file.size() < 0x1000) {
        *err = "EXE nicht lesbar";
        return false;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kTimeDateStamp || nt->OptionalHeader.SizeOfImage != kSizeOfImage) {
        *err = "unbekannter Build der EXE (erwartet TimeDateStamp 58E252DC)";
        return false;
    }
    std::vector<Section> secs;
    auto* sh = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        secs.push_back({sh[i].VirtualAddress, sh[i].Misc.VirtualSize, sh[i].PointerToRawData, sh[i].SizeOfRawData,
                        sh[i].Characteristics});
    auto file_ptr = [&](uint32_t rva, uint32_t n) -> const uint8_t* {
        for (auto& s : secs)
            if (rva >= s.va && rva + n <= s.va + std::min(s.rawsize, s.vsize)) return file.data() + s.raw + (rva - s.va);
        return nullptr;
    };
    // Rohbytes eines Bereichs; was hinter dem Rohdatenende des Abschnitts
    // liegt (BSS), ist 0.
    auto copy_range = [&](uint32_t rva, uint32_t n, std::vector<uint8_t>& dst) -> bool {
        if (rva + n <= nt->OptionalHeader.SizeOfHeaders || (rva == 0 && n <= 0x1000)) {  // PE-Kopf
            dst.insert(dst.end(), file.data() + rva, file.data() + rva + n);
            return true;
        }
        for (auto& s : secs) {
            if (rva < s.va || rva >= s.va + s.vsize) continue;
            const uint32_t raw_end = s.va + std::min(s.rawsize, s.vsize);
            const uint32_t avail = rva < raw_end ? std::min(n, raw_end - rva) : 0;
            const uint8_t* src = file.data() + s.raw + (rva - s.va);
            dst.insert(dst.end(), src, src + avail);
            dst.insert(dst.end(), n - avail, uint8_t(0));
            return true;
        }
        return false;
    };

    std::vector<uint8_t> o;
    put(o, kMagic);
    put(o, kVersion);
    put(o, nt->FileHeader.TimeDateStamp);
    put(o, nt->OptionalHeader.SizeOfImage);
    put(o, uint64_t(nt->OptionalHeader.ImageBase));
    struct R {
        uint32_t rva, size, kind;
    };
    std::vector<R> rs;
    for (auto& t : kText) rs.push_back({t.rva, t.size, kCode});
    for (auto& p : kPages) {
        bool w = false;
        for (auto& s : secs)
            if (p.rva >= s.va && p.rva < s.va + s.vsize) w = s.chars & IMAGE_SCN_MEM_WRITE;
        rs.push_back({p.rva, p.size, w ? kReadWrite : kRead});
    }
    put(o, uint32_t(rs.size()));
    for (auto& r : rs) {
        put(o, r.rva);
        put(o, r.size);
        put(o, r.kind);
        if (!copy_range(r.rva, r.size, o)) {
            *err = "Bereich außerhalb der Abschnitte: " + std::to_string(r.rva);
            return false;
        }
    }
    // Relocations, die in die übernommenen Bereiche fallen
    const auto& rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const uint8_t* p = file_ptr(rd.VirtualAddress, rd.Size);
    std::vector<uint32_t> rel;
    for (const uint8_t* end = p + rd.Size; p && p < end;) {
        auto* blk = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(p);
        if (!blk->SizeOfBlock) break;
        auto* e = reinterpret_cast<const uint16_t*>(blk + 1);
        for (size_t i = 0; i < (blk->SizeOfBlock - sizeof(*blk)) / 2; ++i) {
            if ((e[i] >> 12) != IMAGE_REL_BASED_DIR64) continue;
            uint32_t a = blk->VirtualAddress + (e[i] & 0xFFF);
            for (auto& r : rs)
                if (a >= r.rva && a + 8 <= r.rva + r.size) {
                    rel.push_back(a);
                    break;
                }
        }
        p += blk->SizeOfBlock;
    }
    put(o, uint32_t(rel.size()));
    for (uint32_t a : rel) put(o, a);

    std::filesystem::create_directories(out.parent_path());
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()), std::streamsize(o.size()));
    if (!f) {
        *err = "ts2.bin nicht schreibbar";
        return false;
    }
    log("ts2.bin geschrieben: %zu Bereiche, %zu Relocations, %.1f MB\n", rs.size(), rel.size(), o.size() / 1048576.0);
    return true;
}

bool load_ts2bin(const std::filesystem::path& bin, Ts2Image* img, std::string* err) {
    auto v = read_all(bin);
    size_t o = 0;
    uint32_t magic, ver, ts, size;
    uint64_t pref;
    if (!get(v, o, &magic) || magic != kMagic || !get(v, o, &ver) || ver != kVersion || !get(v, o, &ts) ||
        !get(v, o, &size) || !get(v, o, &pref) || ts != kTimeDateStamp) {
        *err = "ts2.bin fehlt oder passt nicht zu dieser ts2.exe (neu einrichten)";
        return false;
    }
    img->preferred_base = pref;
    img->size = size;
    // Den ganzen Adressraum des Originals reservieren, aber nur benutzte Seiten
    // belegen: Ein Sprung in nicht übernommenen Engine-Code endet sofort in
    // einer Zugriffsverletzung mit klarer Adresse.
    img->base = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(pref), size, MEM_RESERVE, PAGE_NOACCESS));
    if (!img->base) img->base = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
    if (!img->base) {
        *err = "Adressraum nicht reservierbar";
        return false;
    }
    auto commit = [&](uint32_t rva, uint32_t n) {
        uint32_t a = rva & ~(kPage - 1), e = (rva + n + kPage - 1) & ~(kPage - 1);
        VirtualAlloc(img->base + a, e - a, MEM_COMMIT, PAGE_READWRITE);
    };
    struct R {
        uint32_t rva, size, kind;
    };
    std::vector<R> rs;
    uint32_t n;
    if (!get(v, o, &n)) return *err = "ts2.bin beschädigt", false;
    for (uint32_t i = 0; i < n; ++i) {
        R r;
        if (!get(v, o, &r.rva) || !get(v, o, &r.size) || !get(v, o, &r.kind) || o + r.size > v.size())
            return *err = "ts2.bin beschädigt", false;
        commit(r.rva, r.size);
        std::memcpy(img->base + r.rva, v.data() + o, r.size);
        o += r.size;
        rs.push_back(r);
    }
    commit(kBss.rva, kBss.size);  // genullt
    const int64_t delta = int64_t(uint64_t(img->base) - pref);
    if (!get(v, o, &n)) return *err = "ts2.bin beschädigt", false;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t a;
        if (!get(v, o, &a)) return *err = "ts2.bin beschädigt", false;
        if (delta) *reinterpret_cast<int64_t*>(img->base + a) += delta;
    }
    log("[ts2.bin] %zu Bereiche an %p geladen%s\n", rs.size(), img->base, delta ? " (verschoben)" : "");

    // Ersatz für die fremden Funktionen: an jede Adresse ein `jmp [rip+x]`
    // (6 Byte, wie die Original-Thunks; manche liegen nur 6 Byte auseinander)
    // auf einen Zeiger in einer eigenen Seite hinter dem Image-Inhalt.
    const uint32_t pool_rva = (size - 0x10000) & ~(kPage - 1);
    commit(pool_rva, kPage);
    auto** pool = img->rva<void*>(pool_rva);
    size_t used = 0;
    for (auto& e : kExternal) {
        const void* fn = replacement_for(e.rva);
        if (!fn) {
            log("[ts2.bin] kein Ersatz für fremde Funktion RVA %08X\n", e.rva);
            continue;
        }
        commit(e.rva, 6);
        pool[used] = const_cast<void*>(fn);
        uint8_t* p = img->base + e.rva;
        // Seite kann neu belegt sein: sonst mit int3 füllen, nur beim ersten Mal
        int32_t disp = int32_t(int64_t(&pool[used]) - int64_t(p + 6));
        p[0] = 0xFF;
        p[1] = 0x25;
        std::memcpy(p + 2, &disp, 4);
        ++used;
    }
    // Importe
    for (auto& im : kImports) {
        const void* fn = import_for(im.dll, im.name);
        if (!fn) log("[ts2.bin] Import %s!%s nicht gebunden\n", im.dll, im.name);
        commit(im.slot, 8);  // die Importtabelle selbst wird nicht übernommen
        *img->rva<const void*>(im.slot) = fn;
    }
    // Schutz: Code ausführbar, .rdata nur lesbar, Rest schreibbar
    DWORD old;
    for (auto& r : rs) {
        DWORD prot = r.kind == kCode ? PAGE_EXECUTE_READ : r.kind == kRead ? PAGE_READONLY : PAGE_READWRITE;
        uint32_t a = r.rva & ~(kPage - 1), e = (r.rva + r.size + kPage - 1) & ~(kPage - 1);
        // Seiten mit Code und Ersatz-Sprüngen gemeinsam (z. B. __chkstk) brauchen Ausführung
        VirtualProtect(img->base + a, e - a, prot, &old);
    }
    for (auto& e : kExternal) VirtualProtect(img->base + (e.rva & ~(kPage - 1)), kPage, PAGE_EXECUTE_READ, &old);
    VirtualProtect(img->base + pool_rva, kPage, PAGE_READONLY, &old);
    FlushInstructionCache(GetCurrentProcess(), img->base, size);
    return true;
}

}  // namespace host
