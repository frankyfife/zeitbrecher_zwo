// Eigener Mixer für TS2s Stimmen, mit 5.1-Ausgabe.
//
// TS2s Mixer (0x141227CB4) gibt nur Stereo aus: positionierte Stimmen werden
// nach der Querkomponente ihrer Richtung verteilt (links = 0,75 − nx,
// rechts = 0,75 + nx, je auf 0…1 begrenzt), dazu Abstandsdämpfung. Dieser
// Mixer liest dieselben Stimmen (Aufbau in docs/host.md, "Ton") mit
// derselben Zustandsfortschreibung, verteilt positionierte Stimmen aber über
// den Azimut auf FL, FC, FR, SL, SR. Die Lautstärken folgen der Xbox
// (mixer.cpp); ohne diese Anpassung und mit `channels == 2` rechnet er wie
// das Original (Prüfung gegen TS2s Mixer).
#pragma once
#include <cstdint>

#include "ts2bin.h"

namespace host {

void mixer_init(const Ts2Image& img);
// out: frames × channels Floats, verschränkt; channels 2 (L R) oder
// 6 (FL FR FC LFE SL SR). send: nullptr oder frames × 2 Floats (L R), der
// Anteil der Stimmen aus der Spielwelt für den Raum-Hall (reverb.h).
// xbox: Lautstärken wie auf der Xbox (2D-Headroom −6 dB, Abfallkurve der
// 3D-Puffer, mixer.cpp); false rechnet genau wie TS2s Mixer.
void mixer_mix(float* out, uint32_t frames, int rate, int channels, float* send = nullptr, bool xbox = true);
// Vergleicht einen Block dieses Mixers (Stereo) mit TS2s Mixer auf
// demselben Stimmenzustand; liefert die größte Abweichung.
float mixer_compare(void* ts2_mixer, uint32_t frames, int rate);
// spielende positionierte Stimmen (Protokoll)
int mixer_positional_count();

}  // namespace host
