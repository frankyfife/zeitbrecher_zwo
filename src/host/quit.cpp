// Hauptmenü "Quit"/"Beenden".
//
// Die Aktion des Eintrags (0x1410C0D80, 11 Byte: mov dword [0x142F2C378], 1;
// ret) setzt nur ein Flag. Homefront fragte es über die vtable des
// TS2-Laufzeitobjekts ab (+0x38, 0x14122BE88: "läuft noch" = Flag 0) und
// kehrte dann in sein eigenes Menü zurück; ts2.exe fragt es nicht ab, der
// Eintrag tat deshalb nichts. Dasselbe Flag setzt auch der Takt der
// Hauptmenüseite (0x1410D134C) bei "Zurück" oder "Start" (Tasten 0x120) –
// für Homefront der Weg zurück. Deshalb liest ts2.exe nicht das Flag,
// sondern lenkt nur die Aktion des Eintrags um: "Zurück"/Esc im Hauptmenü
// beendet weiterhin nicht (kein versehentliches Beenden ohne Abfrage).
//
// Die eigene Aktion setzt das Flag wie das Original und schickt dem
// Spielfenster WM_CLOSE: Danach läuft alles wie beim Schließen des Fensters
// (DestroyWindow → WM_DESTROY → PostQuitMessage → Hauptschleife endet,
// Ton aus, Profil per atexit gespeichert). Eine Sicherheitsabfrage hat TS2
// an dieser Stelle nicht (auch nicht in der Homefront-Fassung).
#include "quit.h"

#include <windows.h>

#include <cstring>

#include "hf2.h"
#include "hooks.h"
#include "log.h"

namespace host {
namespace {

int32_t* g_quit_flag = nullptr;  // 0x142F2C378

BOOL CALLBACK find_window(HWND h, LPARAM out) {
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    *reinterpret_cast<HWND*>(out) = h;
    return FALSE;
}

void quit_action() {
    if (g_quit_flag) *g_quit_flag = 1;  // wie das Original
    // Das Spielfenster gehört dem Hauptfaden, auf dem TS2s Takt läuft
    HWND wnd = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), find_window, reinterpret_cast<LPARAM>(&wnd));
    log("[beenden] Hauptmenü \"Quit\" gewählt, Fenster wird geschlossen\n");
    if (!wnd || !PostMessageW(wnd, WM_CLOSE, 0, 0)) PostQuitMessage(0);
}

}  // namespace

void quit_install(const Ts2Image& img) {
    uint8_t* at = img.va(hf2::kQuit);
    // C7 05 rel32 01000000 C3: mov dword ptr [rip+rel32], 1; ret
    static const uint8_t head[2] = {0xC7, 0x05}, tail[5] = {0x01, 0x00, 0x00, 0x00, 0xC3};
    if (std::memcmp(at, head, 2) != 0 || std::memcmp(at + 6, tail, 5) != 0) {
        log("[beenden] Quit-Aktion 0x%llX unerwartet, nicht umgelenkt\n",
            static_cast<unsigned long long>(hf2::kQuit));
        return;
    }
    int32_t rel;
    std::memcpy(&rel, at + 2, 4);
    g_quit_flag = reinterpret_cast<int32_t*>(at + 10 + rel);
    auto* stub = static_cast<uint8_t*>(jump_stub(img, reinterpret_cast<const void*>(&quit_action)));
    if (!stub) {
        log("[beenden] kein Platz für den Sprung, Quit nicht umgelenkt\n");
        return;
    }
    uint8_t jmp[5] = {0xE9};
    const int32_t nrel = int32_t(int64_t(stub) - int64_t(at + 5));
    std::memcpy(jmp + 1, &nrel, 4);
    DWORD old;
    VirtualProtect(at, sizeof jmp, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, jmp, sizeof jmp);
    VirtualProtect(at, sizeof jmp, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof jmp);
}

}  // namespace host
