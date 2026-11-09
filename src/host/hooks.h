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
void* hook_call(const Ts2Image& img, uint64_t call_va, uint64_t expected_target, const void* fn);

}  // namespace host
