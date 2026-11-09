// hfpak - lists and extracts the encrypted CryPak archives of
// Homefront: The Revolution (where the PC port of TimeSplitters 2 keeps its data).
//
// Format (found in Homefront2_Release.exe, see docs/formate/crypak.md):
//  * An ordinary ZIP layout. The end of central directory record (PK 05 06)
//    is in the clear; its comment holds, in this order:
//      CryCustomExtendedHeader  8 bytes  {size=8, encryption=3, signing=1}
//      CrySignedCDRHeader       0x84     {size, RSA-1024 signature}
//      CryCustomEncryptionHeader 0x884   {size, IV[128], keys[16][128]}
//      a Homefront trailer      12 bytes {'QL' 05 06, offset delta, 0}
//  * IV and the 16 keys are RSA blocks: raised to the public exponent with
//    the RSA-1024 public key inside the executable, then OAEP (SHA-256)
//    decoded, giving 16 bytes each.
//  * The central directory is Twofish-CTR (little-endian counter) with key 0.
//  * A file's data is encrypted when bit 14 of its method field is set; the
//    key is keys[(~(crc >> 2)) & 15], the IV is built from crc and sizes.
//  * method & 0x1f: 0 stored, 8 raw deflate, 11 Brotli.
//
// The RSA key is read from the user's own executable (--exe), so this tool
// contains no data of the game.

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <brotli/decode.h>
#include <zlib.h>

#include "twofish.h"

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace {

struct Entry {
    std::string name;
    uint16_t flags = 0, method = 0, time = 0, date = 0;
    uint32_t crc = 0, csize = 0, usize = 0, local_offset = 0;
    uint16_t name_len = 0, extra_len = 0;
    std::vector<uint8_t> comment;
};

struct Keys {
    uint8_t iv[16];
    uint8_t key[16][16];
};

[[noreturn]] void die(const char* fmt, const std::string& a = {}) {
    std::fprintf(stderr, "hfpak: ");
    std::fprintf(stderr, fmt, a.c_str());
    std::fprintf(stderr, "\n");
    std::exit(1);
}

std::vector<uint8_t> read_file(const fs::path& p) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) die("cannot open %s", p.string());
    _fseeki64(f, 0, SEEK_END);
    int64_t n = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    std::vector<uint8_t> d(static_cast<size_t>(n));
    if (n && std::fread(d.data(), 1, d.size(), f) != d.size()) die("short read %s", p.string());
    std::fclose(f);
    return d;
}

// ---------------------------------------------------------------- RSA / OAEP

struct RsaPub {
    std::vector<uint8_t> n;  // big-endian modulus
    std::vector<uint8_t> e;  // big-endian exponent
};

// Finds the PKCS#1 RSAPublicKey (SEQUENCE { INTEGER n(1024 bit), INTEGER e })
// CryEngine embeds for pak signatures and keys.
RsaPub find_rsa_key(const std::vector<uint8_t>& exe) {
    static const uint8_t pat[] = {0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00};
    auto it = std::search(exe.begin(), exe.end(), std::begin(pat), std::end(pat));
    if (it == exe.end()) die("no RSA-1024 public key found in the executable");
    size_t o = size_t(it - exe.begin()) + sizeof(pat);
    RsaPub k;
    k.n.assign(exe.begin() + o, exe.begin() + o + 128);
    o += 128;
    if (exe[o] != 0x02) die("unexpected key layout");
    uint8_t elen = exe[o + 1];
    k.e.assign(exe.begin() + o + 2, exe.begin() + o + 2 + elen);
    return k;
}

void sha256(const uint8_t* d, size_t n, uint8_t out[32]) {
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, PUCHAR(d), ULONG(n), out, 32) != 0) die("BCryptHash failed");
}

std::vector<uint8_t> mgf1(const uint8_t* seed, size_t seed_len, size_t len) {
    std::vector<uint8_t> out, buf(seed, seed + seed_len);
    buf.resize(seed_len + 4);
    for (uint32_t c = 0; out.size() < len; ++c) {
        buf[seed_len] = uint8_t(c >> 24);
        buf[seed_len + 1] = uint8_t(c >> 16);
        buf[seed_len + 2] = uint8_t(c >> 8);
        buf[seed_len + 3] = uint8_t(c);
        uint8_t h[32];
        sha256(buf.data(), buf.size(), h);
        out.insert(out.end(), h, h + 32);
    }
    out.resize(len);
    return out;
}

class RsaPublicOp {
public:
    explicit RsaPublicOp(const RsaPub& k) {
        BCRYPT_ALG_HANDLE alg = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0) die("no RSA provider");
        std::vector<uint8_t> blob(sizeof(BCRYPT_RSAKEY_BLOB) + k.e.size() + k.n.size());
        auto* h = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
        h->Magic = BCRYPT_RSAPUBLIC_MAGIC;
        h->BitLength = ULONG(k.n.size() * 8);
        h->cbPublicExp = ULONG(k.e.size());
        h->cbModulus = ULONG(k.n.size());
        std::memcpy(blob.data() + sizeof(*h), k.e.data(), k.e.size());
        std::memcpy(blob.data() + sizeof(*h) + k.e.size(), k.n.data(), k.n.size());
        if (BCryptImportKeyPair(alg, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key_, blob.data(), ULONG(blob.size()), 0) != 0)
            die("RSA key import failed");
        alg_ = alg;
    }
    ~RsaPublicOp() {
        BCryptDestroyKey(key_);
        BCryptCloseAlgorithmProvider(alg_, 0);
    }
    // c^e mod n, then OAEP(SHA-256, empty label) decoding.
    std::vector<uint8_t> decrypt_oaep(const uint8_t c[128]) const {
        uint8_t em[128];
        ULONG got = 0;
        if (BCryptEncrypt(key_, PUCHAR(c), 128, nullptr, nullptr, 0, em, 128, &got, BCRYPT_PAD_NONE) != 0 || got != 128)
            die("RSA operation failed");
        const size_t hl = 32;
        if (em[0] != 0) die("bad OAEP block");
        const uint8_t* mseed = em + 1;
        const uint8_t* mdb = em + 1 + hl;
        const size_t dbl = 128 - 1 - hl;
        auto sm = mgf1(mdb, dbl, hl);
        uint8_t seed[32];
        for (size_t i = 0; i < hl; ++i) seed[i] = mseed[i] ^ sm[i];
        auto dm = mgf1(seed, hl, dbl);
        std::vector<uint8_t> db(dbl);
        for (size_t i = 0; i < dbl; ++i) db[i] = mdb[i] ^ dm[i];
        uint8_t lhash[32];
        sha256(nullptr, 0, lhash);
        if (std::memcmp(db.data(), lhash, hl) != 0) die("bad OAEP label hash");
        size_t i = hl;
        while (i < dbl && db[i] == 0) ++i;
        if (i == dbl || db[i] != 1) die("bad OAEP padding");
        return {db.begin() + i + 1, db.end()};
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
};

// ---------------------------------------------------------------- the pak

template <class T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

class Pak {
public:
    Pak(const fs::path& path, const RsaPublicOp& rsa) : path_(path) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) die("cannot open %s", path.string());
        _fseeki64(f, 0, SEEK_END);
        size_ = _ftelli64(f);
        const int64_t tail_len = std::min<int64_t>(size_, 0x10000);
        std::vector<uint8_t> tail(static_cast<size_t>(tail_len));
        _fseeki64(f, size_ - tail_len, SEEK_SET);
        std::fread(tail.data(), 1, tail.size(), f);

        int64_t eocd = -1;
        for (int64_t i = tail_len - 22; i >= 0; --i)
            if (rd<uint32_t>(&tail[size_t(i)]) == 0x06054b50) {
                eocd = i;
                break;
            }
        if (eocd < 0) die("no end of central directory in %s", path.string());
        const uint8_t* e = &tail[size_t(eocd)];
        const uint16_t entries = rd<uint16_t>(e + 10);
        const uint32_t cdr_size = rd<uint32_t>(e + 12);
        const uint32_t cdr_off = rd<uint32_t>(e + 16);
        const uint16_t comment_len = rd<uint16_t>(e + 20);
        const uint8_t* c = e + 22;
        if (comment_len < 8 + 0x84 + 0x884 + 12) die("unexpected comment (not a Homefront pak?)");
        encryption_ = rd<uint16_t>(c + 4);
        signing_ = rd<uint16_t>(c + 6);
        if (encryption_ != 3) die("encryption technique is not 3 (stream cipher with key table)");
        const uint8_t* enc = c + 8 + 0x84;
        if (rd<uint32_t>(enc) != 0x884) die("bad encryption header");
        auto iv = rsa.decrypt_oaep(enc + 4);
        if (iv.size() != 16) die("unexpected IV length");
        std::memcpy(keys_.iv, iv.data(), 16);
        for (int k = 0; k < 16; ++k) {
            auto kk = rsa.decrypt_oaep(enc + 4 + 128 + 128 * k);
            if (kk.size() != 16) die("unexpected key length");
            std::memcpy(keys_.key[k], kk.data(), 16);
        }
        const uint8_t* trailer = c + comment_len - 12;
        if (rd<uint32_t>(trailer) != 0x06054c51) die("missing 'QL' trailer");
        delta_ = rd<uint32_t>(trailer + 4);

        std::vector<uint8_t> cdr(cdr_size);
        _fseeki64(f, cdr_off, SEEK_SET);
        if (std::fread(cdr.data(), 1, cdr.size(), f) != cdr.size()) die("short CDR read");
        std::fclose(f);
        twofish_ctr_le(keys_.key[0], keys_.iv, cdr.data(), cdr.size());

        size_t o = 0;
        while (o + 46 <= cdr.size()) {
            const uint8_t* h = &cdr[o];
            if (rd<uint32_t>(h) != 0x02014b50) die("bad central directory entry");
            Entry en;
            en.flags = rd<uint16_t>(h + 8);
            en.method = rd<uint16_t>(h + 10);
            en.time = rd<uint16_t>(h + 12);
            en.date = rd<uint16_t>(h + 14);
            en.crc = rd<uint32_t>(h + 16);
            en.csize = rd<uint32_t>(h + 20);
            en.usize = rd<uint32_t>(h + 24);
            en.name_len = rd<uint16_t>(h + 28);
            en.extra_len = rd<uint16_t>(h + 30);
            uint16_t cl = rd<uint16_t>(h + 32);
            en.local_offset = rd<uint32_t>(h + 42);
            en.name.assign(reinterpret_cast<const char*>(h + 46), en.name_len);
            en.comment.assign(h + 46 + en.name_len + en.extra_len, h + 46 + en.name_len + en.extra_len + cl);
            o += 46 + size_t(en.name_len) + en.extra_len + cl;
            entries_.push_back(std::move(en));
        }
        if ((entries_.size() & 0xFFFF) != entries) std::fprintf(stderr, "hfpak: warning: %zu entries, EOCD says %u\n", entries_.size(), entries);
    }

    const std::vector<Entry>& entries() const { return entries_; }

    // Reads, decrypts and decompresses one entry. f is the caller's handle.
    std::optional<std::vector<uint8_t>> read(FILE* f, const Entry& en, std::string* err) const {
        // The local header is not used by the game for these paks; the data
        // follows a 30-byte local header and the name (and extra field).
        const int64_t data_off = int64_t(en.local_offset) + delta_ + 30 + en.name_len + en.extra_len;
        std::vector<uint8_t> buf(en.csize);
        _fseeki64(f, data_off, SEEK_SET);
        if (en.csize && std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
            *err = "short read";
            return std::nullopt;
        }
        if (en.method & 0x4000) {
            uint32_t iv[4];
            iv[0] = en.usize ^ (en.csize << 12);
            iv[1] = en.csize == 0;
            iv[2] = en.crc ^ (en.csize << 12);
            iv[3] = uint32_t(en.usize == 0) ^ en.csize;
            const int k = int((~(en.crc >> 2)) & 0xF);
            twofish_ctr_le(keys_.key[k], reinterpret_cast<const uint8_t*>(iv), buf.data(), buf.size());
        }
        std::vector<uint8_t> out;
        switch (en.method & 0x1f) {
            case 0:
                out = std::move(buf);
                break;
            case 8: {
                out.resize(en.usize);
                z_stream z{};
                if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
                    *err = "inflateInit2";
                    return std::nullopt;
                }
                z.next_in = buf.data();
                z.avail_in = uInt(buf.size());
                z.next_out = out.data();
                z.avail_out = uInt(out.size());
                int r = inflate(&z, Z_FINISH);
                inflateEnd(&z);
                if (r != Z_STREAM_END || z.total_out != en.usize) {
                    *err = "inflate failed";
                    return std::nullopt;
                }
                break;
            }
            case 11: {
                out.resize(en.usize);
                size_t outl = out.size();
                if (BrotliDecoderDecompress(buf.size(), buf.data(), &outl, out.data()) != BROTLI_DECODER_RESULT_SUCCESS ||
                    outl != en.usize) {
                    *err = "brotli failed";
                    return std::nullopt;
                }
                break;
            }
            default:
                *err = "unknown method " + std::to_string(en.method & 0x1f);
                return std::nullopt;
        }
        if (uint32_t(crc32(0, out.data(), uInt(out.size()))) != en.crc) {
            *err = "CRC mismatch";
            return std::nullopt;
        }
        return out;
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    int64_t size_ = 0;
    uint16_t encryption_ = 0, signing_ = 0;
    uint32_t delta_ = 0;
    Keys keys_{};
    std::vector<Entry> entries_;
};

bool safe_relative(const std::string& name, fs::path* out) {
    std::string n = name;
    std::replace(n.begin(), n.end(), '\\', '/');
    fs::path p = fs::u8path(n).lexically_normal();
    if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (auto& part : p)
        if (part == "..") return false;
    *out = p;
    return true;
}

bool matches(const std::string& name, const std::vector<std::string>& prefixes) {
    if (prefixes.empty()) return true;
    std::string n = name;
    std::replace(n.begin(), n.end(), '\\', '/');
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    for (auto& p : prefixes)
        if (n.rfind(p, 0) == 0) return true;
    return false;
}

void usage() {
    std::puts(
        "hfpak - Homefront: The Revolution CryPak reader\n"
        "\n"
        "  hfpak list    --exe <Homefront2_Release.exe> <pak> [prefix...]\n"
        "  hfpak extract --exe <Homefront2_Release.exe> <pak> <outdir> [prefix...]\n"
        "\n"
        "prefix: only entries whose path starts with it (case-insensitive), e.g. ts2/\n"
        "Every extracted file is checked against its CRC32.");
}

}  // namespace

int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(fs::path(wargv[i]).u8string());
    if (args.empty()) {
        usage();
        return 1;
    }
    std::string cmd = args[0];
    fs::path exe;
    std::vector<std::string> pos;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--exe" && i + 1 < args.size()) exe = fs::u8path(args[++i]);
        else if (args[i] == "--threads" && i + 1 < args.size()) threads = unsigned(std::stoul(args[++i]));
        else pos.push_back(args[i]);
    }
    if (exe.empty() || pos.empty() || (cmd != "list" && cmd != "extract")) {
        usage();
        return 1;
    }
    RsaPublicOp rsa(find_rsa_key(read_file(exe)));
    Pak pak(fs::u8path(pos[0]), rsa);

    if (cmd == "list") {
        std::vector<std::string> pre;
        for (size_t i = 1; i < pos.size(); ++i) pre.push_back(pos[i]);
        for (auto& p : pre) std::transform(p.begin(), p.end(), p.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        for (auto& e : pak.entries())
            if (matches(e.name, pre))
                std::printf("%04x %10u %10u %08x %s\n", e.method, e.csize, e.usize, e.crc, e.name.c_str());
        return 0;
    }

    if (pos.size() < 2) {
        usage();
        return 1;
    }
    fs::path outdir = fs::u8path(pos[1]);
    std::vector<std::string> pre;
    for (size_t i = 2; i < pos.size(); ++i) pre.push_back(pos[i]);
    for (auto& p : pre) std::transform(p.begin(), p.end(), p.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });

    std::vector<const Entry*> todo;
    for (auto& e : pak.entries())
        if (matches(e.name, pre) && !e.name.empty() && e.name.back() != '/') todo.push_back(&e);

    std::atomic<size_t> next{0}, ok{0}, bad{0};
    std::atomic<uint64_t> bytes{0};
    std::mutex log;
    auto worker = [&] {
        FILE* f = _wfopen(pak.path().c_str(), L"rb");
        if (!f) return;
        for (size_t i; (i = next++) < todo.size();) {
            const Entry& e = *todo[i];
            fs::path rel;
            if (!safe_relative(e.name, &rel)) {
                std::lock_guard<std::mutex> g(log);
                std::fprintf(stderr, "skip unsafe path: %s\n", e.name.c_str());
                ++bad;
                continue;
            }
            std::string err;
            auto data = pak.read(f, e, &err);
            if (!data) {
                std::lock_guard<std::mutex> g(log);
                std::fprintf(stderr, "FAIL %s: %s\n", e.name.c_str(), err.c_str());
                ++bad;
                continue;
            }
            fs::path dst = outdir / rel;
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            FILE* o = _wfopen(dst.c_str(), L"wb");
            if (!o || (data->size() && std::fwrite(data->data(), 1, data->size(), o) != data->size())) {
                std::lock_guard<std::mutex> g(log);
                std::fprintf(stderr, "FAIL write %s\n", dst.string().c_str());
                ++bad;
                if (o) std::fclose(o);
                continue;
            }
            std::fclose(o);
            bytes += data->size();
            size_t n = ++ok;
            if (n % 500 == 0) {
                std::lock_guard<std::mutex> g(log);
                std::fprintf(stderr, "  %zu/%zu\n", n, todo.size());
            }
        }
        std::fclose(f);
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    std::printf("%zu files extracted (%.1f MB), %zu failed\n", ok.load(), bytes.load() / 1048576.0, bad.load());
    return bad ? 2 : 0;
}
