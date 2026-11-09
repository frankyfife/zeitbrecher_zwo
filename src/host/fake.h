// Attrappen für die Engine-Schnittstellen, die TS2 über gEnv erreicht.
// Jede Methode protokolliert ihren ersten Aufruf ("gEnv+0x60 ->+0x30") und
// liefert 0. Einzelne Methoden werden nach und nach durch echte ersetzt.
#pragma once
#include <cstdint>

namespace host {

struct Fake;
// Objekt mit vtable aus `slots` protokollierenden Methoden und `fields` Byte
// genullter Felder hinter dem vtable-Zeiger.
Fake* make_fake(const char* name, int slots = 512, int fields = 0x1000);
void* fake_object(Fake* f);
// Ersetzt die Methode an vtable-Offset `offset` (Byte) durch fn.
void fake_set(Fake* f, uint32_t offset, const void* fn);
// Die Methode an `offset` liefert immer `value` (z. B. ein weiteres Fake-Objekt).
void fake_returns(Fake* f, uint32_t offset, const void* value);

}  // namespace host
