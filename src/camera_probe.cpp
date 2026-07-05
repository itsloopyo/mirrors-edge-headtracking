#include "camera_probe.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <tlhelp32.h>

#include "pov_layout.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/logging/file_log.h"

namespace meht::camera_probe {
namespace {

namespace log = cameraunlock::logging;
namespace hooks = cameraunlock::hooks;

// ACamera::execSetViewTarget is thiscall: ECX = Camera*, then (FFrame& Stack,
// void* Result) on the stack. Modeled as __fastcall so ECX lands in the first
// parameter; EDX is unused by the callee.
using SetViewTargetFn = void(__fastcall*)(void* thisPtr, void* edx, void* stack, void* result);

SetViewTargetFn g_orig = nullptr;
void* g_target = nullptr;
std::atomic<void*> g_camera{nullptr};

std::atomic<bool> g_dumpRequested{false};
std::atomic<bool> g_watchRequested{false};
std::atomic<bool> g_watchContinuous{false};

std::uintptr_t g_moduleBase = 0;
std::atomic<std::uint32_t> g_gameThreadId{0};
void* g_veh = nullptr;
std::atomic<bool> g_watchArmed{false};

void __fastcall Detour(void* thisPtr, void* edx, void* stack, void* result) {
    if (thisPtr) {
        // Track the latest view target's camera so gameplay dumps read the live
        // player camera, not just the first (menu) one. This runs on the game
        // thread, so record its id for arming the hardware watchpoint there.
        g_camera.store(thisPtr);
        g_gameThreadId.store(GetCurrentThreadId());
    }
    g_orig(thisPtr, edx, stack, result);
}

// True if `addr` lies inside the running exe's image (base .. base+SizeOfImage).
bool InExe(std::uintptr_t addr) {
    if (g_moduleBase == 0 || addr < g_moduleBase) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_moduleBase);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_moduleBase + dos->e_lfanew);
    return addr < g_moduleBase + nt->OptionalHeader.SizeOfImage;
}

// SecuROM anti-debug clears the debug registers, so hardware watchpoints die
// after a couple of hits. Watch the POV rotation via PAGE_GUARD instead: every
// access to the camera object's page faults; we filter to the rotation field,
// log the accessing instruction and its first exe-range caller (deduped by
// caller so the shared memcpy doesn't collapse distinct call sites), then
// single-step past the access and re-arm the guard.
constexpr int kMaxHits = 24;
std::uintptr_t g_seenCaller[kMaxHits];
std::atomic<int> g_hitCount{0};

std::uintptr_t g_guardPage = 0;       // page base of the POV rotation
std::uintptr_t g_rotLo = 0, g_rotHi = 0;  // [lo, hi) = cam+0x4E0 .. cam+0x4E8+4
std::thread g_reguardThread;
std::atomic<bool> g_reguardRun{false};

void ApplyGuard() {
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(g_guardPage), 0x1000,
                   PAGE_READWRITE | PAGE_GUARD, &old);
}

LONG CALLBACK WatchVeh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    CONTEXT* c = ep->ContextRecord;

    constexpr DWORD kGuardPageViolation = 0x80000001u;  // STATUS_GUARD_PAGE_VIOLATION
    if (code == kGuardPageViolation) {
        if (!g_watchArmed.load()) return EXCEPTION_CONTINUE_SEARCH;
        std::uintptr_t accessed = ep->ExceptionRecord->ExceptionInformation[1];
        bool write = ep->ExceptionRecord->ExceptionInformation[0] == 1;

        // The OS has already cleared PAGE_GUARD for this access; a background
        // thread re-applies it (SecuROM blocks the single-step re-arm trick).
        if (accessed >= g_rotLo && accessed < g_rotHi) {
            std::uintptr_t eip = c->Eip;
            auto* sp = reinterpret_cast<std::uintptr_t*>(c->Esp);
            std::uintptr_t caller = 0;
            for (int i = 0; i < 64; ++i) {
                __try {
                    std::uintptr_t v = sp[i];
                    if (InExe(v)) { caller = v; break; }
                } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            }
            std::uintptr_t key = caller ? caller : eip;
            int n = g_hitCount.load();
            bool seen = false;
            for (int i = 0; i < n; ++i) if (g_seenCaller[i] == key) { seen = true; break; }
            if (!seen && n < kMaxHits) {
                g_seenCaller[n] = key;
                g_hitCount.store(n + 1);
                auto camAddr = reinterpret_cast<std::uintptr_t>(g_camera.load());
                log::Line("[watch] rot %s EIP %s0x%X caller rva=0x%X (accessed cam+0x%X)",
                          write ? "WRITE" : "READ ",
                          InExe(eip) ? "rva=" : "extern@",
                          InExe(eip) ? (unsigned)(eip - g_moduleBase) : (unsigned)eip,
                          caller ? (unsigned)(caller - g_moduleBase) : 0u,
                          (unsigned)(accessed - camAddr));
                int logged = 0;
                for (int i = 0; i < 64 && logged < 4; ++i) {
                    __try {
                        std::uintptr_t v = sp[i];
                        if (InExe(v)) {
                            log::Line("        caller [esp+0x%02X] rva=0x%X", i * 4,
                                      (unsigned)(v - g_moduleBase));
                            ++logged;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
                }
            }
            if (g_hitCount.load() >= kMaxHits) {
                g_watchArmed.store(false);
                g_watchContinuous.store(false);
                log::Line("[watch] captured %d distinct callers; disarming", g_hitCount.load());
            }
        }
        (void)c;
        // Access completes with the guard cleared; the re-guard thread restores it.
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Busy-re-applies PAGE_GUARD so the watch survives without single-stepping. Each
// game access clears the guard; this restores it fast enough to catch the next.
void ReguardLoop() {
    // Highest priority so the guard is re-applied as fast as possible after each
    // fault clears it - maximizes the fraction of time the guard is up, which is
    // the capture's catch rate.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (g_reguardRun.load()) {
        if (g_watchArmed.load()) {
            ApplyGuard();
        } else {
            // Capture finished (or never armed): stop pegging a core at
            // time-critical priority while there is nothing to re-guard.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // Leave the page unguarded on exit.
    DWORD old = 0;
    if (g_guardPage) VirtualProtect(reinterpret_cast<void*>(g_guardPage), 0x1000, PAGE_READWRITE, &old);
}

// Arms the page-guard watch on the camera page for the rotation field at `addr`
// (cam+0x4E4). Every access to that 4 KB page faults; the VEH filters to the
// rotation triple and self-re-arms, so it survives across frames (unlike DR).
bool ArmWatch(std::uintptr_t addr, bool quiet = false) {
    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, WatchVeh);
    g_guardPage = addr & ~static_cast<std::uintptr_t>(0xFFF);
    g_rotLo = addr - 4;   // cam+0x4E0
    g_rotHi = addr + 8;   // cam+0x4E8 + 4
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(g_guardPage), 0x1000,
                        PAGE_READWRITE | PAGE_GUARD, &old)) {
        if (!quiet) log::Line("[watch] VirtualProtect(PAGE_GUARD) failed: %lu", GetLastError());
        return false;
    }
    g_watchArmed.store(true);
    if (!g_reguardRun.exchange(true)) {
        g_reguardThread = std::thread(ReguardLoop);
    }
    if (!quiet)
        log::Line("[watch] page-guard armed on page %p (rot %p..%p); play to trigger",
                  reinterpret_cast<void*>(g_guardPage),
                  reinterpret_cast<void*>(g_rotLo), reinterpret_cast<void*>(g_rotHi));
    return true;
}

bool LooksLikeWorldFloat(float f) {
    if (f != f) return false;  // NaN
    float a = f < 0 ? -f : f;
    return a == 0.0f || (a > 0.01f && a < 1.0e7f);
}

}  // namespace

bool Install(std::uintptr_t moduleBase, std::uintptr_t setViewTargetRVA) {
    if (setViewTargetRVA == 0) {
        log::Line("[probe] no SetViewTargetRVA in profile; camera probe disabled");
        return false;
    }
    if (hooks::HookManager::Instance().Initialize() != hooks::HookStatus::Ok) {
        log::Line("[probe] MinHook init failed");
        return false;
    }
    g_moduleBase = moduleBase;
    g_target = reinterpret_cast<void*>(moduleBase + setViewTargetRVA);
    hooks::HookStatus s = hooks::HookManager::Instance().CreateHook(
        g_target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_orig));
    if (s != hooks::HookStatus::Ok) {
        log::Line("[probe] CreateHook(SetViewTarget @ %p) failed: %s",
                  g_target, hooks::HookStatusToString(s));
        return false;
    }
    s = hooks::HookManager::Instance().EnableHook(g_target);
    if (s != hooks::HookStatus::Ok) {
        log::Line("[probe] EnableHook failed: %s", hooks::HookStatusToString(s));
        return false;
    }
    log::Line("[probe] hooked SetViewTarget @ %p, waiting for a Camera...", g_target);
    return true;
}

void Shutdown() {
    // Stop the re-guard worker and tear the page guard down before the module
    // unloads, so a later fault can't reach an unloaded VEH. Only meaningful once
    // the watch was armed; otherwise the thread was never started and g_veh is null.
    g_watchArmed.store(false);
    if (g_reguardRun.exchange(false) && g_reguardThread.joinable()) {
        g_reguardThread.join();
    }
    if (g_veh) {
        RemoveVectoredExceptionHandler(g_veh);
        g_veh = nullptr;
    }
}

void RequestDump() { g_dumpRequested.store(true); }

void RequestWatch() { g_watchRequested.store(true); }

void* GetCamera() { return g_camera.load(); }

constexpr int kScan = 0x800;

// Snapshot of the previous dump, so each dump can print only what CHANGED since
// the last one. Isolating the view-rotation fields is a diff problem: turn the
// in-game view between two dumps and the rotation ints are the words that move.
std::uint32_t g_prev[kScan / 4];
bool g_havePrev = false;

// Reads kScan bytes of the camera object into out[], SEH-guarded. Returns the
// number of dwords successfully read (stops at the first faulting page).
int ReadSnapshot(unsigned char* base, std::uint32_t* out) {
    int words = 0;
    for (; words < kScan / 4; ++words) {
        __try {
            out[words] = *reinterpret_cast<std::uint32_t*>(base + words * 4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
    return words;
}

void FormatCell(char* cell, std::uint32_t v) {
    float f = *reinterpret_cast<float*>(&v);
    if (LooksLikeWorldFloat(f) && f != 0.0f) {
        wsprintfA(cell, "%08X(f=%d.%03d)", v, (int)f,
                  (int)((f < 0 ? -f : f) * 1000) % 1000);
    } else {
        int asRot = static_cast<int>(static_cast<std::int16_t>(v & 0xFFFF));
        wsprintfA(cell, "%08X(r=%d)", v, asRot);
    }
}

void Poll() {
    static bool s_autoDumped = false;
    static int s_settle = 0;

    void* cam = g_camera.load();
    if (!cam) return;

    if (g_watchRequested.exchange(false)) {
        g_hitCount.store(0);
        g_watchContinuous.store(true);
        ArmWatch(reinterpret_cast<std::uintptr_t>(cam) + pov::kYawOffset);
    }

    // Auto-dump once after the camera settles, then dump again on every hotkey
    // request (so the user can dump while looking in known directions in-game).
    bool wantDump = false;
    if (!s_autoDumped) {
        if (++s_settle >= 120) { s_autoDumped = true; wantDump = true; }
    }
    if (g_dumpRequested.exchange(false)) wantDump = true;
    if (!wantDump) return;

    auto* base = reinterpret_cast<unsigned char*>(cam);
    static std::uint32_t cur[kScan / 4];
    int words = ReadSnapshot(base, cur);

    log::Line("[probe] Camera @ %p - snapshot 0x000..0x%03X (%d words):", cam, words * 4, words);
    for (int off = 0; off < words * 4; off += 16) {
        char line[200];
        wsprintfA(line, "  +0x%03X:", off);
        for (int j = 0; j < 16 && (off + j) < words * 4; j += 4) {
            char cell[40];
            FormatCell(cell, cur[(off + j) / 4]);
            lstrcatA(line, " ");
            lstrcatA(line, cell);
        }
        log::Line("%s", line);
    }

    // Dump the camera's vtable (cam+0x000 -> vtable ptr): its virtual methods
    // operate on this=camera, so a method that reads cam+0x4E0 is unambiguously a
    // camera viewpoint accessor (the render's POV read). Map these RVAs in Ghidra.
    if (words > 0) {
        auto vtbl = reinterpret_cast<std::uint32_t*>(cur[0]);
        log::Line("[probe] camera vtable @ 0x%08X entries (rva):", cur[0]);
        char line[220];
        line[0] = 0;
        int shown = 0;
        for (int i = 0; i < 128; ++i) {
            __try {
                std::uint32_t fn = vtbl[i];
                if (!InExe(fn)) continue;
                char cell[32];
                wsprintfA(cell, " [%d]=0x%X", i, (unsigned)(fn - g_moduleBase));
                lstrcatA(line, cell);
                if (++shown % 6 == 0) { log::Line("   %s", line); line[0] = 0; }
            } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        }
        if (line[0]) log::Line("   %s", line);
    }

    if (g_havePrev) {
        log::Line("[probe] CHANGED since previous dump (offset: old -> new):");
        int changes = 0;
        for (int i = 0; i < words; ++i) {
            if (cur[i] != g_prev[i]) {
                char oldC[40], newC[40];
                FormatCell(oldC, g_prev[i]);
                FormatCell(newC, cur[i]);
                log::Line("    +0x%03X: %s -> %s", i * 4, oldC, newC);
                ++changes;
            }
        }
        if (changes == 0) log::Line("    (nothing changed - view was identical)");
    } else {
        log::Line("[probe] (baseline dump - turn the in-game view, then dump again for a diff)");
    }

    for (int i = 0; i < words; ++i) g_prev[i] = cur[i];
    g_havePrev = true;

    log::Line("[probe] dump complete. Rotation fields = the [-32768,32767] rotator "
              "ints that move when the view turns.");
}

}  // namespace meht::camera_probe
