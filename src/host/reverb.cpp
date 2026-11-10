#include "reverb.h"

#include <xaudio2fx.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "hf2.h"
#include "log.h"

namespace host {

namespace {

// I3DL2-Parameter (Felder wie XAUDIO2FX_REVERB_I3DL2_PARAMETERS ohne
// WetDryMix und RoomRolloffFactor). Namen und Reihenfolge wie Dambusters
// Tabelle k_rvb_REVERB_PRESET_* (Namen 0x1421E96B8…0x1421EA7A8, Zeiger
// 0x14259CFD0, ohne Verweise im Code): die 113 EAX/EFX-Voreinstellungen.
// Werte aus den EFX-Voreinstellungen (Creative/OpenAL efx-presets.h)
// umgerechnet: mB = 2000·log10(Gain), Diffusion/Dichte × 100. Die
// klassischen (GENERIC…UNDERWATER) entsprechen weitgehend
// XAUDIO2FX_I3DL2_PRESET_* (GENERIC, STONEROOM, PARKINGLOT genau).
struct Preset {
    const char* name;
    int32_t room, room_hf;
    float decay, decay_hf_ratio;
    int32_t reflections;
    float reflections_delay;
    int32_t reverb;
    float reverb_delay, diffusion, density, hf_reference;
};

constexpr Preset kPresets[] = {
    {"GENERIC", -1000, -100, 1.49f, 0.83f, -2602, 0.0070f, 200, 0.0110f, 100.0f, 100.0f, 5000.0f},
    {"PADDEDCELL", -1000, -6000, 0.17f, 0.10f, -1204, 0.0010f, 207, 0.0020f, 100.0f, 17.2f, 5000.0f},
    {"ROOM", -1000, -454, 0.40f, 0.83f, -1646, 0.0020f, 53, 0.0030f, 100.0f, 42.9f, 5000.0f},
    {"BATHROOM", -1000, -1200, 1.49f, 0.54f, -370, 0.0070f, 1030, 0.0110f, 100.0f, 17.2f, 5000.0f},
    {"LIVINGROOM", -1000, -6000, 0.50f, 0.10f, -1376, 0.0030f, -1104, 0.0040f, 100.0f, 97.7f, 5000.0f},
    {"STONEROOM", -1000, -300, 2.31f, 0.64f, -711, 0.0120f, 83, 0.0170f, 100.0f, 100.0f, 5000.0f},
    {"AUDITORIUM", -1000, -476, 4.32f, 0.59f, -789, 0.0200f, -289, 0.0300f, 100.0f, 100.0f, 5000.0f},
    {"CONCERTHALL", -1000, -500, 3.92f, 0.70f, -1230, 0.0200f, -2, 0.0290f, 100.0f, 100.0f, 5000.0f},
    {"CAVE", -1000, 0, 2.91f, 1.30f, -602, 0.0150f, -302, 0.0220f, 100.0f, 100.0f, 5000.0f},
    {"ARENA", -1000, -698, 7.24f, 0.33f, -1166, 0.0200f, 16, 0.0300f, 100.0f, 100.0f, 5000.0f},
    {"HANGAR", -1000, -1000, 10.05f, 0.23f, -602, 0.0200f, 198, 0.0300f, 100.0f, 100.0f, 5000.0f},
    {"CARPETTEDHALLWAY", -1000, -4000, 0.30f, 0.10f, -1831, 0.0020f, -1630, 0.0300f, 100.0f, 42.9f, 5000.0f},
    {"HALLWAY", -1000, -300, 1.49f, 0.59f, -1219, 0.0070f, 441, 0.0110f, 100.0f, 36.4f, 5000.0f},
    {"STONECORRIDOR", -1000, -237, 2.70f, 0.79f, -1214, 0.0130f, 395, 0.0200f, 100.0f, 100.0f, 5000.0f},
    {"ALLEY", -1000, -270, 1.49f, 0.86f, -1204, 0.0070f, -4, 0.0110f, 30.0f, 100.0f, 5000.0f},
    {"FOREST", -1000, -3300, 1.49f, 0.54f, -2560, 0.1620f, -229, 0.0880f, 30.0f, 100.0f, 5000.0f},
    {"CITY", -1000, -800, 1.49f, 0.67f, -2273, 0.0070f, -1691, 0.0110f, 50.0f, 100.0f, 5000.0f},
    {"MOUNTAINS", -1000, -2501, 1.49f, 0.21f, -2781, 0.3000f, -1434, 0.1000f, 27.0f, 100.0f, 5000.0f},
    {"QUARRY", -1000, -1000, 1.49f, 0.83f, -10000, 0.0610f, 500, 0.0250f, 100.0f, 100.0f, 5000.0f},
    {"PLAIN", -1000, -2000, 1.49f, 0.50f, -2466, 0.1790f, -1926, 0.1000f, 21.0f, 100.0f, 5000.0f},
    {"PARKINGLOT", -1000, 0, 1.65f, 1.50f, -1363, 0.0080f, -1153, 0.0120f, 100.0f, 100.0f, 5000.0f},
    {"SEWERPIPE", -1000, -1000, 2.81f, 0.14f, 429, 0.0140f, 1023, 0.0210f, 80.0f, 30.7f, 5000.0f},
    {"UNDERWATER", -1000, -4000, 1.49f, 0.10f, -449, 0.0070f, 1700, 0.0110f, 100.0f, 36.4f, 5000.0f},
    {"DRUGGED", -1000, 0, 8.39f, 1.39f, -115, 0.0020f, 985, 0.0300f, 50.0f, 42.9f, 5000.0f},
    {"DIZZY", -1000, -400, 17.23f, 0.56f, -1713, 0.0200f, -613, 0.0300f, 60.0f, 36.4f, 5000.0f},
    {"PSYCHOTIC", -1000, -151, 7.56f, 0.91f, -626, 0.0200f, 774, 0.0300f, 50.0f, 6.2f, 5000.0f},
    {"CASTLE_SMALLROOM", -1000, -800, 1.22f, 0.83f, -100, 0.0220f, 600, 0.0110f, 89.0f, 100.0f, 5168.6f},
    {"CASTLE_SHORTPASSAGE", -1000, -1000, 2.32f, 0.83f, -100, 0.0070f, 200, 0.0230f, 89.0f, 100.0f, 5168.6f},
    {"CASTLE_MEDIUMROOM", -1000, -1100, 2.04f, 0.83f, -400, 0.0220f, 400, 0.0110f, 93.0f, 100.0f, 5168.6f},
    {"CASTLE_LONGPASSAGE", -1000, -800, 3.42f, 0.83f, -100, 0.0070f, 300, 0.0230f, 89.0f, 100.0f, 5168.6f},
    {"CASTLE_LARGEROOM", -1000, -1100, 2.53f, 0.83f, -700, 0.0340f, 200, 0.0160f, 82.0f, 100.0f, 5168.6f},
    {"CASTLE_HALL", -1000, -1100, 3.14f, 0.79f, -1500, 0.0560f, 100, 0.0240f, 81.0f, 100.0f, 5168.6f},
    {"CASTLE_CUPBOARD", -1000, -1100, 0.67f, 0.87f, 300, 0.0100f, 1100, 0.0070f, 89.0f, 100.0f, 5168.6f},
    {"CASTLE_COURTYARD", -1000, -700, 2.13f, 0.61f, -1300, 0.1600f, -300, 0.0360f, 42.0f, 100.0f, 5000.0f},
    {"CASTLE_ALCOVE", -1000, -600, 1.64f, 0.87f, 0, 0.0070f, 300, 0.0340f, 89.0f, 100.0f, 5168.6f},
    {"FACTORY_ALCOVE", -1200, -200, 3.14f, 0.65f, 300, 0.0100f, 0, 0.0380f, 59.0f, 36.4f, 3762.6f},
    {"FACTORY_SHORTPASSAGE", -1200, -200, 2.53f, 0.65f, 0, 0.0100f, 200, 0.0380f, 64.0f, 36.4f, 3762.6f},
    {"FACTORY_MEDIUMROOM", -1200, -200, 2.76f, 0.65f, -1100, 0.0220f, 300, 0.0230f, 82.0f, 42.9f, 3762.6f},
    {"FACTORY_LONGPASSAGE", -1200, -200, 4.06f, 0.65f, 0, 0.0200f, 200, 0.0370f, 64.0f, 36.4f, 3762.6f},
    {"FACTORY_LARGEROOM", -1200, -300, 4.24f, 0.51f, -1500, 0.0390f, 100, 0.0230f, 75.0f, 42.9f, 3762.6f},
    {"FACTORY_HALL", -1000, -300, 7.43f, 0.51f, -2400, 0.0730f, -100, 0.0270f, 75.0f, 42.9f, 3762.6f},
    {"FACTORY_CUPBOARD", -1200, -200, 0.49f, 0.65f, 200, 0.0100f, 600, 0.0320f, 63.0f, 30.7f, 3762.6f},
    {"FACTORY_COURTYARD", -1000, -1000, 2.32f, 0.29f, -1300, 0.1400f, -800, 0.0390f, 57.0f, 30.7f, 3762.6f},
    {"FACTORY_SMALLROOM", -1000, -200, 1.72f, 0.65f, -300, 0.0100f, 500, 0.0240f, 82.0f, 36.4f, 3762.6f},
    {"ICEPALACE_ALCOVE", -1000, -500, 2.76f, 1.46f, 100, 0.0100f, -100, 0.0300f, 84.0f, 100.0f, 12428.5f},
    {"ICEPALACE_SHORTPASSAGE", -1000, -500, 1.79f, 1.46f, -600, 0.0100f, 100, 0.0190f, 75.0f, 100.0f, 12428.5f},
    {"ICEPALACE_MEDIUMROOM", -1000, -500, 2.22f, 1.53f, -800, 0.0390f, 100, 0.0270f, 87.0f, 100.0f, 12428.5f},
    {"ICEPALACE_LONGPASSAGE", -1000, -500, 3.01f, 1.46f, -200, 0.0120f, 200, 0.0250f, 77.0f, 100.0f, 12428.5f},
    {"ICEPALACE_LARGEROOM", -1000, -500, 3.14f, 1.53f, -1200, 0.0390f, 0, 0.0270f, 81.0f, 100.0f, 12428.5f},
    {"ICEPALACE_HALL", -1000, -700, 5.49f, 1.53f, -1900, 0.0540f, -400, 0.0520f, 76.0f, 100.0f, 12428.5f},
    {"ICEPALACE_CUPBOARD", -1000, -600, 0.76f, 1.53f, 100, 0.0120f, 600, 0.0160f, 83.0f, 100.0f, 12428.5f},
    {"ICEPALACE_COURTYARD", -1000, -1100, 2.04f, 1.20f, -1000, 0.1730f, -1000, 0.0430f, 59.0f, 100.0f, 12428.5f},
    {"ICEPALACE_SMALLROOM", -1000, -500, 1.51f, 1.53f, -100, 0.0100f, 300, 0.0110f, 84.0f, 100.0f, 12428.5f},
    {"SPACESTATION_ALCOVE", -1000, -300, 1.16f, 0.81f, 300, 0.0070f, 0, 0.0180f, 78.0f, 21.1f, 3316.1f},
    {"SPACESTATION_MEDIUMROOM", -1000, -400, 3.01f, 0.50f, -800, 0.0340f, 100, 0.0350f, 75.0f, 21.1f, 3316.1f},
    {"SPACESTATION_SHORTPASSAGE", -1000, -400, 3.57f, 0.50f, 0, 0.0120f, 100, 0.0160f, 87.0f, 21.1f, 3316.1f},
    {"SPACESTATION_LONGPASSAGE", -1000, -400, 4.62f, 0.62f, 0, 0.0120f, 200, 0.0310f, 82.0f, 42.9f, 3316.1f},
    {"SPACESTATION_LARGEROOM", -1000, -400, 3.89f, 0.38f, -1000, 0.0560f, -100, 0.0350f, 81.0f, 36.4f, 3316.1f},
    {"SPACESTATION_HALL", -1000, -400, 7.11f, 0.38f, -1500, 0.1000f, -400, 0.0470f, 87.0f, 42.9f, 3316.1f},
    {"SPACESTATION_CUPBOARD", -1000, -300, 0.79f, 0.81f, 300, 0.0070f, 500, 0.0180f, 56.0f, 17.2f, 3316.1f},
    {"SPACESTATION_SMALLROOM", -1000, -300, 1.72f, 0.82f, -200, 0.0070f, 300, 0.0130f, 70.0f, 21.1f, 3316.1f},
    {"WOODEN_ALCOVE", -1000, -1800, 1.22f, 0.62f, 100, 0.0120f, -300, 0.0240f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_SHORTPASSAGE", -1000, -1800, 1.75f, 0.50f, -100, 0.0120f, -400, 0.0240f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_MEDIUMROOM", -1000, -2000, 1.47f, 0.42f, -100, 0.0490f, -100, 0.0290f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_LONGPASSAGE", -1000, -2000, 1.99f, 0.40f, 0, 0.0200f, -700, 0.0360f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_LARGEROOM", -1000, -2100, 2.65f, 0.33f, -100, 0.0660f, -200, 0.0490f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_HALL", -1000, -2200, 3.45f, 0.30f, -100, 0.0880f, -200, 0.0630f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_CUPBOARD", -1000, -1700, 0.56f, 0.46f, 100, 0.0120f, 100, 0.0280f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_SMALLROOM", -1000, -1900, 0.79f, 0.32f, 0, 0.0320f, -100, 0.0290f, 100.0f, 100.0f, 4705.0f},
    {"WOODEN_COURTYARD", -1000, -2200, 1.79f, 0.35f, -500, 0.1230f, -2000, 0.0320f, 65.0f, 100.0f, 4705.0f},
    {"SPORT_EMPTYSTADIUM", -1000, -700, 6.26f, 0.51f, -2400, 0.1830f, -800, 0.0380f, 100.0f, 100.0f, 5000.0f},
    {"SPORT_SQUASHCOURT", -1000, -1000, 2.22f, 0.91f, -700, 0.0070f, -200, 0.0110f, 75.0f, 100.0f, 7176.9f},
    {"SPORT_SMALLSWIMMINGPOOL", -1000, -200, 2.76f, 1.25f, -400, 0.0200f, -200, 0.0300f, 70.0f, 100.0f, 5000.0f},
    {"SPORT_LARGESWIMMINGPOOL", -1000, -200, 5.49f, 1.31f, -700, 0.0390f, -600, 0.0490f, 82.0f, 100.0f, 5000.0f},
    {"SPORT_GYMNASIUM", -1000, -700, 3.14f, 1.06f, -800, 0.0290f, -500, 0.0450f, 81.0f, 100.0f, 7176.9f},
    {"SPORT_FULLSTADIUM", -1000, -2300, 5.25f, 0.17f, -2000, 0.1880f, -1100, 0.0380f, 100.0f, 100.0f, 5000.0f},
    {"SPORT_STADIUMTANNOY", -1000, -500, 2.53f, 0.88f, -1100, 0.2300f, -600, 0.0630f, 78.0f, 100.0f, 5000.0f},
    {"PREFAB_WORKSHOP", -1000, -1700, 0.76f, 1.00f, 0, 0.0120f, 100, 0.0120f, 100.0f, 42.9f, 5000.0f},
    {"PREFAB_SCHOOLROOM", -1000, -400, 0.98f, 0.45f, 300, 0.0170f, 300, 0.0150f, 69.0f, 40.2f, 7176.9f},
    {"PREFAB_PRACTISEROOM", -1000, -800, 1.12f, 0.56f, 200, 0.0100f, 300, 0.0110f, 87.0f, 40.2f, 7176.9f},
    {"PREFAB_OUTHOUSE", -1000, -1900, 1.38f, 0.38f, -100, 0.0240f, -400, 0.0440f, 82.0f, 100.0f, 2854.4f},
    {"PREFAB_CARAVAN", -1000, -2100, 0.43f, 1.50f, 0, 0.0120f, 600, 0.0120f, 100.0f, 100.0f, 5000.0f},
    {"DOME_TOMB", -1000, -900, 4.18f, 0.21f, -825, 0.0300f, 450, 0.0220f, 79.0f, 100.0f, 2854.4f},
    {"PIPE_SMALL", -1000, -900, 5.04f, 0.10f, -600, 0.0320f, 800, 0.0150f, 100.0f, 100.0f, 2854.4f},
    {"DOME_SAINTPAULS", -1000, -900, 10.48f, 0.19f, -1500, 0.0900f, 200, 0.0420f, 87.0f, 100.0f, 2854.4f},
    {"PIPE_LONGTHIN", -1000, -700, 9.21f, 0.18f, -300, 0.0100f, -300, 0.0220f, 91.0f, 25.6f, 2854.4f},
    {"PIPE_LARGE", -1000, -900, 8.45f, 0.10f, -800, 0.0460f, 400, 0.0320f, 100.0f, 100.0f, 2854.4f},
    {"PIPE_RESONANT", -1000, -700, 6.81f, 0.18f, -300, 0.0100f, 0, 0.0220f, 91.0f, 13.7f, 2854.4f},
    {"OUTDOORS_BACKYARD", -1000, -1200, 1.12f, 0.34f, -700, 0.0690f, -300, 0.0230f, 45.0f, 100.0f, 4399.1f},
    {"OUTDOORS_ROLLINGPLAINS", -1000, -3902, 2.13f, 0.21f, -1500, 0.3000f, -700, 0.0190f, 0.0f, 100.0f, 4399.1f},
    {"OUTDOORS_DEEPCANYON", -1000, -1500, 3.89f, 0.21f, -1000, 0.2230f, -900, 0.0190f, 74.0f, 100.0f, 4399.1f},
    {"OUTDOORS_CREEK", -1000, -1500, 2.13f, 0.21f, -800, 0.1150f, -1400, 0.0310f, 35.0f, 100.0f, 4399.1f},
    {"OUTDOORS_VALLEY", -1000, -3100, 2.88f, 0.26f, -1700, 0.2630f, -800, 0.1000f, 28.0f, 100.0f, 2854.4f},
    {"MOOD_HEAVEN", -1000, -200, 5.04f, 1.12f, -1230, 0.0200f, 200, 0.0290f, 94.0f, 100.0f, 5000.0f},
    {"MOOD_HELL", -1000, -900, 3.57f, 0.49f, -10000, 0.0200f, 300, 0.0300f, 57.0f, 100.0f, 5000.0f},
    {"MOOD_MEMORY", -1000, -400, 4.06f, 0.82f, -2800, 0.0000f, 100, 0.0000f, 85.0f, 100.0f, 5000.0f},
    {"DRIVING_COMMENTATOR", -1000, -500, 2.42f, 0.88f, -1400, 0.0930f, -1200, 0.0170f, 0.0f, 100.0f, 5000.0f},
    {"DRIVING_PITGARAGE", -1000, -300, 1.72f, 0.93f, -500, 0.0000f, 200, 0.0160f, 59.0f, 42.9f, 5000.0f},
    {"DRIVING_INCAR_RACER", -1000, 0, 0.17f, 2.00f, 500, 0.0070f, -300, 0.0150f, 80.0f, 8.3f, 10268.2f},
    {"DRIVING_INCAR_SPORTS", -1000, -400, 0.17f, 0.75f, 0, 0.0100f, -500, 0.0000f, 80.0f, 8.3f, 10268.2f},
    {"DRIVING_INCAR_LUXURY", -1000, -2000, 0.13f, 0.41f, -200, 0.0100f, 400, 0.0100f, 100.0f, 25.6f, 10268.2f},
    {"DRIVING_FULLGRANDSTAND", -1000, -1100, 3.01f, 1.37f, -900, 0.0900f, -1500, 0.0490f, 100.0f, 100.0f, 10420.2f},
    {"DRIVING_EMPTYGRANDSTAND", -1000, 0, 4.62f, 1.75f, -1363, 0.0900f, -1200, 0.0490f, 100.0f, 100.0f, 10420.2f},
    {"DRIVING_TUNNEL", -1000, -800, 3.42f, 0.94f, -300, 0.0510f, -300, 0.0470f, 81.0f, 100.0f, 5000.0f},
    {"CITY_STREETS", -1000, -300, 1.79f, 1.12f, -1100, 0.0460f, -1400, 0.0280f, 78.0f, 100.0f, 5000.0f},
    {"CITY_SUBWAY", -1000, -300, 3.01f, 1.23f, -300, 0.0460f, 200, 0.0280f, 74.0f, 100.0f, 5000.0f},
    {"CITY_MUSEUM", -1000, -1500, 3.28f, 1.40f, -1200, 0.0390f, -100, 0.0340f, 82.0f, 100.0f, 2854.4f},
    {"CITY_LIBRARY", -1000, -1100, 2.76f, 0.89f, -900, 0.0290f, -100, 0.0200f, 82.0f, 100.0f, 2854.4f},
    {"CITY_UNDERPASS", -1000, -700, 3.57f, 1.12f, -800, 0.0590f, -100, 0.0370f, 82.0f, 100.0f, 5000.0f},
    {"CITY_ABANDONED", -1000, -200, 3.28f, 1.17f, -700, 0.0440f, -1100, 0.0240f, 69.0f, 100.0f, 5000.0f},
    {"DUSTYROOM", -1000, -200, 1.79f, 0.38f, -600, 0.0020f, 200, 0.0060f, 56.0f, 36.4f, 13046.0f},
    {"CHAPEL", -1000, -500, 4.62f, 0.64f, -700, 0.0320f, -200, 0.0490f, 84.0f, 100.0f, 5000.0f},
    {"SMALLWATERROOM", -1000, -698, 1.51f, 1.25f, -100, 0.0200f, 300, 0.0300f, 70.0f, 100.0f, 5000.0f},
};
constexpr int kPresetCount = int(sizeof kPresets / sizeof kPresets[0]);

// Level-Nummer (wie in levelNN; der Umgebungs-Lader 0x1410F6DA8 setzt damit
// "host0:bg/level%d/level%d.h" zusammen, Xbox 0x4A492C).
constexpr uint64_t kLevelNumber = 0x143F27F54;
// Raum der Kamera (Xbox +0x294; Modus 8 setzt ihn aus Spieler +0xD78). Die
// Nummern sind die von LEVELnn_ROOMNUM_* in bg/levelNN/levelNN.h, ab 1;
// einige Tabellen haben auch einen Eintrag für 0. Räume ohne Eintrag
// bleiben trocken (z. B. Level 45: nur Kapelle und Gänge).
constexpr uint32_t kCamRoom = 0x2B8;
constexpr int kMaxRooms = 4096;

const int32_t* g_level = nullptr;
uint8_t* const* g_camera = nullptr;
std::filesystem::path g_dir;  // <game>/ts2/sound/reverb/overrides
int g_loaded_level = INT_MIN;
std::vector<int16_t> g_room_preset;  // Raum -> Index in kPresets, -1 = keiner
int g_fallback = -1;  // Voreinstellung für Räume ohne Eintrag (häufigste des Levels)
int g_force = -1;  // TS2_HALLTEST=<Name>: diese Voreinstellung in jedem Raum (Prüfung)

int find_preset(const char* name) {
    static const char kPrefix[] = "k_rvb_REVERB_PRESET_";
    if (std::strncmp(name, kPrefix, sizeof kPrefix - 1) == 0) name += sizeof kPrefix - 1;
    for (int i = 0; i < kPresetCount; ++i)
        if (std::strcmp(kPresets[i].name, name) == 0) return i;
    return -1;
}

// ts2/sound/reverb/overrides/levelNN.reverb: Zeilen "Raum : Name"
void load_level(int level) {
    g_loaded_level = level;
    g_room_preset.clear();
    g_fallback = -1;
    if (level <= 0 || level > 999) return;
    wchar_t file[32];
    swprintf_s(file, L"level%d.reverb", level);
    FILE* f = _wfopen((g_dir / file).c_str(), L"rb");
    if (!f) {
        log("[ton] Hall: Level %d ohne Raumtabelle, kein Hall\n", level);
        return;
    }
    char line[256], name[128];
    int rooms = 0, bad = 0;
    while (std::fgets(line, sizeof line, f)) {
        int room = 0;
        if (std::sscanf(line, " %d : %127s", &room, name) != 2) continue;
        const int p = find_preset(name);
        if (room < 0 || room >= kMaxRooms || p < 0) {
            ++bad;
            continue;
        }
        if (int(g_room_preset.size()) <= room) g_room_preset.resize(size_t(room) + 1, -1);
        g_room_preset[size_t(room)] = int16_t(p);
        ++rooms;
    }
    std::fclose(f);
    // Räume ohne Eintrag: häufigste Voreinstellung des Levels (Entscheidung:
    // Hall für Umgebung und Waffen überall, auch z. B. im Siberia-Starttunnel,
    // den die Tabelle auslässt; die Xbox hatte dort keinen Hall)
    g_fallback = -1;
    {
        std::vector<int> count(kPresetCount, 0);
        for (int16_t p : g_room_preset)
            if (p >= 0) ++count[size_t(p)];
        int best = 0;
        for (int i = 0; i < kPresetCount; ++i)
            if (count[size_t(i)] > best) best = count[size_t(i)], g_fallback = i;
    }
    log("[ton] Hall: Level %d, %d Räume mit Voreinstellung (%d unbekannt), übrige: %s\n", level, rooms, bad,
        g_fallback >= 0 ? kPresets[g_fallback].name : "kein Hall");
}

// Der Kamera-Zeiger kann zwischen Levels auf alten Speicher zeigen; der Ton-
// Thread liest nur, also abgesichert.
int read_room() {
    __try {
        const uint8_t* cam = *g_camera;
        if (!cam) return -1;
        return *reinterpret_cast<const int32_t*>(cam + kCamRoom);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

}  // namespace

void reverb_init(const Ts2Image& img) {
    g_level = img.va<int32_t>(kLevelNumber);
    g_camera = img.va<uint8_t*>(hf2::kCamera);
    std::error_code ec;
    g_dir = std::filesystem::current_path(ec) / L"ts2" / L"sound" / L"reverb" / L"overrides";
    char env[64] = {};
    if (GetEnvironmentVariableA("TS2_HALLTEST", env, sizeof env) > 0 && env[0]) {
        g_force = find_preset(env);
        log("[ton] Hall: TS2_HALLTEST=%s -> %s\n", env, g_force >= 0 ? kPresets[g_force].name : "unbekannt");
    }
}

bool Hall::create(IXAudio2* xa, IXAudio2Voice* dest, UINT32 dest_channels, int rate, int out_channels) {
    if (rate < XAUDIO2FX_REVERB_MIN_FRAMERATE || rate > XAUDIO2FX_REVERB_MAX_FRAMERATE) {
        log("[ton] Hall: %d Hz nicht unterstützt (20000…48000), kein Hall\n", rate);
        return false;
    }
    IUnknown* apo = nullptr;
    if (FAILED(XAudio2CreateReverb(&apo, 0))) {
        log("[ton] Hall: XAudio2CreateReverb fehlgeschlagen, kein Hall\n");
        return false;
    }
    XAUDIO2_EFFECT_DESCRIPTOR ed{apo, TRUE, UINT32(out_channels)};
    XAUDIO2_EFFECT_CHAIN chain{1, &ed};
    XAUDIO2_SEND_DESCRIPTOR sd{0, dest};
    XAUDIO2_VOICE_SENDS sends{1, &sd};
    const HRESULT hr = xa->CreateSubmixVoice(&voice_, 2, UINT32(rate), 0, 0, &sends, &chain);
    apo->Release();
    if (FAILED(hr)) {
        log("[ton] Hall: CreateSubmixVoice fehlgeschlagen (0x%08X), kein Hall\n", hr);
        voice_ = nullptr;
        return false;
    }
    // Hall-Kanal i -> Gerätekanal i, wie die trockene Ausgabe
    std::vector<float> m(size_t(out_channels) * dest_channels, 0.0f);
    for (UINT32 i = 0; i < UINT32(out_channels) && i < dest_channels; ++i) m[i * out_channels + i] = 1.0f;
    voice_->SetOutputMatrix(dest, UINT32(out_channels), dest_channels, m.data());
    apply(find_preset("GENERIC"));
    current_ = -2;
    log("[ton] Hall: XAudio2-Hall bereit (Stereo -> %d Kanäle, %d Hz)\n", out_channels, rate);
    return true;
}

void Hall::destroy() {
    if (voice_) voice_->DestroyVoice(), voice_ = nullptr;
}

void Hall::apply(int preset) {
    if (!voice_ || preset < 0) return;
    const Preset& p = kPresets[preset];
    XAUDIO2FX_REVERB_I3DL2_PARAMETERS i3{};
    i3.WetDryMix = 100.0f;  // der Submix bekommt nur den Hall-Anteil
    i3.Room = p.room;
    i3.RoomHF = p.room_hf;
    i3.RoomRolloffFactor = 0.0f;
    i3.DecayTime = p.decay;
    i3.DecayHFRatio = p.decay_hf_ratio;
    i3.Reflections = p.reflections;
    i3.ReflectionsDelay = p.reflections_delay;
    i3.Reverb = p.reverb;
    i3.ReverbDelay = p.reverb_delay;
    i3.Diffusion = p.diffusion;
    i3.Density = p.density;
    i3.HFReference = p.hf_reference;
    XAUDIO2FX_REVERB_PARAMETERS native{};
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN10)
    ReverbConvertI3DL2ToNative(&i3, &native, FALSE);  // 5.1, nicht 7.1
#else
    ReverbConvertI3DL2ToNative(&i3, &native);
#endif
    voice_->SetEffectParameters(0, &native, sizeof native);
    applied_ = preset;
}

bool Hall::update() {
    if (!voice_ || !g_level) return false;
    const int level = *g_level;
    if (level != g_loaded_level) load_level(level);
    const int room = read_room();
    int p = (room >= 0 && room < int(g_room_preset.size())) ? g_room_preset[size_t(room)] : -1;
    if (p < 0 && room >= 0 && room < kMaxRooms && level > 0) p = g_fallback;
    if (g_force >= 0 && room >= 0 && room < kMaxRooms && level > 0) p = g_force;
    if (p != current_) {
        if (p >= 0 && p != applied_) apply(p);
        log("[ton] Hall: Level %d, Raum %d -> %s\n", level, room, p >= 0 ? kPresets[p].name : "kein Hall");
        current_ = p;
    }
    last_room_ = room;
    return p >= 0;
}

}  // namespace host
