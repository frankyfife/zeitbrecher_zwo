// Ton für TS2 – an Stelle von Homefronts Soundsystem (gEnv+0xD0).
//
// TS2 mischt selbst: ce_sound meldet beim Start seinen Mixer (0x141227CB4,
// mix(float* out, frames, rate, channels), 80 Stimmen) über
// gEnv+0xD0 ->+0x100(mixer) an und bekommt ein Strom-Objekt zurück
// (+0x08 freigeben, +0x10 Abtastrate, +0x18 drei Floats). Musik und Sprache
// laufen über dieselben Stimmen; die Ogg-Dateien dekodiert vorbisfile
// (ersetzt durch stb_vorbis, vorbis.cpp). ts2.exe ruft den Mixer aus einem
// eigenen Thread (bei 5.1 den eigenen, mixer.h) und gibt über XAudio2 aus.
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

struct AudioConfig {
    bool enabled = true;
    float master = 1.0f, music = 1.0f, effects = 1.0f;
    bool surround = true;  // 5.1-Ausgabe, wenn das Gerät sie hat
};

AudioConfig load_audio_config(const std::filesystem::path& ini);
// Methode gEnv+0xD0 ->+0x100 (this, mixer) -> Strom-Objekt
void* audio_open_stream(void* self, void* mixer);
// vor Start: Mixer vorbereiten, Lautstärken in TS2 setzen
void audio_install(const Ts2Image& img, const AudioConfig& c);
void audio_shutdown();

}  // namespace host
