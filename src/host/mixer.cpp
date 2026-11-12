#include "mixer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "hf2.h"
#include "log.h"

namespace host {

namespace {

// Stimme (0xD8 Byte, 80 Stück ab hf2::kSoundVoices). Feldnamen nach ihrer
// Verwendung im Original-Mixer und in ce_sound.
struct Voice {
    int32_t active;          // +0x00
    int32_t queue[32];       // +0x04  Puffer-Nummern (1-basiert), Ringpuffer
    int32_t loop_to;         // +0x84  Rücksprung beim Schleifen
    int32_t current;         // +0x88  spielender Eintrag
    int32_t end;             // +0x8C  hinter dem letzten Eintrag
    uint32_t flags;          // +0x90  0x1000 = Musik
    int32_t loop;            // +0x94
    int32_t positional;      // +0x98
    int32_t state;           // +0x9C  2 = spielt
    float time;              // +0xA0  gespielte Sekunden
    float pos;               // +0xA4  Position im Puffer, Sekunden
    float pitch;             // +0xA8
    float max_dist;          // +0xAC
    float min_dist;          // +0xB0
    float rolloff;           // +0xB4
    float vol_left;          // +0xB8
    float vol_right;         // +0xBC
    float x, y, z;           // +0xC0  relativ zur Kamera: x rechts, y oben, z vorn
    uint8_t pad[4];          // +0xCC
    CRITICAL_SECTION* lock;  // +0xD0
};
static_assert(sizeof(Voice) == 0xD8, "Stimme");
static_assert(offsetof(Voice, lock) == 0xD0, "Stimme");

struct Buffer {  // 0x28 Byte ab hf2::kSoundBuffers
    uint8_t pad0[8];
    const int16_t* data;  // +0x08
    uint8_t pad1[8];
    uint32_t frames;      // +0x18
    uint32_t rate;        // +0x1C
    uint32_t channels;    // +0x20
    uint8_t pad2[4];
};
static_assert(sizeof(Buffer) == 0x28, "Puffer");

constexpr int kVoices = 80;
constexpr double kSampleScale = 1.0 / 32767.0;

Voice* g_voices = nullptr;
const Buffer* g_buffers = nullptr;
const float* g_master = nullptr;
const float* g_music = nullptr;
const float* g_effects = nullptr;
const float* g_listener = nullptr;  // (0, 1): Bezugsrichtung des Original-Pannings

// Lautsprecher im Kreis (Grad, 0 = vorn, positiv = rechts) und ihr Kanal
// in FL FR FC LFE SL SR.
struct Speaker {
    float deg;
    int ch;
};
constexpr Speaker kRing[] = {{0, 2}, {30, 1}, {110, 5}, {250, 4}, {330, 0}};

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Abstandsdämpfung wie im Original: min / (min + rolloff · (d − min)),
// d auf [min, max] begrenzt.
float distance_gain(const Voice& v) {
    const float d = std::sqrt(v.y * v.y + v.x * v.x + v.z * v.z);
    const float lim = d < v.max_dist ? d : v.max_dist;
    float dc;
    if (v.min_dist > lim)
        dc = v.min_dist;
    else if (d >= v.max_dist)
        dc = v.max_dist;
    else
        dc = d;
    return v.min_dist / ((dc - v.min_dist) * v.rolloff + v.min_dist);
}

// --- Lautstärken wie auf der Xbox (PAL-XBE) ---
// Die Xbox spielt dieselben Stimmen über DirectSound: Lautstärke
// 2000 · log10(Wert · Gesamt/16) hundertstel dB (0x1C6600, linear also gleich).
// Sie unterscheidet zwei Arten von Puffern (0x1C6640, Pools 0x46A748/0x451EA0):
// - Ohne Position (Musik, Sprache, Menü, HUD; auf dem PC unpositioniert oder
//   "positioniert" am Hörer mit min = max = 1): 2D-Puffer, je Seite die volle
//   Lautstärke, aber mit der Vorgabe-Headroom von DirectSound für 2D-Stimmen,
//   600 hundertstel dB (DSOUND 0x2000FF; SetVolume zieht sie ab, 0x1FEE42).
// - Mit Position: 3D-Puffer (Headroom 0), Mindestabstand 1, Höchstabstand wie
//   auf dem PC, Abfallkurve {0,5; 0,25; 0,125; 0} (0x34A2B4, SetRolloffCurve):
//   zwischen Mindest- und Höchstabstand stückweise linear in vier gleichen
//   Abschnitten (DSOUND 0x201A92). Der PC-Port nähert das mit min = max/4 und
//   Abfall 4 an, ist damit aber in der Nähe (unter ~17 m) bis 5,5 dB lauter.
constexpr float kXbox2dGain = 0.50118723f;  // −6 dB
bool xbox_2d(const Voice& v) {
    return !v.positional || (v.max_dist == 1.0f && v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
}
float xbox_distance_gain(const Voice& v) {
    static const float kCurve[5] = {1.0f, 0.5f, 0.25f, 0.125f, 0.0f};
    constexpr float kMin = 1.0f;
    const float d = std::sqrt(v.y * v.y + v.x * v.x + v.z * v.z);
    if (d <= kMin) return 1.0f;
    const float max = v.max_dist > kMin ? v.max_dist : kMin + 1.0f;
    const float t = (std::min(d, max) - kMin) * 4.0f / (max - kMin);
    int i = int(t);
    if (i > 3) i = 3;
    return kCurve[i] + (kCurve[i + 1] - kCurve[i]) * (t - float(i));
}
bool g_xbox = true;  // false: genau wie TS2s Mixer (TS2_TONPRUEFUNG)
float dist_gain(const Voice& v) { return g_xbox ? xbox_distance_gain(v) : distance_gain(v); }

// waagerechte Richtung (nx rechts, nz vorn), wie das Original
void direction(const Voice& v, float* nx, float* nz) {
    const float h2 = v.z * v.z + v.x * v.x;
    if (h2 > 0.0f) {
        const float inv = 1.0f / std::sqrt(h2);
        *nx = inv * v.x;
        *nz = inv * v.z;
    } else {
        *nx = g_listener[0];
        *nz = g_listener[1];
    }
}

// Verstärkung je Ausgabekanal (bis 6), für Mono- und Stereoquellen, dazu
// der Anteil für den Hall (Stereo, reverb.h).
struct Gains {
    float mono[6];
    float st[2][6];  // [Quellkanal][Ausgabekanal]
    bool hall;
    float send_mono[2];
    float send_st[2];  // Stereoquelle: L -> L, R -> R
};

// Stimmen aus der Spielwelt: positionierte Stimmen ohne Flag 0x1000 (Musik)
// und ohne 0x4 (Menügeräusche: am Hörer, min = max = 1).
// Unpositionierte Stimmen (Zwischensequenz-Ton in Blöcken zu vier Puffern,
// Menü) bleiben trocken. Gemessen mit TS2_HALLPROBE in Menü und Siberia.
bool in_world(const Voice& v) { return v.positional && (v.flags & 0x1004) == 0 && !(g_xbox && xbox_2d(v)); }

// Original-Stereopanning (0x141227D99…0x141227F17): Faktor links/rechts
void stereo_pan(const Voice& v, float* pl, float* pr) {
    float nx, nz;
    direction(v, &nx, &nz);
    const float a = nz * g_listener[1] + nx * g_listener[0];
    const float b = nx * g_listener[1] + (-nz) * g_listener[0];
    const float ang = std::atan2(a, b);
    const float s = std::sin(float(double(ang) - 1.5707963267948966));
    *pl = clamp01(0.75f + s);
    *pr = clamp01(0.75f - s);
}

Gains voice_gains(const Voice& v, int channels, bool hall) {
    Gains out{};
    const float cat = (v.flags & 0x1000) ? *g_music : *g_effects;
    float gl = *g_master * v.vol_left;
    float gr = *g_master * v.vol_right;
    // Xbox: Stimmen ohne Position als 2D-Puffer (volle Lautstärke je Seite,
    // −6 dB Headroom), die übrigen als 3D-Puffer mit der Xbox-Abfallkurve
    const bool placed = v.positional && !(g_xbox && xbox_2d(v));
    if (g_xbox && !placed) gl *= kXbox2dGain, gr *= kXbox2dGain;
    if (hall && in_world(v)) {
        // Hall-Anteil: wie Stereo, aber nur mit der Wurzel der
        // Abstandsdämpfung (ferne Quellen klingen halliger, verstummen aber
        // trotzdem; I3DL2 dämpft den Hall mit RoomRolloffFactor 0 gar nicht).
        out.hall = true;
        float pl, pr;
        stereo_pan(v, &pl, &pr);
        const float dg = std::sqrt(dist_gain(v));
        out.send_mono[0] = out.send_st[0] = gl * pl * dg * cat;
        out.send_mono[1] = out.send_st[1] = gr * pr * dg * cat;
    }
    if (placed && channels == 6) {
        float nx, nz;
        direction(v, &nx, &nz);
        // gleiche Lautheit wie Stereo von vorn: 2 · 0,75² = 1,125
        const float g = 0.5f * (gl + gr) * dist_gain(v) * cat * 1.0606602f;
        float deg = std::atan2(nx, nz) * 57.29578f;
        if (deg < 0) deg += 360.0f;
        for (int i = 0; i < 5; ++i) {
            const Speaker a = kRing[i], b = kRing[(i + 1) % 5];
            const float span = (i == 4 ? 360.0f : b.deg) - a.deg;
            const float rel = deg - a.deg;
            if (rel < 0 || rel > span) continue;
            const float t = rel / span * 1.5707964f;
            out.mono[a.ch] = g * std::cos(t);
            out.mono[b.ch] = g * std::sin(t);
            break;
        }
        // Stereoquelle an einer Position: beide Kanäle zur Mitte gemischt
        for (int c = 0; c < 6; ++c) out.st[0][c] = out.st[1][c] = 0.5f * out.mono[c];
        return out;
    }
    if (placed) {
        // Original (0x141227D99…0x141227F17)
        float pl, pr;
        stereo_pan(v, &pl, &pr);
        const float dg = dist_gain(v);
        gl *= pl * dg;
        gr *= pr * dg;
    }
    gl *= cat;
    gr *= cat;
    // FL = Kanal 0, FR = Kanal 1 in beiden Ausgabeformaten
    out.mono[0] = gl;
    out.mono[1] = gr;
    out.st[0][0] = gl;
    out.st[1][1] = gr;
    return out;
}

// Eine Stimme in `out` mischen; Zustandsfortschreibung wie das Original.
void mix_voice(Voice& v, float* out, float* send, uint32_t frames, int channels, float dt, const Gains& G) {
    if (v.loop_to == v.end) return;
    int32_t cur = v.current;
    uint32_t i = 0;
    int guard = 0;
    while (i < frames) {
        const int32_t id = v.queue[cur];
        const Buffer& b = g_buffers[id - 1];
        if (!b.data) {
            v.pos += dt;
            ++i;
            continue;
        }
        const float fpos = float(b.rate) * v.pos * v.pitch;
        const uint32_t last = b.frames - 1;
        uint32_t i0 = uint32_t(std::floor(fpos));
        uint32_t i1 = uint32_t(std::ceil(fpos));
        if (i0 > last) i0 = last;
        if (i1 > last) i1 = last;
        if (i0 == last) {  // Puffer zu Ende: nächster Eintrag, dieselbe Ausgabe-Frame neu
            v.pos = 0.0f;
            cur = (cur + 1) & 31;
            if (cur == v.end) {
                if (!v.loop) {
                    v.state = 0;
                    v.current = cur;
                    break;
                }
                cur = v.loop_to;
            }
            v.current = cur;
            if (++guard > 64) break;  // nur leere Puffer in der Schleife
            continue;
        }
        guard = 0;
        float* o = out + size_t(i) * channels;
        if (b.channels == 2) {
            const float l = float(double(b.data[i0 * 2]) * kSampleScale);
            const float r = float(double(b.data[i0 * 2 + 1]) * kSampleScale);
            for (int c = 0; c < channels; ++c) o[c] += l * G.st[0][c] + r * G.st[1][c];
            if (G.hall) {
                send[size_t(i) * 2] += l * G.send_st[0];
                send[size_t(i) * 2 + 1] += r * G.send_st[1];
            }
        } else {
            const float frac = fpos - float(i0);
            const float s0 = float(double(b.data[i0 * b.channels]) * kSampleScale);
            const float s1 = float(double(b.data[i1 * b.channels]) * kSampleScale);
            const float s = s0 * (1.0f - frac) + s1 * frac;
            for (int c = 0; c < channels; ++c) o[c] += s * G.mono[c];
            if (G.hall) {
                send[size_t(i) * 2] += s * G.send_mono[0];
                send[size_t(i) * 2 + 1] += s * G.send_mono[1];
            }
        }
        v.pos += dt;
        ++i;
    }
    v.time += float(frames) * dt;
}

// TS2_HALLPROBE=1: jede neu gestartete Stimme einmal protokollieren (Flags,
// positioniert, Abstand), um Spielwelt und Menü zu unterscheiden.
bool g_probe = false;
int32_t g_probe_seen[kVoices];
void probe_voice(int k, const Voice& v) {
    const int32_t key = v.queue[v.loop_to & 31];
    if (g_probe_seen[k] == key) return;
    g_probe_seen[k] = key;
    log("[ton] Stimme %d: Puffer %d, Flags 0x%X, Schleife %d, positioniert %d, Abstand %.1f (min %.1f max %.1f), "
        "Lautstärke %.2f/%.2f\n",
        k, key, v.flags, v.loop, v.positional, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), v.min_dist, v.max_dist,
        v.vol_left, v.vol_right);
}

}  // namespace

void mixer_init(const Ts2Image& img) {
    g_probe = GetEnvironmentVariableW(L"TS2_HALLPROBE", nullptr, 0) > 0;
    g_voices = img.va<Voice>(hf2::kSoundVoices);
    g_buffers = img.va<Buffer>(hf2::kSoundBuffers);
    g_master = img.va<float>(hf2::kMasterVolume);
    g_music = img.va<float>(hf2::kMusicVolume);
    g_effects = img.va<float>(hf2::kEffectsVolume);
    g_listener = img.va<float>(hf2::kSoundListenerDir);
}

void mixer_mix(float* out, uint32_t frames, int rate, int channels, float* send, bool xbox) {
    g_xbox = xbox;
    std::memset(out, 0, size_t(frames) * channels * sizeof(float));
    if (send) std::memset(send, 0, size_t(frames) * 2 * sizeof(float));
    const float dt = 1.0f / float(rate);
    int playing = 0, same_max = 0, same_id = 0;
    int ids[kVoices];
    for (int k = 0; k < kVoices; ++k) {
        Voice& v = g_voices[k];
        if (!v.lock) continue;
        EnterCriticalSection(v.lock);
        if (v.active && v.state == 2) {
            if (g_probe) {
                probe_voice(k, v);
                ids[playing] = v.queue[v.current & 31];
                int same = 1;
                for (int j = 0; j < playing; ++j) same += ids[j] == ids[playing];
                if (same > same_max) same_max = same, same_id = ids[playing];
                ++playing;
            }
            mix_voice(v, out, send, frames, channels, dt, voice_gains(v, channels, send != nullptr));
        }
        LeaveCriticalSection(v.lock);
    }
    if (g_probe) {
        static int blocks = 0, pmax = 0, smax = 0, sid = 0;
        if (playing > pmax) pmax = playing;
        if (same_max > smax) smax = same_max, sid = same_id;
        if (++blocks % 470 == 0) {
            log("[ton] Stimmen: höchstens %d gleichzeitig, derselbe Puffer höchstens %d× (Puffer %d)\n", pmax, smax, sid);
            pmax = smax = sid = 0;
        }
    }
}

int mixer_positional_count() {
    int n = 0;
    for (int k = 0; k < kVoices; ++k) n += g_voices[k].active && g_voices[k].state == 2 && g_voices[k].positional;
    return n;
}

float mixer_compare(void* ts2_mixer, uint32_t frames, int rate) {
    using MixFn = void (*)(float*, uint32_t, int, int);
    for (int k = 0; k < kVoices; ++k)
        if (g_voices[k].lock) EnterCriticalSection(g_voices[k].lock);
    std::vector<uint8_t> snap(sizeof(Voice) * kVoices);
    std::memcpy(snap.data(), g_voices, snap.size());
    std::vector<float> a(size_t(frames) * 2), b(size_t(frames) * 2);
    reinterpret_cast<MixFn>(ts2_mixer)(a.data(), frames, rate, 2);
    std::vector<uint8_t> after_ts2(snap.size());
    std::memcpy(after_ts2.data(), g_voices, snap.size());
    std::memcpy(g_voices, snap.data(), snap.size());
    mixer_mix(b.data(), frames, rate, 2, nullptr, false);  // genau wie TS2s Mixer
    float diff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
    if (std::memcmp(after_ts2.data(), g_voices, snap.size()) != 0) diff = std::max(diff, 1000.0f);  // Zustand weicht ab
    for (int k = kVoices - 1; k >= 0; --k)
        if (g_voices[k].lock) LeaveCriticalSection(g_voices[k].lock);
    return diff;
}

}  // namespace host
