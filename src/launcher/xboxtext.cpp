#include "xboxtext.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>

namespace xboxtext {
namespace {

constexpr uint64_t kSector = 2048;
constexpr int kEntries = 2770;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

bool read_at(std::ifstream& f, uint64_t off, void* dst, size_t n) {
    f.clear();
    f.seekg(std::streamoff(off));
    f.read(static_cast<char*>(dst), std::streamsize(n));
    return size_t(f.gcount()) == n;
}

// Sucht im Verzeichnisbaum (binärer Baum aus Einträgen) nach name, rekursiv
// in Unterverzeichnisse.
bool find_file(std::ifstream& f, uint64_t base, uint32_t sector, uint32_t size, const std::string& name,
               uint32_t& out_sec, uint32_t& out_size, int depth = 0) {
    if (depth > 16 || size == 0 || size > (16u << 20)) return false;
    std::vector<uint8_t> d(size);
    if (!read_at(f, base + sector * kSector, d.data(), size)) return false;
    std::vector<uint32_t> todo{0};
    int guard = 0;
    while (!todo.empty() && ++guard < 100000) {
        const uint32_t off = todo.back() * 4;
        todo.pop_back();
        if (off + 14 > d.size()) continue;
        const uint8_t* e = d.data() + off;
        const uint16_t left = rd16(e), right = rd16(e + 2);
        if (left == 0xFFFF) continue;
        const uint32_t sec = rd32(e + 4), sz = rd32(e + 8);
        const uint8_t attr = e[12], nlen = e[13];
        if (off + 14 + nlen > d.size()) continue;
        std::string n(reinterpret_cast<const char*>(e + 14), nlen);
        std::transform(n.begin(), n.end(), n.begin(), [](char c) { return char(std::tolower((unsigned char)c)); });
        if (attr & 0x10) {
            if (find_file(f, base, sec, sz, name, out_sec, out_size, depth + 1)) return true;
        } else if (depth == 0 && n == name) {
            out_sec = sec;
            out_size = sz;
            return true;
        }
        if (left) todo.push_back(left);
        if (right) todo.push_back(right);
    }
    return false;
}

struct Section {
    uint32_t va, vsize, raw, rsize;
};

}  // namespace

Result extract(const std::filesystem::path& image, const std::filesystem::path& out_dir) {
    Result r;
    std::ifstream f(image, std::ios::binary);
    if (!f) {
        r.error = "Image nicht lesbar";
        return r;
    }
    // XISO-Kopf: Sektor 32 der Spielpartition (Redump-Abbilder ab 0x18300000)
    std::optional<uint64_t> base;
    for (uint64_t b : {0x0ull, 0x18300000ull, 0x0FD90000ull, 0x89D80000ull, 0x2080000ull}) {
        char m[20];
        if (read_at(f, b + 32 * kSector, m, 20) && std::memcmp(m, kMagic, 20) == 0) {
            base = b;
            break;
        }
    }
    if (!base) {
        r.error = "kein Xbox-Image (XISO-Kopf fehlt)";
        return r;
    }
    uint8_t root[8];
    read_at(f, *base + 32 * kSector + 20, root, 8);
    uint32_t xsec = 0, xsize = 0;
    if (!find_file(f, *base, rd32(root), rd32(root + 4), "default.xbe", xsec, xsize) || xsize > (64u << 20)) {
        r.error = "default.xbe nicht im Image";
        return r;
    }
    std::vector<uint8_t> xbe(xsize);
    if (!read_at(f, *base + xsec * kSector, xbe.data(), xsize) || xsize < 0x124) {
        r.error = "default.xbe nicht lesbar";
        return r;
    }

    const uint32_t xbase = rd32(&xbe[0x104]);
    const uint32_t nsec = rd32(&xbe[0x11C]);
    const uint32_t sh = rd32(&xbe[0x120]) - xbase;
    std::vector<Section> secs;
    for (uint32_t i = 0; i < nsec && sh + i * 56 + 20 <= xsize; ++i) {
        const uint8_t* s = &xbe[sh + i * 56];
        secs.push_back({rd32(s + 4), rd32(s + 8), rd32(s + 12), rd32(s + 16)});
    }
    auto va2off = [&](uint32_t va) -> std::optional<uint32_t> {
        for (const auto& s : secs)
            if (va >= s.va && va < s.va + s.rsize && s.raw + (va - s.va) < xsize) return s.raw + (va - s.va);
        return std::nullopt;
    };
    auto off2va = [&](uint32_t o) -> std::optional<uint32_t> {
        for (const auto& s : secs)
            if (o >= s.raw && o < s.raw + s.rsize) return s.va + (o - s.raw);
        return std::nullopt;
    };
    auto text = [&](uint32_t va) -> std::string {
        const auto o = va ? va2off(va) : std::nullopt;
        if (!o) return {};
        const auto* p = &xbe[*o];
        const auto* end = static_cast<const uint8_t*>(std::memchr(p, 0, xsize - *o));
        return end ? std::string(reinterpret_cast<const char*>(p), end - p) : std::string();
    };
    auto find_all = [&](const uint8_t* pat, size_t n) {
        std::vector<uint32_t> hits;
        for (auto it = xbe.begin();;) {
            it = std::search(it, xbe.end(), pat, pat + n);
            if (it == xbe.end()) break;
            hits.push_back(uint32_t(it - xbe.begin()));
            ++it;
        }
        return hits;
    };

    const std::map<std::string, std::string> quit = {
        {"Quit", "en"}, {"Quitter", "fr"}, {"Beenden", "de"}, {"Esci", "it"}, {"Salir", "es"}};
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const uint8_t arcade[] = {'A', 'r', 'c', 'a', 'd', 'e', 0};
    for (uint32_t ao : find_all(arcade, sizeof arcade)) {
        const auto arcade_va = off2va(ao);
        if (!arcade_va) continue;
        uint8_t ptr[4];
        std::memcpy(ptr, &*arcade_va, 4);  // Xbox und PC: little endian
        for (uint32_t po : find_all(ptr, 4)) {
            const auto loc = off2va(po);
            if (!loc || *loc < 422 * 4) continue;
            const uint32_t table = *loc - 422 * 4;
            if (!va2off(table) || !va2off(table + kEntries * 4 - 4)) continue;
            auto entry = [&](int i) { return rd32(&xbe[*va2off(table + i * 4)]); };
            const auto q = quit.find(text(entry(2142)));
            if (q == quit.end() || text(entry(425)).empty()) continue;
            if (std::find(r.languages.begin(), r.languages.end(), q->second) != r.languages.end()) continue;
            std::ofstream o(out_dir / (q->second + ".bin"), std::ios::binary);
            for (int i = 0; i < kEntries; ++i) {
                const std::string t = text(entry(i));
                o.write(t.c_str(), std::streamsize(t.size() + 1));
            }
            if (!o) {
                r.error = "Schreiben fehlgeschlagen";
                return r;
            }
            r.languages.push_back(q->second);
        }
    }
    if (r.languages.empty()) r.error = "keine Texttabellen gefunden (PAL-Fassung von TimeSplitters 2?)";
    std::sort(r.languages.begin(), r.languages.end());
    return r;
}

}  // namespace xboxtext
