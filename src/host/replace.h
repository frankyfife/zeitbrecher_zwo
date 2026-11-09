// Eigener Ersatz für alles, was der TS2-Code außerhalb seines Blocks ruft
// (Liste: kExternal und kImports in ts2_manifest.inc).
#pragma once
#include <cstdint>

namespace host {
// Ersatz für die fremde Funktion an dieser RVA oder nullptr.
const void* replacement_for(uint32_t rva);
// Ersatz für einen Import des TS2-Codes oder nullptr.
const void* import_for(const char* dll, const char* name);
}  // namespace host
