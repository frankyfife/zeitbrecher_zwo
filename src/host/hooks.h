// Umwege in den TS2-Code: einen einzelnen Aufruf (E8 rel32) auf eigenen
// Code lenken. Der eigene Code liegt außerhalb von ±2 GB; deshalb geht der
// Aufruf über einen Sprung (jmp [rip]) in einer eigenen Seite innerhalb der
// Reservierung von ts2.bin.
#pragma once
#include <cstdint>

#include "ts2bin.h"

namespace host {

// Lenkt den Aufruf an `call_va` auf `fn` um und liefert das bisherige Ziel,
// oder nullptr, wenn dort kein Aufruf von `expected_target` steht.
// Auch ein Sprung (E9 rel32, Endaufruf) geht.
void* hook_call(const Ts2Image& img, uint64_t call_va, uint64_t expected_target, const void* fn);

// Sprung (jmp [rip]) auf `fn` in der Stub-Seite, also in Reichweite eines
// rel32 aus dem TS2-Code (z. B. als neues Ziel eines lea auf einen
// Rückruf); nullptr bei Fehler.
void* jump_stub(const Ts2Image& img, const void* fn);

// Ersetzt den Befehl an `va` (genau `expected`, n ≥ 5 Byte) durch einen
// Aufruf eines Code-Stücks `stub_code` (wird in die Stub-Seite kopiert, endet
// mit ret) und füllt den Rest mit nop.
bool patch_with_call(const Ts2Image& img, uint64_t va, const uint8_t* expected, size_t n, const uint8_t* stub_code,
                     size_t stub_n);

}  // namespace host
