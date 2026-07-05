#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "mod.h"

// Bootstrap on a dedicated thread: DllMain runs under the loader lock, so the
// mod's setup (threads, sockets, file IO) must not happen inline here.
static DWORD WINAPI BootstrapThread(LPVOID) {
    meht::Start();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(module);
            HANDLE t = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
            break;
        }
        case DLL_PROCESS_DETACH:
            // reserved != NULL means the process is terminating: the OS has already
            // killed every other thread, so joining our worker threads (Stop() does)
            // under the loader lock can hang, and touching torn-down runtime state
            // can crash on exit. Only run orderly shutdown on a real FreeLibrary
            // unload (reserved == NULL).
            if (reserved == nullptr) meht::Stop();
            break;
    }
    return TRUE;
}
