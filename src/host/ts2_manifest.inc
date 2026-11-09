// Erzeugt von tools/re/make_manifest.py – nicht von Hand ändern.
// Adressen als RVA in Homefront2_Release.exe (TimeDateStamp 0x58E252DC).
#pragma once
namespace manifest {
struct Range { uint32_t rva, size; };
struct Import { uint32_t slot; const char* dll; const char* name; };
struct External { uint32_t rva; uint32_t uses; };
constexpr uint32_t kTimeDateStamp = 0x58E252DC;
constexpr uint32_t kSizeOfImage = 0x04311000;
constexpr Range kText[] = {
  {0x01069B90, 0x1C7BD6},
  {0x01C50CA0, 0x50},
};
constexpr Range kPages[] = {
  {0x00000000, 0x1000}, // PE-Kopf: Windows-Funktionen bekommen die Modulbasis als HINSTANCE
  {0x01F8D000, 0x260000}, // .rdata
  {0x022AF000, 0xA000}, // .rdata
  {0x02479000, 0x139000}, // .data
};
constexpr Range kBss = {0x025B2000, 0x1B299C8};
constexpr Import kImports[] = {
  {0x01F7D038, "advapi32.dll", "GetUserNameA"},
  {0x01F7D430, "kernel32.dll", "VirtualQuery"},
  {0x01F7D460, "kernel32.dll", "QueryPerformanceCounter"},
  {0x01F7D468, "kernel32.dll", "QueryPerformanceFrequency"},
  {0x01F7D488, "kernel32.dll", "GetLastError"},
  {0x01F7D560, "kernel32.dll", "Sleep"},
  {0x01F7D5C0, "kernel32.dll", "CreateMutexA"},
  {0x01F7D908, "msvcr100.dll", "_controlfp"},
  {0x01F7D910, "msvcr100.dll", "getchar"},
  {0x01F7D9D8, "msvcr100.dll", "exit"},
  {0x01F7DAC8, "msvcr100.dll", "__iob_func"},
  {0x01F7DAF0, "msvcr100.dll", "fflush"},
  {0x01F7DC40, "msvcr100.dll", "fopen"},
  {0x01F7DCE8, "msvcr100.dll", "strstr"},
  {0x01F7DCF8, "msvcr100.dll", "memmove"},
  {0x01F7DD10, "msvcr100.dll", "sprintf"},
  {0x01F7DD28, "msvcr100.dll", "strchr"},
  {0x01F7DD38, "msvcr100.dll", "strrchr"},
  {0x01F7DD68, "msvcr100.dll", "fclose"},
  {0x01F7DD70, "msvcr100.dll", "fread"},
  {0x01F7DD78, "msvcr100.dll", "ftell"},
  {0x01F7DD80, "msvcr100.dll", "fseek"},
  {0x01F7DDC0, "msvcr100.dll", "fwrite"},
  {0x01F7DDE8, "msvcr100.dll", "atoi"},
  {0x01F7DE20, "msvcr100.dll", "strncpy"},
  {0x01F7DE30, "msvcr100.dll", "sscanf"},
  {0x01F7DE48, "msvcr100.dll", "_vsnprintf"},
  {0x01F7DE78, "msvcr100.dll", "tolower"},
  {0x01F7DE90, "msvcr100.dll", "strncmp"},
  {0x01F7E0A8, "user32.dll", "LoadCursorA"},
  {0x01F7E228, "user32.dll", "SetCursor"},
};
constexpr External kExternal[] = {
  {0x000012D0, 2},
  {0x000012F0, 7},
  {0x00001300, 7},
  {0x000015F0, 1},
  {0x00001D00, 2},
  {0x00001D10, 3},
  {0x00001EC0, 2},
  {0x00004370, 1},
  {0x002E5830, 9},
  {0x008761DC, 237},
  {0x0088251C, 64},
  {0x0095E660, 2},
  {0x00C6E840, 3},
  {0x00C6F8B0, 1},
  {0x01C32B60, 3},
  {0x01C32C80, 1},
  {0x01C33A80, 1},
  {0x01C33B50, 2},
  {0x01C33FF0, 1},
  {0x01C34420, 2},
  {0x01C50C6E, 178},
  {0x01C50C74, 311},
  {0x01C50C80, 4},
  {0x01C50E7C, 16},
  {0x01C50E88, 3},
  {0x01C50E8E, 104},
  {0x01C50E94, 2},
  {0x01C50EBE, 58},
  {0x01C50EC4, 9},
  {0x01C50ECA, 5},
  {0x01C50ED6, 85},
};
}  // namespace manifest
