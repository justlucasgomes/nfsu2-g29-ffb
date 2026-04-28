#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include "logitech_led.h"
#include "logger.h"

// ── Logitech Steering Wheel SDK — dynamic load ────────────────────────────────
//
// LogitechSteeringWheel.dll is distributed by Logitech Gaming Software and
// G HUB. It is NOT linked at build time so the mod loads cleanly even if the
// SDK is absent.
//
// Exported functions used:
//   LogiSteeringInitialize(BOOL ignoreXInputControllers) -> BOOL
//   LogiSteeringShutdown()                               -> void
//   LogiPlayLeds(int index,
//                float currentRPM,
//                float rpmFirstLedTurnsOn,
//                float rpmRedLine,
//                float rpmMaxShown)                      -> BOOL
//
// LogiPlayLeds handles the progressive LED display automatically:
//   currentRPM < rpmFirstLedTurnsOn  → all LEDs off
//   rpmFirstLedTurnsOn..rpmRedLine   → LEDs light progressively
//   currentRPM >= rpmRedLine         → all LEDs on + blink

typedef BOOL (WINAPI* PFN_Init)    (BOOL ignoreXInputControllers);
typedef VOID (WINAPI* PFN_Shutdown)();
typedef BOOL (WINAPI* PFN_PlayLeds)(int index, float currentRPM,
                                     float rpmFirstLedTurnsOn,
                                     float rpmRedLine,
                                     float rpmMaxShown);

static HMODULE     g_hSDK    = nullptr;
static PFN_Init    g_Init    = nullptr;
static PFN_Shutdown g_Shutdown = nullptr;
static PFN_PlayLeds g_PlayLeds = nullptr;
static bool        g_ready   = false;

// Search order for the SDK DLL.
// The DLL is 32-bit — must match the 32-bit mod build.
static const char* const kSDKPaths[] = {
    "LogitechSteeringWheel.dll",   // game folder or PATH (user can place it here)
    "C:\\Program Files\\LGHUB\\LogitechSteeringWheel.dll",
    "C:\\Program Files (x86)\\LGHUB\\LogitechSteeringWheel.dll",
    "C:\\Program Files\\Logitech\\Gaming Software\\SDKs\\SteeringWheel\\x32\\LogitechSteeringWheel.dll",
    "C:\\Program Files (x86)\\Logitech\\Gaming Software\\SDKs\\SteeringWheel\\x32\\LogitechSteeringWheel.dll",
};

// ── Init ──────────────────────────────────────────────────────────────────────

bool InitLogitechLED() {
    // Try each search path
    for (const char* path : kSDKPaths) {
        g_hSDK = LoadLibraryA(path);
        if (g_hSDK) {
            LOG_INFO("LogitechLED: SDK loaded from %s", path);
            break;
        }
    }
    if (!g_hSDK) {
        LOG_INFO("LogitechLED: SDK not found — shift lights disabled");
        return false;
    }

    g_Init    = reinterpret_cast<PFN_Init>    (GetProcAddress(g_hSDK, "LogiSteeringInitialize"));
    g_Shutdown = reinterpret_cast<PFN_Shutdown>(GetProcAddress(g_hSDK, "LogiSteeringShutdown"));
    g_PlayLeds = reinterpret_cast<PFN_PlayLeds>(GetProcAddress(g_hSDK, "LogiPlayLeds"));

    if (!g_Init || !g_Shutdown || !g_PlayLeds) {
        LOG_ERROR("LogitechLED: SDK function lookup failed (wrong DLL version?)");
        FreeLibrary(g_hSDK);
        g_hSDK = nullptr;
        return false;
    }

    if (!g_Init(TRUE)) {  // TRUE = ignore XInput controllers
        LOG_ERROR("LogitechLED: initialization failed");
        FreeLibrary(g_hSDK);
        g_hSDK = nullptr;
        return false;
    }

    g_ready = true;
    LOG_INFO("LogitechLED: initialized — shift lights active");
    return true;
}

// ── Update ────────────────────────────────────────────────────────────────────

void UpdateShiftLights(float rpm, float redline) {
    if (!g_ready || !g_PlayLeds) return;
    if (redline <= 0.0f) return;

    // First LED turns on at 15 % of redline (mirrors Assetto Corsa behaviour).
    // LogiPlayLeds maps [firstLed, redline] linearly onto the 5 shift LEDs.
    // Above redline the SDK switches to blink mode automatically.
    const float firstLed = redline * 0.15f;

    g_PlayLeds(
        0,         // device index (0 = first connected wheel)
        rpm,       // current RPM
        firstLed,  // RPM at which first LED turns on
        redline,   // RPM at which all LEDs light + blink starts
        redline    // rpmMaxShown — no dead zone above redline
    );

    // Debug snapshot — LogLevel=3 only (too frequent for LogLevel=2)
    {
        static int s_tick = 0;
        if (++s_tick >= 100) {
            s_tick = 0;
            float pct = std::clamp(rpm / redline, 0.0f, 1.0f);
            if (pct >= 0.98f)
                LOG_DEBUG("LogitechLED: redline blink");
            else
                LOG_DEBUG("LogitechLED: rpm=%.0f redline=%.0f pct=%.2f",
                          rpm, redline, pct);
        }
    }
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void ShutdownLogitechLED() {
    if (g_ready && g_Shutdown) {
        g_Shutdown();
        g_ready = false;
        LOG_INFO("LogitechLED: shutdown");
    }
    if (g_hSDK) {
        FreeLibrary(g_hSDK);
        g_hSDK    = nullptr;
        g_Init    = nullptr;
        g_Shutdown = nullptr;
        g_PlayLeds = nullptr;
    }
}
