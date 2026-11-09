#pragma once
#include <cstdint>

namespace host {
// Schreibt nach stderr und ts2_host.log (neben der EXE).
void log(const char* fmt, ...);
// Erzeugt einen Thunk: rcx = rcx_value, edx = edx_value, dann Sprung nach handler.
uint8_t* make_call_thunk(uint64_t rcx_value, uint32_t edx_value, const void* handler);
}  // namespace host
