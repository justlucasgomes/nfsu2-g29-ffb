// winmm_proxy.cpp
// WinMM proxy DLL for NFSU2.
//
// NFSU2's login/profile-selection menu reads joystick state through the legacy
// WinMM joyGetPosEx() API rather than DirectInput.  The Logitech G29 pedal axes
// are hardware-inverted: at rest they sit at 65535, which NFSU2 interprets as
// "maximum axis positive" → menu scrolls DOWN before the player touches anything.
//
// This proxy intercepts joyGetPosEx() and applies the same activation-latch
// logic used in dinput8.dll:
//
//   • All axes idle for ≥3 s  → s_activated = false → pedal axes neutralized
//   • Any real input detected  → s_activated = true  → raw values pass through
//
// All other WinMM functions are forwarded transparently to System32\winmm.dll.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

// All proxy stubs redefine SDK-imported functions — suppress the inconsistent
// dllimport linkage warnings that fire on every stub.
#pragma warning(disable: 4273)

// ── Real DLL ──────────────────────────────────────────────────────────────────

static HMODULE g_hReal = nullptr;

static void* GetReal(const char* name) {
    if (!g_hReal) {
        char sys[MAX_PATH] = {};
        GetSystemDirectoryA(sys, MAX_PATH);
        char path[MAX_PATH];
        wsprintfA(path, "%s\\winmm.dll", sys);
        g_hReal = LoadLibraryA(path);
    }
    return g_hReal ? (void*)GetProcAddress(g_hReal, name) : nullptr;
}

// ── Logging ───────────────────────────────────────────────────────────────────

static FILE* g_log       = nullptr;
static bool  g_logOpened = false;

static void WmmLog(const char* fmt, ...) {
    if (!g_logOpened) {
        g_logOpened = true;
        char dir[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, dir, MAX_PATH);
        char* sl = strrchr(dir, '\\'); if (sl) sl[1] = '\0';
        char logDir[MAX_PATH], logPath[MAX_PATH];
        wsprintfA(logDir,  "%slogs",                  dir);
        wsprintfA(logPath, "%slogs\\winmm_proxy.log", dir);
        CreateDirectoryA(logDir, nullptr);
        g_log = fopen(logPath, "w");
        if (g_log) fprintf(g_log, "[winmm] proxy loaded\n"), fflush(g_log);
    }
    if (!g_log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    fprintf(g_log, "%s\n", buf);
    fflush(g_log);
}

// ── Input activation latch ────────────────────────────────────────────────────
// Independent state from dinput_proxy — separate DLL, separate statics.
//
// Thresholds (WinMM axes are DWORD 0–65535):
//   Steering idle — within 1200 of center (32767)
//   Pedal idle    — within 1500 of full-release (65535)   ← G29 rest position
//
// IDLE_RESET_FRAMES at ~100 Hz WinMM polling ≈ 3 seconds.

static bool s_activated  = false;
static int  s_idleFrames = 0;
static constexpr int IDLE_RESET_FRAMES = 300;

// ── joyGetPosEx — intercepted ─────────────────────────────────────────────────

extern "C" MMRESULT WINAPI joyGetPosEx(UINT uJoyID, LPJOYINFOEX pji) {
    typedef MMRESULT (WINAPI* PFN)(UINT, LPJOYINFOEX);
    static PFN real = nullptr;
    if (!real) real = (PFN)GetReal("joyGetPosEx");
    if (!real) return JOYERR_NOCANDO;

    MMRESULT hr = real(uJoyID, pji);
    if (hr != JOYERR_NOERROR || !pji) return hr;

    const DWORD f = pji->dwFlags;

    // Extract current axis values (default to idle if axis not requested).
    DWORD xPos = (f & JOY_RETURNX) ? pji->dwXpos : 32767u;
    DWORD yPos = (f & JOY_RETURNY) ? pji->dwYpos : 65535u;
    DWORD zPos = (f & JOY_RETURNZ) ? pji->dwZpos : 65535u;
    DWORD rPos = (f & JOY_RETURNR) ? pji->dwRpos : 65535u;
    DWORD uPos = (f & JOY_RETURNU) ? pji->dwUpos : 65535u;
    DWORD vPos = (f & JOY_RETURNV) ? pji->dwVpos : 65535u;

    // Log the first call so we can verify axis values in the log.
    static bool s_firstCall = true;
    if (s_firstCall) {
        s_firstCall = false;
        WmmLog("[winmm] joyGetPosEx first call joy=%u  X=%u Y=%u Z=%u R=%u U=%u V=%u  flags=0x%08X",
               uJoyID, xPos, yPos, zPos, rPos, uPos, vPos, f);
    }

    // Steering idle: within ±1200 of center (32767).
    DWORD dx = (xPos >= 32767u) ? (xPos - 32767u) : (32767u - xPos);
    bool steeringIdle = (dx < 1200u);

    // Pedal idle: within 1500 of 65535 (not pressed).
    bool pedalsIdle = ((65535u - yPos) < 1500u) &&
                      ((65535u - zPos) < 1500u) &&
                      ((65535u - rPos) < 1500u) &&
                      ((65535u - uPos) < 1500u) &&
                      ((65535u - vPos) < 1500u);

    if (!steeringIdle || !pedalsIdle) {
        if (!s_activated)
            WmmLog("[winmm] activated  X=%u Y=%u Z=%u R=%u", xPos, yPos, zPos, rPos);
        s_idleFrames = 0;
        s_activated  = true;
    } else {
        if (++s_idleFrames >= IDLE_RESET_FRAMES) {
            if (s_activated)
                WmmLog("[winmm] idle reset X=%u Y=%u Z=%u R=%u", xPos, yPos, zPos, rPos);
            s_activated = false;
        }
    }

    // While not activated: clamp pedal axes to center so menus see no input.
    if (!s_activated) {
        if (f & JOY_RETURNY) pji->dwYpos = 32767u;
        if (f & JOY_RETURNZ) pji->dwZpos = 32767u;
        if (f & JOY_RETURNR) pji->dwRpos = 32767u;
        if (f & JOY_RETURNU) pji->dwUpos = 32767u;
        if (f & JOY_RETURNV) pji->dwVpos = 32767u;
    }

    return hr;
}

// ── Forwarding stubs ──────────────────────────────────────────────────────────
// Every other WinMM function is forwarded transparently to the real DLL.
// The macro resolves the real function pointer on first call.

#define FWD_MM(name, params, args) \
    extern "C" MMRESULT WINAPI name params { \
        typedef MMRESULT (WINAPI* FN) params; \
        static FN fn = nullptr; \
        if (!fn) fn = (FN)GetReal(#name); \
        return fn ? fn args : MMSYSERR_NODRIVER; \
    }

#define FWD_UINT(name, params, args) \
    extern "C" UINT WINAPI name params { \
        typedef UINT (WINAPI* FN) params; \
        static FN fn = nullptr; \
        if (!fn) fn = (FN)GetReal(#name); \
        return fn ? fn args : 0u; \
    }

#define FWD_DWORD(name, params, args) \
    extern "C" DWORD WINAPI name params { \
        typedef DWORD (WINAPI* FN) params; \
        static FN fn = nullptr; \
        if (!fn) fn = (FN)GetReal(#name); \
        return fn ? fn args : 0u; \
    }

#define FWD_LONG(name, params, args) \
    extern "C" LONG WINAPI name params { \
        typedef LONG (WINAPI* FN) params; \
        static FN fn = nullptr; \
        if (!fn) fn = (FN)GetReal(#name); \
        return fn ? fn args : -1L; \
    }

#define FWD_BOOL(name, params, args) \
    extern "C" BOOL WINAPI name params { \
        typedef BOOL (WINAPI* FN) params; \
        static FN fn = nullptr; \
        if (!fn) fn = (FN)GetReal(#name); \
        return fn ? fn args : FALSE; \
    }

#define FWD_VOID(name, params, args) \
    extern "C" void WINAPI name params { \
        typedef void (WINAPI* FN) params; \
        static FN fn = nullptr; \
        if (!fn) fn = (FN)GetReal(#name); \
        if (fn) fn args; \
    }

// ── joyGetPos — intercepted (older API used by NFSU2 menus) ──────────────────
// JOYINFO has only dwXpos (steering), dwYpos (throttle), dwZpos (brake).
// G29 pedals rest at 65535 — same inversion problem as joyGetPosEx.

extern "C" MMRESULT WINAPI joyGetPos(UINT uJoyID, LPJOYINFO pji) {
    typedef MMRESULT (WINAPI* PFN)(UINT, LPJOYINFO);
    static PFN real = nullptr;
    if (!real) real = (PFN)GetReal("joyGetPos");
    if (!real) return JOYERR_NOCANDO;

    MMRESULT hr = real(uJoyID, pji);
    if (hr != JOYERR_NOERROR || !pji) return hr;

    UINT xPos = pji->wXpos;
    UINT yPos = pji->wYpos;
    UINT zPos = pji->wZpos;

    static bool s_firstCallPos = true;
    if (s_firstCallPos) {
        s_firstCallPos = false;
        WmmLog("[winmm] joyGetPos first call joy=%u  X=%u Y=%u Z=%u",
               uJoyID, xPos, yPos, zPos);
    }

    UINT dx = (xPos >= 32767u) ? (xPos - 32767u) : (32767u - xPos);
    bool steeringIdle = (dx < 1200u);
    bool pedalsIdle   = ((65535u - yPos) < 1500u) &&
                        ((65535u - zPos) < 1500u);

    if (!steeringIdle || !pedalsIdle) {
        if (!s_activated)
            WmmLog("[winmm] activated (joyGetPos)  X=%u Y=%u Z=%u", xPos, yPos, zPos);
        s_idleFrames = 0;
        s_activated  = true;
    } else {
        if (++s_idleFrames >= IDLE_RESET_FRAMES) {
            if (s_activated)
                WmmLog("[winmm] idle reset (joyGetPos)  X=%u Y=%u Z=%u", xPos, yPos, zPos);
            s_activated = false;
        }
    }

    if (!s_activated) {
        pji->wYpos = 32767u;
        pji->wZpos = 32767u;
    }

    return hr;
}

// ── Joystick (non-intercepted) ────────────────────────────────────────────────
FWD_MM  (joyGetDevCapsA,   (UINT_PTR id, LPJOYCAPSA pjc, UINT cbjc), (id, pjc, cbjc))
FWD_MM  (joyGetDevCapsW,   (UINT_PTR id, LPJOYCAPSW pjc, UINT cbjc), (id, pjc, cbjc))
FWD_UINT(joyGetNumDevs,    (void), ())
FWD_MM  (joyGetThreshold,  (UINT id, LPUINT put), (id, put))
FWD_MM  (joySetThreshold,  (UINT id, UINT ut), (id, ut))
FWD_MM  (joySetCapture,    (HWND hwnd, UINT id, UINT ms, BOOL changed), (hwnd, id, ms, changed))
FWD_MM  (joyReleaseCapture,(UINT id), (id))
FWD_MM  (joyConfigChanged, (DWORD flags), (flags))

// ── Multimedia timer ──────────────────────────────────────────────────────────
FWD_DWORD(timeGetTime,       (void), ())
FWD_MM   (timeBeginPeriod,   (UINT ms), (ms))
FWD_MM   (timeEndPeriod,     (UINT ms), (ms))
FWD_MM   (timeGetDevCaps,    (LPTIMECAPS ptc, UINT cb), (ptc, cb))
FWD_MM   (timeGetSystemTime, (LPMMTIME pmmt, UINT cb), (pmmt, cb))

// timeSetEvent and timeKillEvent have non-MMRESULT/DWORD returns in older SDKs
extern "C" MMRESULT WINAPI timeSetEvent(UINT ms, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT flags) {
    typedef MMRESULT (WINAPI* FN)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT);
    static FN fn = nullptr;
    if (!fn) fn = (FN)GetReal("timeSetEvent");
    return fn ? fn(ms, res, cb, user, flags) : (MMRESULT)TIMERR_NOCANDO;
}
FWD_MM(timeKillEvent, (UINT id), (id))

// ── Sound ─────────────────────────────────────────────────────────────────────
FWD_BOOL(PlaySoundA,    (LPCSTR psz, HMODULE hmod, DWORD fdw), (psz, hmod, fdw))
FWD_BOOL(PlaySoundW,    (LPCWSTR psz, HMODULE hmod, DWORD fdw), (psz, hmod, fdw))
FWD_BOOL(sndPlaySoundA, (LPCSTR psz, UINT fdw), (psz, fdw))
FWD_BOOL(sndPlaySoundW, (LPCWSTR psz, UINT fdw), (psz, fdw))

// ── Wave out ──────────────────────────────────────────────────────────────────
FWD_UINT(waveOutGetNumDevs,     (void), ())
FWD_MM  (waveOutGetDevCapsA,    (UINT_PTR id, LPWAVEOUTCAPSA pwoc, UINT cb), (id, pwoc, cb))
FWD_MM  (waveOutGetDevCapsW,    (UINT_PTR id, LPWAVEOUTCAPSW pwoc, UINT cb), (id, pwoc, cb))
FWD_MM  (waveOutOpen,           (LPHWAVEOUT phwo, UINT id, LPCWAVEFORMATEX pwfx, DWORD_PTR cb, DWORD_PTR inst, DWORD fdw), (phwo, id, pwfx, cb, inst, fdw))
FWD_MM  (waveOutClose,          (HWAVEOUT hwo), (hwo))
FWD_MM  (waveOutPrepareHeader,  (HWAVEOUT hwo, LPWAVEHDR pwh, UINT cb), (hwo, pwh, cb))
FWD_MM  (waveOutUnprepareHeader,(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cb), (hwo, pwh, cb))
FWD_MM  (waveOutWrite,          (HWAVEOUT hwo, LPWAVEHDR pwh, UINT cb), (hwo, pwh, cb))
FWD_MM  (waveOutPause,          (HWAVEOUT hwo), (hwo))
FWD_MM  (waveOutRestart,        (HWAVEOUT hwo), (hwo))
FWD_MM  (waveOutReset,          (HWAVEOUT hwo), (hwo))
FWD_MM  (waveOutBreakLoop,      (HWAVEOUT hwo), (hwo))
FWD_MM  (waveOutGetPosition,    (HWAVEOUT hwo, LPMMTIME pmmt, UINT cb), (hwo, pmmt, cb))
FWD_MM  (waveOutGetVolume,      (HWAVEOUT hwo, LPDWORD pdwVol), (hwo, pdwVol))
FWD_MM  (waveOutSetVolume,      (HWAVEOUT hwo, DWORD dwVol), (hwo, dwVol))
FWD_MM  (waveOutGetID,          (HWAVEOUT hwo, LPUINT puid), (hwo, puid))
FWD_MM  (waveOutMessage,        (HWAVEOUT hwo, UINT msg, DWORD_PTR dw1, DWORD_PTR dw2), (hwo, msg, dw1, dw2))

// ── Wave in ───────────────────────────────────────────────────────────────────
FWD_UINT(waveInGetNumDevs,     (void), ())
FWD_MM  (waveInGetDevCapsA,    (UINT_PTR id, LPWAVEINCAPSA pwic, UINT cb), (id, pwic, cb))
FWD_MM  (waveInOpen,           (LPHWAVEIN phwi, UINT id, LPCWAVEFORMATEX pwfx, DWORD_PTR cb, DWORD_PTR inst, DWORD fdw), (phwi, id, pwfx, cb, inst, fdw))
FWD_MM  (waveInClose,          (HWAVEIN hwi), (hwi))
FWD_MM  (waveInPrepareHeader,  (HWAVEIN hwi, LPWAVEHDR pwh, UINT cb), (hwi, pwh, cb))
FWD_MM  (waveInUnprepareHeader,(HWAVEIN hwi, LPWAVEHDR pwh, UINT cb), (hwi, pwh, cb))
FWD_MM  (waveInAddBuffer,      (HWAVEIN hwi, LPWAVEHDR pwh, UINT cb), (hwi, pwh, cb))
FWD_MM  (waveInStart,          (HWAVEIN hwi), (hwi))
FWD_MM  (waveInStop,           (HWAVEIN hwi), (hwi))
FWD_MM  (waveInReset,          (HWAVEIN hwi), (hwi))
FWD_MM  (waveInGetPosition,    (HWAVEIN hwi, LPMMTIME pmmt, UINT cb), (hwi, pmmt, cb))
FWD_MM  (waveInGetID,          (HWAVEIN hwi, LPUINT puid), (hwi, puid))
FWD_MM  (waveInMessage,        (HWAVEIN hwi, UINT msg, DWORD_PTR dw1, DWORD_PTR dw2), (hwi, msg, dw1, dw2))

// ── Auxiliary audio ───────────────────────────────────────────────────────────
FWD_UINT(auxGetNumDevs,  (void), ())
FWD_MM  (auxGetDevCapsA, (UINT_PTR id, LPAUXCAPSA pac, UINT cb), (id, pac, cb))
FWD_MM  (auxGetVolume,   (UINT id, LPDWORD pdwVol), (id, pdwVol))
FWD_MM  (auxSetVolume,   (UINT id, DWORD dwVol), (id, dwVol))
FWD_MM  (auxOutMessage,  (UINT id, UINT msg, DWORD_PTR dw1, DWORD_PTR dw2), (id, msg, dw1, dw2))

// ── Mixer ─────────────────────────────────────────────────────────────────────
FWD_UINT(mixerGetNumDevs,        (void), ())
FWD_MM  (mixerGetDevCapsA,       (UINT_PTR id, LPMIXERCAPSA pmc, UINT cb), (id, pmc, cb))
FWD_MM  (mixerOpen,              (LPHMIXER phmx, UINT id, DWORD_PTR cb, DWORD_PTR inst, DWORD fdw), (phmx, id, cb, inst, fdw))
FWD_MM  (mixerClose,             (HMIXER hmx), (hmx))
FWD_MM  (mixerGetLineInfoA,      (HMIXEROBJ hmxobj, LPMIXERLINEA pml, DWORD fdw), (hmxobj, pml, fdw))
FWD_MM  (mixerGetLineControlsA,  (HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmlc, DWORD fdw), (hmxobj, pmlc, fdw))
FWD_MM  (mixerGetControlDetailsA,(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmcd, DWORD fdw), (hmxobj, pmcd, fdw))
FWD_MM  (mixerSetControlDetails, (HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmcd, DWORD fdw), (hmxobj, pmcd, fdw))
FWD_MM  (mixerGetID,             (HMIXEROBJ hmxobj, UINT* puid, DWORD fdw), (hmxobj, puid, fdw))
FWD_DWORD(mixerMessage,          (HMIXER hmx, UINT msg, DWORD_PTR dw1, DWORD_PTR dw2), (hmx, msg, dw1, dw2))

// ── MMIO ──────────────────────────────────────────────────────────────────────
extern "C" HMMIO WINAPI mmioOpenA(LPSTR sz, LPMMIOINFO pmmioinfo, DWORD fdwOpen) {
    typedef HMMIO (WINAPI* FN)(LPSTR, LPMMIOINFO, DWORD);
    static FN fn = nullptr;
    if (!fn) fn = (FN)GetReal("mmioOpenA");
    return fn ? fn(sz, pmmioinfo, fdwOpen) : nullptr;
}
extern "C" HMMIO WINAPI mmioOpenW(LPWSTR sz, LPMMIOINFO pmmioinfo, DWORD fdwOpen) {
    typedef HMMIO (WINAPI* FN)(LPWSTR, LPMMIOINFO, DWORD);
    static FN fn = nullptr;
    if (!fn) fn = (FN)GetReal("mmioOpenW");
    return fn ? fn(sz, pmmioinfo, fdwOpen) : nullptr;
}
FWD_MM  (mmioClose,        (HMMIO h, UINT flags), (h, flags))
FWD_LONG(mmioRead,         (HMMIO h, HPSTR pch, LONG cb), (h, pch, cb))
FWD_LONG(mmioWrite,        (HMMIO h, const char* pch, LONG cb), (h, pch, cb))
FWD_LONG(mmioSeek,         (HMMIO h, LONG off, int origin), (h, off, origin))
FWD_MM  (mmioAscend,       (HMMIO h, LPMMCKINFO pmmcki, UINT flags), (h, pmmcki, flags))
FWD_MM  (mmioDescend,      (HMMIO h, LPMMCKINFO pmmcki, const MMCKINFO* pmmckiParent, UINT flags), (h, pmmcki, pmmckiParent, flags))
FWD_MM  (mmioCreateChunk,  (HMMIO h, LPMMCKINFO pmmcki, UINT flags), (h, pmmcki, flags))
FWD_MM  (mmioFlush,        (HMMIO h, UINT flags), (h, flags))
FWD_MM  (mmioGetInfo,      (HMMIO h, LPMMIOINFO pmmioinfo, UINT flags), (h, pmmioinfo, flags))
FWD_MM  (mmioSetInfo,      (HMMIO h, LPCMMIOINFO pmmioinfo, UINT flags), (h, pmmioinfo, flags))

// ── MCI ───────────────────────────────────────────────────────────────────────
FWD_DWORD(mciSendCommandA,   (MCIDEVICEID id, UINT msg, DWORD_PTR dw1, DWORD_PTR dw2), (id, msg, dw1, dw2))
// mciSendStringA is defined inline in SDK 10.0.26100+ so we can't reuse the same name.
// Use an internal alias; winmm.def maps mciSendStringA → wmm_mciSendStringA.
extern "C" DWORD WINAPI wmm_mciSendStringA(LPCSTR cmd, LPSTR ret, UINT cbRet, HANDLE hwnd) {
    typedef DWORD (WINAPI* FN)(LPCSTR, LPSTR, UINT, HANDLE);
    static FN fn = nullptr;
    if (!fn) fn = (FN)GetReal("mciSendStringA");
    return fn ? fn(cmd, ret, cbRet, hwnd) : 0u;
}
FWD_BOOL (mciGetErrorStringA,(DWORD err, LPSTR text, UINT cb), (err, text, cb))
extern "C" MCIDEVICEID WINAPI mciGetDeviceIDA(LPCSTR pszDevice) {
    typedef MCIDEVICEID (WINAPI* FN)(LPCSTR);
    static FN fn = nullptr;
    if (!fn) fn = (FN)GetReal("mciGetDeviceIDA");
    return fn ? fn(pszDevice) : 0;
}

// ── DllMain ───────────────────────────────────────────────────────────────────
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        WmmLog("[winmm] DllMain attach");
    return TRUE;
}
