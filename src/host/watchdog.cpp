#include "watchdog.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "hf2.h"
#include "log.h"

namespace host {

namespace {

HANDLE g_game = nullptr;               // Spiel-Thread (dupliziert)
const volatile uint32_t* g_ticks = nullptr;
DWORD g_limit_ms = 2000;

// TS2-Code: Rücksprungadressen auf dem Stapel liegen hier
bool in_ts2(uint64_t a) { return a >= hf2::kWatchCodeLo && a < hf2::kWatchCodeHi; }

struct Sample {
    CONTEXT ctx;
    uint64_t stack[1024];
    size_t n;
};

// Spiel-Thread anhalten, Register und Stapelanfang kopieren, weiterlaufen
// lassen. Im angehaltenen Zustand nichts anfordern (Heap-Sperre).
bool take(Sample* s, bool with_stack) {
    if (SuspendThread(g_game) == DWORD(-1)) return false;
    std::memset(&s->ctx, 0, sizeof s->ctx);
    s->ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    const bool ok = GetThreadContext(g_game, &s->ctx) != 0;
    s->n = 0;
    if (ok && with_stack) {
        MEMORY_BASIC_INFORMATION mi{};
        if (VirtualQuery(reinterpret_cast<void*>(s->ctx.Rsp), &mi, sizeof mi)) {
            const uint64_t end = reinterpret_cast<uint64_t>(mi.BaseAddress) + mi.RegionSize;
            s->n = std::min<size_t>(1024, (end - s->ctx.Rsp) / 8);
            std::memcpy(s->stack, reinterpret_cast<void*>(s->ctx.Rsp), s->n * 8);
        }
    }
    ResumeThread(g_game);
    return ok;
}

void report(DWORD stalled_ms, uint32_t ticks) {
    static Sample s;
    if (!take(&s, true)) {
        log("[wachhund] Spiel-Thread nicht lesbar (%lu)\n", GetLastError());
        return;
    }
    const CONTEXT& c = s.ctx;
    log("[wachhund] Takt %u steht seit %lu ms: RIP %llX RSP %llX\n", ticks, stalled_ms,
        (unsigned long long)c.Rip, (unsigned long long)c.Rsp);
    log("[wachhund]   rax %llX rbx %llX rcx %llX rdx %llX rsi %llX rdi %llX rbp %llX\n",
        (unsigned long long)c.Rax, (unsigned long long)c.Rbx, (unsigned long long)c.Rcx,
        (unsigned long long)c.Rdx, (unsigned long long)c.Rsi, (unsigned long long)c.Rdi,
        (unsigned long long)c.Rbp);
    log("[wachhund]   r8 %llX r9 %llX r10 %llX r11 %llX r12 %llX r13 %llX r14 %llX r15 %llX\n",
        (unsigned long long)c.R8, (unsigned long long)c.R9, (unsigned long long)c.R10,
        (unsigned long long)c.R11, (unsigned long long)c.R12, (unsigned long long)c.R13,
        (unsigned long long)c.R14, (unsigned long long)c.R15);
    // TS2 hat keine Abwicklungsdaten: Stapel roh nach Adressen im TS2-Code
    // durchsuchen (mögliche Rücksprungadressen, innerste zuerst)
    std::string chain;
    char buf[40];
    int found = 0;
    for (size_t i = 0; i < s.n && found < 24; ++i)
        if (in_ts2(s.stack[i])) {
            std::snprintf(buf, sizeof buf, " [%zu]%llX", i, (unsigned long long)s.stack[i]);
            chain += buf;
            ++found;
        }
    log("[wachhund]   Stapel:%s\n", chain.empty() ? " (nichts im TS2-Code)" : chain.c_str());
    // 200 Stichproben im Abstand von 1 ms: welche Befehle laufen (Schleife)
    struct Hit {
        uint64_t rip;
        int n;
    } hits[64];
    int nh = 0, total = 0;
    static Sample q;
    for (int k = 0; k < 200; ++k) {
        if (!take(&q, false)) break;
        ++total;
        int j = 0;
        while (j < nh && hits[j].rip != q.ctx.Rip) ++j;
        if (j < nh) ++hits[j].n;
        else if (nh < 64) hits[nh++] = {q.ctx.Rip, 1};
        Sleep(1);
    }
    std::sort(hits, hits + nh, [](const Hit& a, const Hit& b) { return a.rip < b.rip; });
    std::string list;
    // Adressen in ts2.exe als exe+Versatz (build-*/ts2.map)
    const uint64_t self = uint64_t(GetModuleHandleW(nullptr));
    for (int j = 0; j < nh; ++j) {
        if (hits[j].rip >= self && hits[j].rip < self + 0x400000)
            std::snprintf(buf, sizeof buf, " exe+%llX:%d", (unsigned long long)(hits[j].rip - self), hits[j].n);
        else
            std::snprintf(buf, sizeof buf, " %llX:%d", (unsigned long long)hits[j].rip, hits[j].n);
        list += buf;
    }
    log("[wachhund]   %d Stichproben, %d Adressen:%s\n", total, nh, list.c_str());
}

DWORD WINAPI watch(void*) {
    uint32_t last = *g_ticks;
    ULONGLONG since = GetTickCount64(), reported = 0;
    int reports = 0;
    for (;;) {
        Sleep(100);
        const uint32_t now = *g_ticks;
        const ULONGLONG t = GetTickCount64();
        if (now != last) {
            if (reports) log("[wachhund] Takt läuft wieder (%u, nach %llu ms)\n", now, t - since);
            last = now, since = t, reported = 0, reports = 0;
            continue;
        }
        // stillstehend: erste Meldung nach der Grenze, dann alle 5 s, höchstens 6
        if (t - since >= g_limit_ms && (reported == 0 || t - reported >= 5000) && reports < 6) {
            report(DWORD(t - since), now);
            reported = GetTickCount64();
            ++reports;
        }
    }
}

}  // namespace

void watchdog_install(const Ts2Image& img) {
    wchar_t v[16] = {};
    if (GetEnvironmentVariableW(L"TS2_WACHHUND", v, 16) == 0 || v[0] == L'0') return;
    if (GetEnvironmentVariableW(L"TS2_WACHHUND_MS", v, 16) > 0) g_limit_ms = std::max(200, _wtoi(v));
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
        log("[wachhund] kein Zugriff auf den Spiel-Thread (%lu)\n", GetLastError());
        return;
    }
    g_ticks = img.va<const volatile uint32_t>(hf2::kWatchTickCounter);
    if (HANDLE h = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr)) {
        CloseHandle(h);
        log("[wachhund] an: meldet Stillstand des Taktzählers über %lu ms\n", g_limit_ms);
    }
}

}  // namespace host
