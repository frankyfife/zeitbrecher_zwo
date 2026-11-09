#include "audio.h"

#include <windows.h>
#include <xaudio2.h>

#include <atomic>
#include <cmath>
#include <fstream>
#include <thread>
#include <vector>

#include "hf2.h"
#include "log.h"
#include "mixer.h"

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

        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = WORD(channels_);
        wf.nSamplesPerSec = DWORD(rate_);
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = WORD(wf.nChannels * 4);
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
        if (FAILED(xa_->CreateSourceVoice(&src_, &wf, 0, 2.0f, this))) return fail("CreateSourceVoice");

        // Quellkanal i -> Gerätekanal i (FL FR FC LFE, dann die beiden
        // Surround-Kanäle des Geräts, seitlich oder hinten).
        std::vector<float> m(size_t(channels_) * out_channels_, 0.0f);
        for (UINT32 i = 0; i < UINT32(channels_) && i < out_channels_; ++i) m[i * channels_ + i] = 1.0f;
        src_->SetOutputMatrix(master_, channels_, out_channels_, m.data());

        for (auto& b : bufs_) b.assign(size_t(kFrames) * channels_, 0.0f);
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        running_ = true;
        thread_ = std::thread([this] { run(); });
        src_->Start();
        log("[ton] XAudio2: Gerät %u Kanäle (Maske 0x%X), Ausgabe %s, %d Hz, Puffer %d Frames\n", out_channels_, mask,
            channels_ == 6 ? "5.1 (eigener Mixer)" : "Stereo (TS2-Mixer)", rate_, kFrames);
        return true;
    }
    void stop() {
        if (!running_) return;
        running_ = false;
        SetEvent(event_);
        if (thread_.joinable()) thread_.join();
        if (src_) src_->DestroyVoice(), src_ = nullptr;
        if (master_) master_->DestroyVoice(), master_ = nullptr;
        if (xa_) xa_->Release(), xa_ = nullptr;
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
        double energy[6] = {};
        while (running_) {
            XAUDIO2_VOICE_STATE st{};
            src_->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            while (st.BuffersQueued < kQueue && running_) {
                auto& b = bufs_[next];
                if (g_compare && blocks % 47 == 0) {
                    const float d = mixer_compare(reinterpret_cast<void*>(g_ts2_mixer), kFrames, rate_);
                    log("[ton] Prüfung eigener Mixer gegen TS2: größte Abweichung %g (%d positioniert)\n", d, mixer_positional_count());
                }
                if (channels_ == 6)
                    mixer_mix(b.data(), kFrames, rate_, 6);
                else
                    g_ts2_mixer(b.data(), kFrames, rate_, 2);
                const float v = g_cfg.master;
                for (size_t i = 0; i < b.size(); ++i) {
                    b[i] *= v;
                    energy[i % channels_] += double(b[i]) * b[i];
                }
                XAUDIO2_BUFFER xb{};
                xb.AudioBytes = UINT32(b.size() * sizeof(float));
                xb.pAudioData = reinterpret_cast<const BYTE*>(b.data());
                src_->SubmitSourceBuffer(&xb);
                next = (next + 1) % kQueue;
                ++st.BuffersQueued;
                // etwa alle 5 s den Pegel protokollieren (Prüfung ohne Lautsprecher)
                if (++blocks % 470 == 0) {
                    char line[160];
                    int n = 0;
                    for (int c = 0; c < channels_; ++c) {
                        n += snprintf(line + n, sizeof line - n, " %.4f", std::sqrt(energy[c] / (470.0 * kFrames)));
                        energy[c] = 0.0;
                    }
                    log("[ton] Pegel (RMS je Kanal%s)%s\n", channels_ == 6 ? " FL FR FC LFE SL SR" : " L R", line);
                }
            }
            WaitForSingleObject(event_, 50);
        }
    }

    int rate_ = 48000;
    int channels_ = 2;
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
    // Ohne Engine-CVars (s_MusicVolume, s_SFXVolume; 0x141226070) bleiben
    // TS2s Lautstärken auf ihren Vorgaben 1,0; hier aus ts2.ini.
    *img.va<float>(hf2::kMusicVolume) = c.music;
    *img.va<float>(hf2::kEffectsVolume) = c.effects;
    log("[ton] Lautstärken: gesamt %.2f, Musik %.2f, Effekte %.2f\n", c.master, c.music, c.effects);
}

void audio_shutdown() { g_out.stop(); }

}  // namespace host
