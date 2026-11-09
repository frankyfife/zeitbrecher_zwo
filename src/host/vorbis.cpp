// Ersatz für das statisch gelinkte libvorbisfile (0x141C32B60 …), mit
// stb_vorbis. TS2 (Musik/Sprache, 0x14117EF9C, 0x14117F350, 0x14117F45C)
// benutzt nur:
//   ov_open_callbacks(datasource, vf, NULL, 0, callbacks*)  -> 0 = gut
//   ov_info(vf, -1)        -> vorbis_info*, liest +4 Kanäle, +8 Rate
//   ov_pcm_total(vf, -1)   -> Frames
//   ov_read(vf, buf, len, 0, 2, 1, &bitstream) -> Bytes, 16 Bit verschränkt
//   ov_time_seek(vf, 0.0)  -> zurückspulen (Schleifen)
//   ov_clear(vf)
// `vf` ist TS2s OggVorbis_File-Speicher (≥ 0x300 Byte); hier steht in den
// ersten 8 Byte nur der Zeiger auf den eigenen Zustand.
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

#include <cstdint>
#include <cstring>
#include <vector>

#include "log.h"

namespace host {

struct OvCallbacks {
    size_t (*read)(void* ptr, size_t size, size_t nmemb, void* datasource);
    int (*seek)(void* datasource, int64_t offset, int whence);
    int (*close)(void* datasource);
    long (*tell)(void* datasource);
};

namespace {

struct VorbisInfo {  // Anfang von vorbis_info (long = 32 Bit unter Windows)
    int32_t version, channels, rate, bitrate_upper, bitrate_nominal, bitrate_lower, bitrate_window;
    void* codec_setup;
};

struct Ogg {
    std::vector<uint8_t> data;
    stb_vorbis* v = nullptr;
    VorbisInfo info{};
};

Ogg*& slot(void* vf) { return *reinterpret_cast<Ogg**>(vf); }

}  // namespace

int ov_open_callbacks_x(void* datasource, void* vf, const char*, long, const OvCallbacks* cb) {
    slot(vf) = nullptr;
    auto* o = new Ogg();
    uint8_t chunk[65536];
    for (;;) {
        const size_t n = cb->read(chunk, 1, sizeof chunk, datasource);
        if (n == 0) break;
        o->data.insert(o->data.end(), chunk, chunk + n);
    }
    int err = 0;
    o->v = stb_vorbis_open_memory(o->data.data(), int(o->data.size()), &err, nullptr);
    if (!o->v) {
        log("[ogg] Öffnen fehlgeschlagen (%zu Byte, stb-Fehler %d)\n", o->data.size(), err);
        delete o;
        return -132;  // OV_ENOTVORBIS
    }
    const stb_vorbis_info i = stb_vorbis_get_info(o->v);
    o->info.version = 0;
    o->info.channels = i.channels;
    o->info.rate = int32_t(i.sample_rate);
    slot(vf) = o;
    return 0;
}

int ov_clear_x(void* vf) {
    if (Ogg* o = slot(vf)) {
        stb_vorbis_close(o->v);
        delete o;
        slot(vf) = nullptr;
    }
    return 0;
}

void* ov_info_x(void* vf, int) {
    Ogg* o = slot(vf);
    return o ? &o->info : nullptr;
}

int64_t ov_pcm_total_x(void* vf, int) {
    Ogg* o = slot(vf);
    return o ? int64_t(stb_vorbis_stream_length_in_samples(o->v)) : -131;  // OV_EINVAL
}

int ov_time_seek_x(void* vf, double seconds) {
    Ogg* o = slot(vf);
    if (!o) return -131;
    const unsigned frame = unsigned(seconds * o->info.rate + 0.5);
    return (frame == 0 ? stb_vorbis_seek_start(o->v) : stb_vorbis_seek(o->v, frame)) ? 0 : -137;  // OV_ENOSEEK
}

long ov_read_x(void* vf, char* buf, int len, int bigendian, int word, int sgned, int* bitstream) {
    Ogg* o = slot(vf);
    if (!o) return -131;
    if (bigendian || word != 2 || !sgned) {
        log("[ogg] ov_read mit Format %d/%d/%d nicht nachgebaut\n", bigendian, word, sgned);
        return -130;  // OV_EIMPL
    }
    if (bitstream) *bitstream = 0;
    const int ch = o->info.channels;
    const int frames = stb_vorbis_get_samples_short_interleaved(o->v, ch, reinterpret_cast<short*>(buf), len / 2);
    return long(frames) * ch * 2;
}

}  // namespace host
