// Raum-Hall für TS2s Stimmen aus der Spielwelt ([ton] hall).
//
// Die Xbox stellt bei jedem Raumwechsel der Kamera den I3DL2-Hall von
// DirectSound ein (IDirectSound_SetI3DL2Listener, Effektnummer je Raum aus
// sound/reverb/levelNN_reverb.s). Die PC-Fassung hat das nicht; sie bringt
// aber Dambusters Tabellen ts2/sound/reverb/overrides/levelNN.reverb mit
// ("Raumnummer : k_rvb_REVERB_PRESET_<Name>", EAX-Voreinstellungen). Diese
// Datei hier verfolgt den Raum der Kamera, wählt die Voreinstellung des
// Raums und betreibt den XAudio2-Hall (I3DL2-Parameter, umgerechnet mit
// ReverbConvertI3DL2ToNative) als Submix, in den der eigene Mixer
// (mixer.h) einen Stereo-Anteil der Stimmen aus der Spielwelt schickt.
#pragma once
#include <windows.h>
#include <xaudio2.h>

#include "ts2bin.h"

namespace host {

// Vor dem Start: Spielordner ist das Arbeitsverzeichnis (main.cpp).
void reverb_init(const Ts2Image& img);

class Hall {
public:
    // Submix mit Hall (Eingang Stereo, Ausgang out_channels = 2 oder 6),
    // gibt kanalgleich an dest (dest_channels Kanäle) weiter.
    bool create(IXAudio2* xa, IXAudio2Voice* dest, UINT32 dest_channels, int rate, int out_channels);
    void destroy();
    IXAudio2SubmixVoice* voice() const { return voice_; }
    // Je Block aus dem Ton-Thread: Level und Raum verfolgen, bei Wechsel
    // die Voreinstellung setzen. true = der Raum hat Hall (dann die
    // Stimmen aus der Spielwelt hineinschicken).
    bool update();

private:
    void apply(int preset);
    IXAudio2SubmixVoice* voice_ = nullptr;
    int current_ = -2;  // Index der Voreinstellung, -1 = kein Hall
    int applied_ = -1;  // zuletzt im Effekt gesetzt
    int last_room_ = -1;
};

}  // namespace host
