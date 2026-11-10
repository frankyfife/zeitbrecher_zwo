#include "audio.h"

#include <windows.h>
#include <xapo.h>
#include <xaudio2.h>

#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

#include "hf2.h"
#include "log.h"
#include "mixer.h"
#include "reverb.h"

namespace host {

AudioConfig load_audio_config(const std::filesystem::path& ini) {
    const std::wstring p = ini.wstring();
    auto getf = [&](const wchar_t* key, float def) {
        wchar_t buf[32], d[32];
        swprintf_s(d, L"%g", def);
        GetPrivateProfileStringW(L"ton", key, d, buf, 32, p.c_str());
        return float(_wtof(buf));
    };
    AudioConfig c;
    c.enabled = GetPrivateProfileIntW(L"ton", L"an", 1, p.c_str()) != 0;
    c.master = getf(L"gesamt", c.master);
    c.music = getf(L"musik", c.music);
    c.effects = getf(L"effekte", c.effects);
    c.surround = GetPrivateProfileIntW(L"ton", L"surround", 1, p.c_str()) != 0;
    c.hall = GetPrivateProfileIntW(L"ton", L"hall", 1, p.c_str()) != 0;
    return c;
}

namespace {

using MixFn = void (*)(float* out, uint32_t frames, int rate, int channels);

AudioConfig g_cfg;
MixFn g_ts2_mixer = nullptr;
bool g_compare = false;  // Umgebungsvariable TS2_TONPRUEFUNG=1

// Das Strom-Objekt, das TS2 von gEnv+0xD0 ->+0x100 bekommt. Reihenfolge der
// virtuellen Methoden = vtable-Offsets 0x00, 0x08, 0x10, 0x18.
struct Stream {
    virtual void slot0() { log("[ton] Strom +0x00 gerufen\n"); }
    virtual void release();
    virtual void set_rate(float hz);  // TS2 meldet 48000
    virtual void set_vec3(const float* v) {
        static bool once = false;
        if (!once) log("[ton] Strom +0x18 (%.3f %.3f %.3f)\n", v[0], v[1], v[2]), once = true;
    }
};

// TS2_TONAUFNAHME=<datei.wav>: greift die Endmischung (nach dem Hall) am
// Master ab und schreibt sie beim Beenden als 32-Bit-Float-WAV (Prüfung ohne
// Lautsprecher). Ein XAPO an Ort und Stelle, das nur mitschreibt.
class Tap : public IXAPO {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** p) override {
        if (riid == __uuidof(IXAPO) || riid == __uuidof(IUnknown)) {
            *p = static_cast<IXAPO*>(this);
            AddRef();
            return S_OK;
        }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return ULONG(r);
    }
    STDMETHODIMP GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES** pp) override {
        auto* r = static_cast<XAPO_REGISTRATION_PROPERTIES*>(XAPOAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
        if (!r) return E_OUTOFMEMORY;
        *r = {};
        wcscpy_s(r->FriendlyName, L"TS2 Tonaufnahme");
        r->MajorVersion = 1;
        r->Flags = XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH |
                   XAPO_FLAG_BUFFERCOUNT_MUST_MATCH | XAPO_FLAG_INPLACE_SUPPORTED | XAPO_FLAG_INPLACE_REQUIRED;
        r->MinInputBufferCount = r->MaxInputBufferCount = r->MinOutputBufferCount = r->MaxOutputBufferCount = 1;
        *pp = r;
        return S_OK;
    }
    STDMETHODIMP IsInputFormatSupported(const WAVEFORMATEX*, const WAVEFORMATEX*, WAVEFORMATEX** sup) override {
        if (sup) *sup = nullptr;
        return S_OK;
    }
    STDMETHODIMP IsOutputFormatSupported(const WAVEFORMATEX*, const WAVEFORMATEX*, WAVEFORMATEX** sup) override {
        if (sup) *sup = nullptr;
        return S_OK;
    }
    STDMETHODIMP Initialize(const void*, UINT32) override { return S_OK; }
    STDMETHODIMP_(void) Reset() override {}
    STDMETHODIMP LockForProcess(UINT32 n, const XAPO_LOCKFORPROCESS_PARAMETERS* in, UINT32,
                                const XAPO_LOCKFORPROCESS_PARAMETERS*) override {
        if (n && in && in[0].pFormat) {
            channels_ = in[0].pFormat->nChannels;
            rate_ = in[0].pFormat->nSamplesPerSec;
        }
        return S_OK;
    }
    STDMETHODIMP_(void) UnlockForProcess() override {}
    STDMETHODIMP_(void) Process(UINT32 n, const XAPO_PROCESS_BUFFER_PARAMETERS* in, UINT32 m,
                                XAPO_PROCESS_BUFFER_PARAMETERS* out, BOOL) override {
        if (!n || !in) return;
        const size_t count = size_t(in[0].ValidFrameCount) * channels_;
        if (data_.size() + count <= kMaxSamples) {
            if (in[0].BufferFlags == XAPO_BUFFER_VALID) {
                const float* f = static_cast<const float*>(in[0].pBuffer);
                data_.insert(data_.end(), f, f + count);
            } else {
                data_.resize(data_.size() + count, 0.0f);
            }
        }
        if (m && out) {
            out[0].BufferFlags = in[0].BufferFlags;
            out[0].ValidFrameCount = in[0].ValidFrameCount;
        }
    }
    STDMETHODIMP_(UINT32) CalcInputFrames(UINT32 f) override { return f; }
    STDMETHODIMP_(UINT32) CalcOutputFrames(UINT32 f) override { return f; }

    // nach dem Abbau der Stimmen (dann läuft Process nicht mehr)
    void write(const wchar_t* path) const {
        FILE* f = _wfopen(path, L"wb");
        if (!f) return;
        const uint32_t bytes = uint32_t(data_.size() * sizeof(float));
        const uint16_t fmt = 3, ch = uint16_t(channels_), bits = 32;
        const uint32_t rate = rate_, avg = rate_ * channels_ * 4, size = 36 + bytes, fmt_size = 16;
        const uint16_t align = uint16_t(channels_ * 4);
        std::fwrite("RIFF", 1, 4, f);
        std::fwrite(&size, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f);
        std::fwrite(&fmt_size, 4, 1, f);
        std::fwrite(&fmt, 2, 1, f);
        std::fwrite(&ch, 2, 1, f);
        std::fwrite(&rate, 4, 1, f);
        std::fwrite(&avg, 4, 1, f);
        std::fwrite(&align, 2, 1, f);
        std::fwrite(&bits, 2, 1, f);
        std::fwrite("data", 1, 4, f);
        std::fwrite(&bytes, 4, 1, f);
        std::fwrite(data_.data(), sizeof(float), data_.size(), f);
        std::fclose(f);
        log("[ton] Aufnahme: %zu Frames, %u Kanäle, %u Hz -> %ls\n", data_.size() / (channels_ ? channels_ : 1),
            channels_, rate_, path);
    }

private:
    static constexpr size_t kMaxSamples = size_t(48000) * 8 * 600;  // 10 min bei 8 Kanälen
    LONG ref_ = 1;
    UINT32 channels_ = 2, rate_ = 48000;
    std::vector<float> data_;
};

wchar_t g_record_path[MAX_PATH] = {};  // Umgebungsvariable TS2_TONAUFNAHME

class Output : public IXAudio2VoiceCallback {
public:
    bool start(int rate) {
        if (running_) return true;
        rate_ = rate;
        if (FAILED(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return fail("XAudio2Create");
        if (FAILED(xa_->CreateMasteringVoice(&master_, XAUDIO2_DEFAULT_CHANNELS, rate_)))
            return fail("CreateMasteringVoice");
        XAUDIO2_VOICE_DETAILS md{};
        master_->GetVoiceDetails(&md);
        DWORD mask = 0;
        master_->GetChannelMask(&mask);
        out_channels_ = md.InputChannels;
        // 5.1 mit dem eigenen Mixer, wenn das Gerät mindestens sechs Kanäle
        // hat (FL FR FC LFE + zwei hintere/seitliche); sonst Stereo mit TS2s
        // eigenem Mixer, genau wie im Original.
        channels_ = (g_cfg.surround && out_channels_ >= 6) ? 6 : 2;

        if (g_record_path[0]) {
            tap_ = new Tap();
            XAUDIO2_EFFECT_DESCRIPTOR ed{static_cast<IXAPO*>(tap_), TRUE, out_channels_};
            XAUDIO2_EFFECT_CHAIN chain{1, &ed};
            if (FAILED(master_->SetEffectChain(&chain))) {
                log("[ton] Aufnahme: SetEffectChain fehlgeschlagen\n");
                tap_->Release();
                tap_ = nullptr;
            }
        }

        // Raum-Hall: ein Submix mit dem XAudio2-Hall; die Quelle bekommt zwei
        // Kanäle mehr (Hall-Anteil L R), die nur in den Submix gehen. Der
        // eigene Mixer rechnet dann auch in Stereo (gleich wie TS2s, siehe
        // TS2_TONPRUEFUNG).
        hall_on_ = g_cfg.hall && hall_.create(xa_, master_, out_channels_, rate_, channels_);
        src_channels_ = channels_ + (hall_on_ ? 2 : 0);

        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = WORD(src_channels_);
        wf.nSamplesPerSec = DWORD(rate_);
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = WORD(wf.nChannels * 4);
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
        XAUDIO2_SEND_DESCRIPTOR sd[2] = {{0, master_}, {0, hall_on_ ? hall_.voice() : nullptr}};
        XAUDIO2_VOICE_SENDS sends{hall_on_ ? 2u : 1u, sd};
        if (FAILED(xa_->CreateSourceVoice(&src_, &wf, 0, 2.0f, this, &sends))) return fail("CreateSourceVoice");

        // Quellkanal i -> Gerätekanal i (FL FR FC LFE, dann die beiden
        // Surround-Kanäle des Geräts, seitlich oder hinten).
        std::vector<float> m(size_t(src_channels_) * out_channels_, 0.0f);
        for (UINT32 i = 0; i < UINT32(channels_) && i < out_channels_; ++i) m[i * src_channels_ + i] = 1.0f;
        src_->SetOutputMatrix(master_, src_channels_, out_channels_, m.data());
        if (hall_on_) {
            // nur die beiden letzten Kanäle in den Hall
            std::vector<float> h(size_t(src_channels_) * 2, 0.0f);
            h[0 * src_channels_ + channels_] = 1.0f;
            h[1 * src_channels_ + channels_ + 1] = 1.0f;
            src_->SetOutputMatrix(hall_.voice(), src_channels_, 2, h.data());
            mix_.assign(size_t(kFrames) * channels_, 0.0f);
            send_.assign(size_t(kFrames) * 2, 0.0f);
        }

        for (auto& b : bufs_) b.assign(size_t(kFrames) * src_channels_, 0.0f);
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        running_ = true;
        thread_ = std::thread([this] { run(); });
        src_->Start();
        log("[ton] XAudio2: Gerät %u Kanäle (Maske 0x%X), Ausgabe %s, %d Hz, Puffer %d Frames, Hall %s\n", out_channels_,
            mask, channels_ == 6 ? "5.1 (eigener Mixer)" : (hall_on_ ? "Stereo (eigener Mixer)" : "Stereo (TS2-Mixer)"),
            rate_, kFrames, hall_on_ ? "an" : "aus");
        return true;
    }
    void stop() {
        if (!running_) return;
        running_ = false;
        SetEvent(event_);
        if (thread_.joinable()) thread_.join();
        if (src_) src_->DestroyVoice(), src_ = nullptr;
        hall_.destroy();
        if (master_) master_->DestroyVoice(), master_ = nullptr;
        if (xa_) xa_->Release(), xa_ = nullptr;
        if (tap_) {
            tap_->write(g_record_path);
            tap_->Release();
            tap_ = nullptr;
        }
    }

    // IXAudio2VoiceCallback
    void STDMETHODCALLTYPE OnBufferEnd(void*) override { SetEvent(event_); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT e) override { log("[ton] Stimmenfehler 0x%08X\n", e); }

private:
    static constexpr int kFrames = 512;  // ≈ 10,7 ms bei 48 kHz
    static constexpr int kQueue = 3;

    bool fail(const char* what) {
        log("[ton] %s fehlgeschlagen, kein Ton\n", what);
        return false;
    }
    void run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        int next = 0;
        uint64_t blocks = 0;
        double energy[8] = {};
        while (running_) {
            XAUDIO2_VOICE_STATE st{};
            src_->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            while (st.BuffersQueued < kQueue && running_) {
                auto& b = bufs_[next];
                if (g_compare && blocks % 47 == 0) {
                    const float d = mixer_compare(reinterpret_cast<void*>(g_ts2_mixer), kFrames, rate_);
                    log("[ton] Prüfung eigener Mixer gegen TS2: größte Abweichung %g (%d positioniert)\n", d, mixer_positional_count());
                }
                const float v = g_cfg.master;
                if (hall_on_) {
                    // trocken nach mix_, Hall-Anteil nach send_, dann verschränkt
                    const bool wet = hall_.update();
                    mixer_mix(mix_.data(), kFrames, rate_, channels_, wet ? send_.data() : nullptr);
                    if (!wet) std::fill(send_.begin(), send_.end(), 0.0f);
                    for (int f = 0; f < kFrames; ++f) {
                        float* o = b.data() + size_t(f) * src_channels_;
                        for (int c = 0; c < channels_; ++c) o[c] = mix_[size_t(f) * channels_ + c] * v;
                        o[channels_] = send_[size_t(f) * 2] * v;
                        o[channels_ + 1] = send_[size_t(f) * 2 + 1] * v;
                    }
                } else if (channels_ == 6) {
                    mixer_mix(b.data(), kFrames, rate_, 6);
                    for (float& x : b) x *= v;
                } else {
                    g_ts2_mixer(b.data(), kFrames, rate_, 2);
                    for (float& x : b) x *= v;
                }
                for (size_t i = 0; i < b.size(); ++i) energy[i % src_channels_] += double(b[i]) * b[i];
                XAUDIO2_BUFFER xb{};
                xb.AudioBytes = UINT32(b.size() * sizeof(float));
                xb.pAudioData = reinterpret_cast<const BYTE*>(b.data());
                src_->SubmitSourceBuffer(&xb);
                next = (next + 1) % kQueue;
                ++st.BuffersQueued;
                // etwa alle 5 s den Pegel protokollieren (Prüfung ohne Lautsprecher)
                if (++blocks % 470 == 0) {
                    char line[240];
                    int n = 0;
                    for (int c = 0; c < channels_; ++c) {
                        n += snprintf(line + n, sizeof line - n, " %.4f", std::sqrt(energy[c] / (470.0 * kFrames)));
                        energy[c] = 0.0;
                    }
                    for (int c = channels_; c < src_channels_; ++c) {
                        n += snprintf(line + n, sizeof line - n, "%s %.4f", c == channels_ ? " | Hall-Anteil" : "",
                                      std::sqrt(energy[c] / (470.0 * kFrames)));
                        energy[c] = 0.0;
                    }
                    log("[ton] Pegel (RMS je Kanal%s)%s\n", channels_ == 6 ? " FL FR FC LFE SL SR" : " L R", line);
                }
            }
            WaitForSingleObject(event_, 50);
        }
    }

    int rate_ = 48000;
    int channels_ = 2;      // trockene Ausgabe (2 oder 6)
    int src_channels_ = 2;  // Quelle: trocken + 2 Hall-Kanäle
    bool hall_on_ = false;
    Hall hall_;
    Tap* tap_ = nullptr;
    std::vector<float> mix_, send_;
    IXAudio2* xa_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice* src_ = nullptr;
    UINT32 out_channels_ = 2;
    std::vector<float> bufs_[kQueue];
    HANDLE event_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

Output g_out;
Stream* g_stream = nullptr;

void Stream::set_rate(float hz) {
    log("[ton] TS2 mischt mit %.0f Hz\n", hz);
    if (g_cfg.enabled && g_ts2_mixer) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        g_out.start(int(hz));
    }
}

void Stream::release() {
    log("[ton] Strom freigegeben\n");
    g_out.stop();
}

}  // namespace

void* audio_open_stream(void*, void* mixer) {
    log("[ton] TS2 meldet den Mixer %p an\n", mixer);
    g_ts2_mixer = reinterpret_cast<MixFn>(mixer);
    if (!g_stream) g_stream = new Stream();
    return g_stream;
}

void audio_install(const Ts2Image& img, const AudioConfig& c) {
    g_cfg = c;
    mixer_init(img);
    wchar_t env[8] = {};
    g_compare = GetEnvironmentVariableW(L"TS2_TONPRUEFUNG", env, 8) > 0 && env[0] == L'1';
    if (GetEnvironmentVariableW(L"TS2_TONAUFNAHME", g_record_path, MAX_PATH) >= MAX_PATH) g_record_path[0] = 0;
    if (g_record_path[0] && std::filesystem::path(g_record_path).is_relative()) {
        // relativ zur EXE (das Arbeitsverzeichnis ist schon der Spielordner)
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const auto p = std::filesystem::path(exe).parent_path() / g_record_path;
        wcsncpy_s(g_record_path, p.c_str(), _TRUNCATE);
    }
    if (c.hall) reverb_init(img);
    // Ohne Engine-CVars (s_MusicVolume, s_SFXVolume; 0x141226070) bleiben
    // TS2s Lautstärken auf ihren Vorgaben 1,0; hier aus ts2.ini.
    *img.va<float>(hf2::kMusicVolume) = c.music;
    *img.va<float>(hf2::kEffectsVolume) = c.effects;
    log("[ton] Lautstärken: gesamt %.2f, Musik %.2f, Effekte %.2f; Hall %s\n", c.master, c.music, c.effects,
        c.hall ? "an" : "aus");
}

void audio_shutdown() { g_out.stop(); }

}  // namespace host
