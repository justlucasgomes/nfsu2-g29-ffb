// dllmain.cpp — proxy entry point + ASI loader + cleanup
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PROXY_LOG_OWNER
#include "proxy_log.h"
#include "wheel_input.h"
#include "force_feedback.h"
#include "telemetry.h"

// ---------------------------------------------------------------------------

static void GetGameDir(char* out, DWORD size) {
    GetModuleFileNameA(nullptr, out, size);
    char* s = strrchr(out, '\\'); if (s) s[1] = '\0';
}

static void LoadChainDll() {
    char dir[MAX_PATH] = {};
    GetGameDir(dir, MAX_PATH);
    char path[MAX_PATH];
    wsprintfA(path, "%sdinput8_chain.dll", dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    if (LoadLibraryA(path)) ProxyLog("[g29_ffb] chain: dinput8_chain.dll carregado");
    else ProxyLogFmt("[g29_ffb] chain: FALHOU dinput8_chain.dll (err=%u)", GetLastError());
}

static void LoadASIMods() {
    char dir[MAX_PATH] = {};
    GetGameDir(dir, MAX_PATH);
    char mask[MAX_PATH];
    wsprintfA(mask, "%sscripts\\*.asi", dir);
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) { ProxyLog("[g29_ffb] ASI: scripts\\ vazio"); return; }
    do {
        char path[MAX_PATH];
        wsprintfA(path, "%sscripts\\%s", dir, fd.cFileName);
        if (LoadLibraryA(path))
            ProxyLogFmt("[g29_ffb] ASI: carregado   %s", fd.cFileName);
        else
            ProxyLogFmt("[g29_ffb] ASI: FALHOU      %s (err=%u)", fd.cFileName, GetLastError());
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hMod);
        ProxyLogOpen();
        ProxyLog("[g29_ffb] ====================================================");
        ProxyLog("[g29_ffb] NFSU2 G29 FFB Mod  v0.6.0");
        ProxyLog("[g29_ffb] proxy dinput8.dll carregado (G29 + ASI loader)");
        LoadChainDll();
        LoadASIMods();
        ProxyLog("[g29_ffb] DllMain OK — aguardando DirectInput8Create");
        break;

    case DLL_PROCESS_DETACH:
        WheelInput::Get().Shutdown();
        ForceFeedback::Get().Shutdown();
        Telemetry::Get().Shutdown();
        ProxyLog("[g29_ffb] DLL descarregada");
        if (g_proxy_log) { fclose(g_proxy_log); g_proxy_log = nullptr; }
        break;
    }
    return TRUE;
}
