// Ersatz für die 31 fremden Funktionen und 31 Importe des TS2-Codes.
// Die Originale (VA bei ImageBase 0x140000000) sind in docs/host.md
// beschrieben; hier steht je Eintrag, was das Original tut.
#include "replace.h"

#include <windows.h>

#include <cmath>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <float.h>
#include <mutex>
#include <set>
#include <string>

#include "log.h"

namespace host {

char g_overlay[64] = "";  // Vorsatz für überlagerte Dateien, leer = aus

void set_file_overlay(const char* prefix) { std::snprintf(g_overlay, sizeof g_overlay, "%s", prefix ? prefix : ""); }

namespace {

// ---------------------------------------------------------------- Speicher
// 0x140C6F8B0 CryMalloc(size) und 0x140C6E840 CryFree(p) samt ihren Hüllen
// 0x140001D00 (operator new) und 0x140001D10 (operator delete): CryEngines
// Speicherverwaltung. Hier gewöhnlicher Heap mit 16-Byte-Ausrichtung.
void* ts2_malloc(size_t n) { return _aligned_malloc(n ? n : 1, 16); }
void ts2_free(void* p) {
    if (p) _aligned_free(p);
}

// ---------------------------------------------------------------- Synchronisation
// 0x1400012D0: InitializeCriticalSection(cs), gibt cs zurück
void* init_cs(CRITICAL_SECTION* cs) {
    InitializeCriticalSection(cs);
    return cs;
}
// 0x1400015F0: ReleaseSemaphore(*holder, 1, nullptr)
BOOL release_sem(HANDLE* holder) { return ReleaseSemaphore(*holder, 1, nullptr); }

// ---------------------------------------------------------------- Leere Engine-Funktionen
// 0x1408761DC: `ret` (ausgebaute Protokollierung), 0x14095E660 springt dorthin
void nothing() {}
// 0x14088251C: `return 0`
uint64_t zero() { return 0; }
// 0x1402E5830: `return 4`
uint64_t four() { return 4; }

// 0x140004370: CryStringT<char>::assign (Zeiger, Länge). Nur im Pfad, der
// lsao_cached.pak über ICryPak öffnet (0x141225770); den ersetzt ts2.exe.
void* crystring_assign(void* self, const char*, size_t) {
    log("[ersatz] CryString::assign gerufen (unerwartet)\n");
    return self;
}

// ---------------------------------------------------------------- libvorbisfile
// Statisch gelinktes vorbisfile, von TS2s Musik/Sprache benutzt; Ersatz mit
// stb_vorbis in vorbis.cpp.
}  // namespace
struct OvCallbacks;
int ov_open_callbacks_x(void*, void*, const char*, long, const OvCallbacks*);
int ov_clear_x(void*);
void* ov_info_x(void*, int);
int64_t ov_pcm_total_x(void*, int);
int ov_time_seek_x(void*, double);
long ov_read_x(void*, char*, int, int, int, int, int*);
namespace {

// ---------------------------------------------------------------- C-Laufzeit (msvcr100)
// __iob_func liefert in msvcr100 ein Feld von FILE-Strukturen à 0x30 Byte;
// TS2 rechnet damit &iob[1] (stdout) und &iob[2] (stderr). Hier ein
// Platzhalterfeld, das die Datei-Funktionen unten auf die echten Ströme abbilden.
alignas(16) uint8_t g_fake_iob[3][0x30];
void* iob_func() { return g_fake_iob; }
FILE* real_file(FILE* f) {
    auto* p = reinterpret_cast<uint8_t*>(f);
    for (int i = 0; i < 3; ++i)
        if (p == g_fake_iob[i]) return __acrt_iob_func(unsigned(i));
    return f;
}
int x_fflush(FILE* f) { return std::fflush(f ? real_file(f) : nullptr); }
int x_fclose(FILE* f) { return std::fclose(real_file(f)); }
size_t x_fread(void* b, size_t s, size_t n, FILE* f) { return std::fread(b, s, n, real_file(f)); }
size_t x_fwrite(const void* b, size_t s, size_t n, FILE* f) { return std::fwrite(b, s, n, real_file(f)); }
long x_ftell(FILE* f) { return std::ftell(real_file(f)); }
int x_fseek(FILE* f, long o, int w) { return std::fseek(real_file(f), o, w); }
FILE* x_fopen(const char* n, const char* m) {
    // Überlagerung (z. B. "redux/" mit den HD-Texturen von TS2 Redux): nur
    // zum Lesen, nur wenn die Datei dort liegt
    if (g_overlay[0] && m && m[0] == 'r') {
        char p[512];
        std::snprintf(p, sizeof p, "%s%s", g_overlay, n);
        if (FILE* o = std::fopen(p, m)) {
            log("[datei] %s %s\n", m, p);
            return o;
        }
    }
    FILE* f = std::fopen(n, m);
    log("[datei] %s %s%s\n", m, n, f ? "" : "  -> fehlt");
    return f;
}
int x_vsnprintf(char* b, size_t n, const char* f, va_list a) { return _vsnprintf(b, n, f, a); }
int x_sprintf(char* b, const char* f, ...) {
    va_list a;
    va_start(a, f);
    int r = vsprintf(b, f, a);
    va_end(a);
    return r;
}
int x_sscanf(const char* s, const char* f, void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6,
             void* a7) {
    // msvcr100-sscanf ist variadisch; bis zu acht Zielzeiger reichen für TS2.
    return std::sscanf(s, f, a0, a1, a2, a3, a4, a5, a6, a7);
}
unsigned x_controlfp(unsigned n, unsigned m) { return _controlfp(n, m); }
void x_exit(int c) {
    // Aufrufkette (Rücksprungadressen) für die Fehlersuche
    void* frames[12] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
    std::string chain;
    char buf[24];
    for (USHORT i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof buf, " %p", frames[i]);
        chain += buf;
    }
    // TS2-Code hat keine Abwicklungsdaten: Stapel roh nach Adressen im
    // TS2-Bereich durchsuchen (mögliche Rücksprungadressen)
    const auto* sp = static_cast<const uint64_t*>(_AddressOfReturnAddress());
    for (int i = 0; i < 64; ++i)
        if (sp[i] >= 0x141069000ull && sp[i] < 0x141C60000ull) {
            std::snprintf(buf, sizeof buf, " [%d]%llX", i, static_cast<unsigned long long>(sp[i]));
            chain += buf;
        }
    log("[ersatz] TS2 ruft exit(%d), Aufrufer:%s\n", c, chain.c_str());
    std::exit(c);
}

// Mathe, Speicher (über die CRT-Thunks bei 0x141C50xxx gerufen)
void* x_memcpy(void* d, const void* s, size_t n) { return std::memcpy(d, s, n); }
void* x_memset(void* d, int c, size_t n) { return std::memset(d, c, n); }
int x_memcmp(const void* a, const void* b, size_t n) { return std::memcmp(a, b, n); }
void* x_memmove(void* d, const void* s, size_t n) { return std::memmove(d, s, n); }
float x_powf(float a, float b) { return std::pow(a, b); }
float x_sinf(float a) { return std::sin(a); }
float x_cosf(float a) { return std::cos(a); }
float x_tanf(float a) { return std::tan(a); }
float x_asinf(float a) { return std::asin(a); }
float x_acosf(float a) { return std::acos(a); }
float x_atanf(float a) { return std::atan(a); }
float x_atan2f(float a, float b) { return std::atan2(a, b); }
const char* x_strstr(const char* a, const char* b) { return std::strstr(a, b); }
const char* x_strchr(const char* a, int c) { return std::strchr(a, c); }
const char* x_strrchr(const char* a, int c) { return std::strrchr(a, c); }
char* x_strncpy(char* d, const char* s, size_t n) { return std::strncpy(d, s, n); }
int x_strncmp(const char* a, const char* b, size_t n) { return std::strncmp(a, b, n); }
int x_atoi(const char* s) { return std::atoi(s); }
int x_tolower(int c) { return std::tolower(c); }
int x_getchar() { return std::getchar(); }

struct Rep {
    uint32_t rva;
    const void* fn;
};
#define F(x) reinterpret_cast<const void*>(&x)
const Rep kRep[] = {
    {0x000012D0, F(init_cs)},
    {0x000012F0, F(EnterCriticalSection)},
    {0x00001300, F(LeaveCriticalSection)},
    {0x000015F0, F(release_sem)},
    {0x00001D00, F(ts2_malloc)},
    {0x00001D10, F(ts2_free)},
    {0x00001EC0, F(DeleteCriticalSection)},
    {0x00004370, F(crystring_assign)},
    {0x002E5830, F(four)},
    {0x008761DC, F(nothing)},
    {0x0088251C, F(zero)},
    {0x0095E660, F(nothing)},
    {0x00C6E840, F(ts2_free)},
    {0x00C6F8B0, F(ts2_malloc)},
    {0x01C32B60, F(ov_clear_x)},
    {0x01C32C80, F(ov_pcm_total_x)},
    {0x01C33A80, F(ov_time_seek_x)},
    {0x01C33B50, F(ov_info_x)},
    {0x01C33FF0, F(ov_read_x)},
    {0x01C34420, F(ov_open_callbacks_x)},
    {0x01C50C6E, F(x_memcpy)},
    {0x01C50C74, F(x_memset)},
    {0x01C50C80, F(x_powf)},
    {0x01C50E7C, F(x_acosf)},
    {0x01C50E88, F(x_asinf)},
    {0x01C50E8E, F(x_atan2f)},
    {0x01C50E94, F(x_tanf)},
    {0x01C50EBE, F(x_cosf)},
    {0x01C50EC4, F(x_memcmp)},
    {0x01C50ECA, F(x_atanf)},
    {0x01C50ED6, F(x_sinf)},
};

struct Imp {
    const char* name;
    const void* fn;
};
const Imp kCrt[] = {
    {"_controlfp", F(x_controlfp)}, {"getchar", F(x_getchar)}, {"exit", F(x_exit)},
    {"__iob_func", F(iob_func)},    {"fflush", F(x_fflush)},   {"fopen", F(x_fopen)},
    {"strstr", F(x_strstr)},        {"memmove", F(x_memmove)}, {"sprintf", F(x_sprintf)},
    {"strchr", F(x_strchr)},        {"strrchr", F(x_strrchr)}, {"fclose", F(x_fclose)},
    {"fread", F(x_fread)},          {"ftell", F(x_ftell)},     {"fseek", F(x_fseek)},
    {"fwrite", F(x_fwrite)},        {"atoi", F(x_atoi)},       {"strncpy", F(x_strncpy)},
    {"sscanf", F(x_sscanf)},        {"_vsnprintf", F(x_vsnprintf)}, {"tolower", F(x_tolower)},
    {"strncmp", F(x_strncmp)},
};
#undef F

}  // namespace

const void* replacement_for(uint32_t rva) {
    for (auto& r : kRep)
        if (r.rva == rva) return r.fn;
    return nullptr;
}

void WINAPI precise_sleep(DWORD ms) {
    if (ms == 0) {
        SwitchToThread();
        return;
    }
    static thread_local HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) {
        Sleep(ms);
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -LONGLONG(ms) * 10000;  // 100-ns-Schritte, relativ
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
}

const void* import_for(const char* dll, const char* name) {
    if (_stricmp(dll, "msvcr100.dll") == 0) {
        for (auto& i : kCrt)
            if (std::strcmp(i.name, name) == 0) return i.fn;
        return nullptr;
    }
    // Sleep: TS2 schläft bis kurz vor seinen nächsten Takt und wartet den
    // Rest aktiv ab (0x14107CC64). Ein hochauflösender Zeitgeber schläft
    // genauer als Sleep (auch mit timeBeginPeriod).
    if (_stricmp(dll, "kernel32.dll") == 0 && std::strcmp(name, "Sleep") == 0) return reinterpret_cast<const void*>(&precise_sleep);
    // Windows selbst: kernel32, user32, advapi32
    HMODULE h = LoadLibraryA(dll);
    return h ? reinterpret_cast<const void*>(GetProcAddress(h, name)) : nullptr;
}

}  // namespace host
