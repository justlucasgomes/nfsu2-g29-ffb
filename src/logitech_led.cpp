#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include "logitech_led.h"
#include "logger.h"

// ── SDK Mode ──────────────────────────────────────────────────────────────────
//
// Two Logitech SDKs are tried in order:
//
// 1. Steering Wheel SDK  (LogitechSteeringWheel.dll)
//    Controls G29 hardware shift-indicator LEDs via LogiPlayLeds().
//    NOT installed by G HUB — place the DLL manually in the NFSU2 game folder.
//
// 2. Legacy LED SDK  (sdk_legacy_led_x86.dll, installed by G HUB)
//    Controls RGB lighting on compatible Logitech devices (keyboards, headsets…).
//    G29 shift LEDs are NOT reachable via this SDK; an RPM colour-gradient is
//    provided as a visual indicator on whatever RGB device is connected.

enum class SDKMode { None, SteeringWheel, LegacyLED };
static SDKMode  g_mode  = SDKMode::None;
static HMODULE  g_hSDK  = nullptr;
static bool     g_ready = false;

// ── Steering Wheel SDK typedefs ───────────────────────────────────────────────

typedef BOOL (WINAPI* PFN_SW_Init)    (BOOL ignoreXInput);
typedef VOID (WINAPI* PFN_SW_Shutdown)();
typedef BOOL (WINAPI* PFN_SW_PlayLeds)(int index, float currentRPM,
                                        float rpmFirstLed,
                                        float rpmRedLine,
                                        float rpmMaxShown);

static PFN_SW_Init     g_SW_Init     = nullptr;
static PFN_SW_Shutdown g_SW_Shutdown = nullptr;
static PFN_SW_PlayLeds g_SW_PlayLeds = nullptr;

// Only the game-folder path — user must place the DLL here manually.
static const char* const kSwPaths[] = {
    "LogitechSteeringWheel.dll",
};

// ── Legacy LED SDK typedefs ───────────────────────────────────────────────────
// Functions use default (__cdecl) calling convention per Logitech SDK headers.

typedef BOOL (__cdecl* PFN_LED_Init)        ();
typedef BOOL (__cdecl* PFN_LED_SetLighting) (int r, int g, int b);  // 0-100 %
typedef void (__cdecl* PFN_LED_Shutdown)    ();

static PFN_LED_Init        g_LED_Init        = nullptr;
static PFN_LED_SetLighting g_LED_SetLighting = nullptr;
static PFN_LED_Shutdown    g_LED_Shutdown    = nullptr;

static const char* const kLedPaths[] = {
    "C:\\Program Files\\LGHUB\\sdks\\sdk_legacy_led_x86.dll",
    "C:\\Program Files (x86)\\LGHUB\\sdks\\sdk_legacy_led_x86.dll",
    "sdk_legacy_led_x86.dll",  // game folder fallback
};

// ── Blink state (Legacy LED SDK redline flash) ────────────────────────────────
static DWORD g_lastBlinkTick = 0;
static bool  g_blinkOn       = false;

// ── Init ──────────────────────────────────────────────────────────────────────

bool InitLogitechLED() {
    // 1. Steering Wheel SDK — direct G29 shift LED control
    for (const char* path : kSwPaths) {
        g_hSDK = LoadLibraryA(path);
        if (!g_hSDK) continue;

        g_SW_Init     = reinterpret_cast<PFN_SW_Init>    (GetProcAddress(g_hSDK, "LogiSteeringInitialize"));
        g_SW_Shutdown = reinterpret_cast<PFN_SW_Shutdown>(GetProcAddress(g_hSDK, "LogiSteeringShutdown"));
        g_SW_PlayLeds = reinterpret_cast<PFN_SW_PlayLeds>(GetProcAddress(g_hSDK, "LogiPlayLeds"));

        if (g_SW_Init && g_SW_Shutdown && g_SW_PlayLeds && g_SW_Init(TRUE)) {
            g_mode  = SDKMode::SteeringWheel;
            g_ready = true;
            LOG_INFO("LogitechLED: SteeringWheel SDK loaded from %s — G29 shift LEDs active", path);
            return true;
        }

        FreeLibrary(g_hSDK);
        g_hSDK        = nullptr;
        g_SW_Init     = nullptr;
        g_SW_Shutdown = nullptr;
        g_SW_PlayLeds = nullptr;
    }

    // 2. Legacy LED SDK — RGB colour gradient on G HUB devices
    for (const char* path : kLedPaths) {
        g_hSDK = LoadLibraryA(path);
        if (!g_hSDK) continue;

        g_LED_Init        = reinterpret_cast<PFN_LED_Init>       (GetProcAddress(g_hSDK, "LogiLedInit"));
        g_LED_SetLighting = reinterpret_cast<PFN_LED_SetLighting>(GetProcAddress(g_hSDK, "LogiLedSetLighting"));
        g_LED_Shutdown    = reinterpret_cast<PFN_LED_Shutdown>   (GetProcAddress(g_hSDK, "LogiLedShutdown"));

        if (g_LED_Init && g_LED_SetLighting && g_LED_Shutdown && g_LED_Init()) {
            g_mode  = SDKMode::LegacyLED;
            g_ready = true;
            LOG_INFO("LogitechLED: Legacy LED SDK loaded from %s — RGB colour feedback active (G29 shift LEDs require LogitechSteeringWheel.dll)", path);
            return true;
        }

        FreeLibrary(g_hSDK);
        g_hSDK            = nullptr;
        g_LED_Init        = nullptr;
        g_LED_SetLighting = nullptr;
        g_LED_Shutdown    = nullptr;
    }

    LOG_INFO("LogitechLED: no Logitech SDK found — LEDs disabled");
    return false;
}

// ── Update ────────────────────────────────────────────────────────────────────

void UpdateShiftLights(float rpm, float redline) {
    if (!g_ready || redline <= 0.0f) return;

    const float pct = std::clamp(rpm / redline, 0.0f, 1.1f);

    if (g_mode == SDKMode::SteeringWheel) {
        // LogiPlayLeds handles progressive LED fill and blink automatically.
        const float firstLed = redline * 0.15f;
        g_SW_PlayLeds(0, rpm, firstLed, redline, redline);
    }
    else if (g_mode == SDKMode::LegacyLED) {
        int r = 0, g = 0, b = 0;

        if (pct >= 0.98f) {
            // Redline: blink red at ~4 Hz
            DWORD now = GetTickCount();
            if (now - g_lastBlinkTick >= 125) {
                g_blinkOn       = !g_blinkOn;
                g_lastBlinkTick = now;
            }
            if (g_blinkOn) { r = 100; }
        } else if (pct >= 0.90f) {
            r = 100;                   // red
        } else if (pct >= 0.75f) {
            r = 100; g = 30;           // orange
        } else if (pct >= 0.50f) {
            r = 100; g = 100;          // yellow
        } else if (pct >= 0.15f) {
            g = 100;                   // green
        }
        // below 15% of redline: all off

        g_LED_SetLighting(r, g, b);
    }

    // Debug every ~1 s (100 × 10 ms ticks)
    {
        static int s_tick = 0;
        if (++s_tick >= 100) {
            s_tick = 0;
            LOG_DEBUG("LogitechLED: rpm=%.0f redline=%.0f pct=%.2f", rpm, redline, pct);
        }
    }
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void ShutdownLogitechLED() {
    if (g_ready) {
        if (g_mode == SDKMode::SteeringWheel && g_SW_Shutdown)
            g_SW_Shutdown();
        else if (g_mode == SDKMode::LegacyLED && g_LED_Shutdown)
            g_LED_Shutdown();

        g_ready = false;
        g_mode  = SDKMode::None;
        LOG_INFO("LogitechLED: shutdown");
    }
    if (g_hSDK) {
        FreeLibrary(g_hSDK);
        g_hSDK = nullptr;
    }
    g_SW_Init     = nullptr;
    g_SW_Shutdown = nullptr;
    g_SW_PlayLeds = nullptr;
    g_LED_Init        = nullptr;
    g_LED_SetLighting = nullptr;
    g_LED_Shutdown    = nullptr;
}
