// ts2.bin: der TS2-Code und seine Daten, einmalig aus der eigenen
// Homefront2_Release.exe herausgelöst (src/host/ts2_manifest.inc sagt, was).
//
// Aufbau (little endian):
//   "ZB2T" u32 Version(1) u32 TimeDateStamp u32 SizeOfImage u64 ImageBase
//   u32 Bereichszahl; je Bereich: u32 RVA, u32 Größe, u32 Art (1 Code,
//       2 nur lesen, 3 lesen/schreiben), Größe Byte
//   u32 Relocationzahl; je Relocation: u32 RVA (IMAGE_REL_BASED_DIR64)
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace host {

struct Ts2Image {
    uint8_t* base = nullptr;        // Beginn der Reservierung (= ImageBase, wenn frei)
    uint64_t preferred_base = 0;
    uint32_t size = 0;
    template <class T = uint8_t>
    T* va(uint64_t original_va) const {
        return reinterpret_cast<T*>(base + (original_va - preferred_base));
    }
    template <class T = uint8_t>
    T* rva(uint32_t r) const {
        return reinterpret_cast<T*>(base + r);
    }
};

// Liest die EXE, prüft den Build und schreibt ts2.bin.
bool write_ts2bin(const std::filesystem::path& exe, const std::filesystem::path& out, std::string* err);

// Lädt ts2.bin, legt die Ersatzfunktionen an und bindet die Importe.
bool load_ts2bin(const std::filesystem::path& bin, Ts2Image* img, std::string* err);

}  // namespace host
