#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include "logitech_led.h"
#include "logger.h"

// ── Shared state ──────────────────────────────────────────────────────────────

enum class LEDMode { None, DInputEscape, SteeringWheelSDK, LegacyLED };
static LEDMode  g_mode  = LEDMode::None;
static bool     g_ready = false;

// ── Mode 1: DirectInput Escape ────────────────────────────────────────────────
// Uses IDirectInputDevice8::Escape(dwCommand=0) to control G29 shift LEDs.
// Discovered via Logitech SDK "Independant" sample — no external DLL needed.

static IDirectInputDevice8A* g_pDev = nullptr;

struct LedRpmData  { float currentRPM, rpmFirstLed, rpmRedLine; };
struct LedWheelData { DWORD size, version; LedRpmData rpm; };

static HRESULT DInputPlayLeds(float current, float firstLed, float redLine) {
    LedWheelData wd{};
    wd.size    = sizeof(LedWheelData);
    wd.version = 1;
    wd.rpm     = { current, firstLed, redLine };

    DIEFFESCAPE esc{};
    esc.dwSize     = sizeof(DIEFFESCAPE);
    esc.dwCommand  = 0;        // ESCAPE_COMMAND_LEDS
    esc.lpvInBuffer  = &wd;
    esc.cbInBuffer   = sizeof(wd);
    return g_pDev->Escape(&esc);
}

// ── Mode 2: Steering Wheel SDK DLL ───────────────────────────────────────────

typedef BOOL (WINAPI* PFN_SW_Init)    (BOOL ignoreXInput);
typedef VOID (WINAPI* PFN_SW_Shutdown)();
typedef BOOL (WINAPI* PFN_SW_PlayLeds)(int index, float currentRPM,
                                        float rpmFirstLed, float rpmRedLine,
                                        float rpmMaxShown);
static HMODULE       g_hSWSDK     = nullptr;
static PFN_SW_Init   g_SW_Init    = nullptr;
static PFN_SW_Shutdown g_SW_Shutdown = nullptr;
static PFN_SW_PlayLeds g_SW_PlayLeds = nullptr;

// ── Mode 3: Legacy LED SDK ────────────────────────────────────────────────────

typedef BOOL (__cdecl* PFN_LED_Init)       ();
typedef BOOL (__cdecl* PFN_LED_SetLighting)(int r, int g, int b);
typedef void (__cdecl* PFN_LED_Shutdown)   ();
static HMODULE          g_hLEDSDK        = nullptr;
static PFN_LED_Init     g_LED_Init        = nullptr;
static PFN_LED_SetLighting g_LED_SetLighting = nullptr;
static PFN_LED_Shutdown g_LED_Shutdown   = nullptr;

static DWORD g_lastBlinkTick = 0;
static bool  g_blinkOn       = false;

// ── Init ──────────────────────────────────────────────────────────────────────

bool InitLogitechLED(IDirectInputDevice8A* pDev) {

    // 1. DirectInput Escape — primary, works with G HUB, no DLL needed
    if (pDev) {
        g_pDev = pDev;
        HRESULT hr = DInputPlayLeds(0.0f, 1200.0f, 8000.0f);
        if (SUCCEEDED(hr)) {
            g_mode  = LEDMode::DInputEscape;
            g_ready = true;
            LOG_INFO("LogitechLED: G29 shift LEDs via DirectInput Escape — active");
            return true;
        }
        LOG_INFO("LogitechLED: DInput Escape not supported (hr=0x%08X) — trying SDK", (DWORD)hr);
        g_pDev = nullptr;
    }

    // 2. Steering Wheel SDK DLL — user-placed in game folder
    g_hSWSDK = LoadLibraryA("LogitechSteeringWheel.dll");
    if (g_hSWSDK) {
        g_SW_Init     = reinterpret_cast<PFN_SW_Init>    (GetProcAddress(g_hSWSDK, "LogiSteeringInitialize"));
        g_SW_Shutdown = reinterpret_cast<PFN_SW_Shutdown>(GetProcAddress(g_hSWSDK, "LogiSteeringShutdown"));
        g_SW_PlayLeds = reinterpret_cast<PFN_SW_PlayLeds>(GetProcAddress(g_hSWSDK, "LogiPlayLeds"));
        if (g_SW_Init && g_SW_Shutdown && g_SW_PlayLeds && g_SW_Init(TRUE)) {
            g_mode  = LEDMode::SteeringWheelSDK;
            g_ready = true;
            LOG_INFO("LogitechLED: SteeringWheel SDK — G29 shift LEDs active");
            return true;
        }
        FreeLibrary(g_hSWSDK); g_hSWSDK = nullptr;
    }

    // 3. Legacy LED SDK — G HUB RGB fallback
    static const char* kLedPaths[] = {
        "C:\\Program Files\\LGHUB\\sdks\\sdk_legacy_led_x86.dll",
        "C:\\Program Files (x86)\\LGHUB\\sdks\\sdk_legacy_led_x86.dll",
    };
    for (const char* p : kLedPaths) {
        g_hLEDSDK = LoadLibraryA(p);
        if (!g_hLEDSDK) continue;
        g_LED_Init        = reinterpret_cast<PFN_LED_Init>       (GetProcAddress(g_hLEDSDK, "LogiLedInit"));
        g_LED_SetLighting = reinterpret_cast<PFN_LED_SetLighting>(GetProcAddress(g_hLEDSDK, "LogiLedSetLighting"));
        g_LED_Shutdown    = reinterpret_cast<PFN_LED_Shutdown>   (GetProcAddress(g_hLEDSDK, "LogiLedShutdown"));
        if (g_LED_Init && g_LED_SetLighting && g_LED_Shutdown && g_LED_Init()) {
            g_mode  = LEDMode::LegacyLED;
            g_ready = true;
            LOG_INFO("LogitechLED: Legacy LED SDK loaded from %s — RGB colour feedback active (G29 shift LEDs require LogitechSteeringWheel.dll)", p);
            return true;
        }
        FreeLibrary(g_hLEDSDK); g_hLEDSDK = nullptr;
    }

    LOG_INFO("LogitechLED: no LED backend available");
    return false;
}

// ── Update ────────────────────────────────────────────────────────────────────

void UpdateShiftLights(float rpm, float redline) {
    if (!g_ready || redline <= 0.0f) return;
    const float firstLed = redline * 0.15f;
    const float pct      = std::clamp(rpm / redline, 0.0f, 1.1f);

    switch (g_mode) {
    case LEDMode::DInputEscape:
        DInputPlayLeds(rpm, firstLed, redline);
        break;

    case LEDMode::SteeringWheelSDK:
        g_SW_PlayLeds(0, rpm, firstLed, redline, redline);
        break;

    case LEDMode::LegacyLED: {
        int r = 0, g = 0, b = 0;
        if (pct >= 0.98f) {
            DWORD now = GetTickCount();
            if (now - g_lastBlinkTick >= 125) { g_blinkOn = !g_blinkOn; g_lastBlinkTick = now; }
            if (g_blinkOn) r = 100;
        } else if (pct >= 0.90f) { r = 100;
        } else if (pct >= 0.75f) { r = 100; g = 30;
        } else if (pct >= 0.50f) { r = 100; g = 100;
        } else if (pct >= 0.15f) { g = 100; }
        g_LED_SetLighting(r, g, b);
        break;
    }
    default: break;
    }
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void ShutdownLogitechLED() {
    if (!g_ready) return;
    switch (g_mode) {
    case LEDMode::DInputEscape:
        if (g_pDev) DInputPlayLeds(0.0f, 1200.0f, 8000.0f);
        g_pDev = nullptr;
        break;
    case LEDMode::SteeringWheelSDK:
        if (g_SW_Shutdown) g_SW_Shutdown();
        if (g_hSWSDK) { FreeLibrary(g_hSWSDK); g_hSWSDK = nullptr; }
        break;
    case LEDMode::LegacyLED:
        if (g_LED_Shutdown) g_LED_Shutdown();
        if (g_hLEDSDK) { FreeLibrary(g_hLEDSDK); g_hLEDSDK = nullptr; }
        break;
    default: break;
    }
    g_mode  = LEDMode::None;
    g_ready = false;
    LOG_INFO("LogitechLED: shutdown");
}
