#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include "logitech_led.h"
#include "logger.h"

// ── G HUB Legacy LED SDK ──────────────────────────────────────────────────────
// Controls RGB lighting on Logitech G HUB-managed devices.
// Loaded dynamically — mod works if SDK is absent.

typedef BOOL (__cdecl* PFN_LED_Init)       ();
typedef BOOL (__cdecl* PFN_LED_SetLighting)(int r, int g, int b);  // 0-100 %
typedef void (__cdecl* PFN_LED_Shutdown)   ();

static HMODULE          g_hSDK           = nullptr;
static PFN_LED_Init     g_LED_Init        = nullptr;
static PFN_LED_SetLighting g_LED_SetLighting = nullptr;
static PFN_LED_Shutdown g_LED_Shutdown   = nullptr;
static bool             g_ready          = false;

static DWORD g_lastBlinkTick = 0;
static bool  g_blinkOn       = false;

static const char* const kPaths[] = {
    "C:\\Program Files\\LGHUB\\sdks\\sdk_legacy_led_x86.dll",
    "C:\\Program Files (x86)\\LGHUB\\sdks\\sdk_legacy_led_x86.dll",
};

bool InitLogitechLED() {
    for (const char* p : kPaths) {
        g_hSDK = LoadLibraryA(p);
        if (!g_hSDK) continue;

        g_LED_Init        = reinterpret_cast<PFN_LED_Init>       (GetProcAddress(g_hSDK, "LogiLedInit"));
        g_LED_SetLighting = reinterpret_cast<PFN_LED_SetLighting>(GetProcAddress(g_hSDK, "LogiLedSetLighting"));
        g_LED_Shutdown    = reinterpret_cast<PFN_LED_Shutdown>   (GetProcAddress(g_hSDK, "LogiLedShutdown"));

        if (g_LED_Init && g_LED_SetLighting && g_LED_Shutdown && g_LED_Init()) {
            g_ready = true;
            LOG_INFO("LogitechLED: RGB feedback via G HUB SDK (%s)", p);
            return true;
        }
        FreeLibrary(g_hSDK);
        g_hSDK            = nullptr;
        g_LED_Init        = nullptr;
        g_LED_SetLighting = nullptr;
        g_LED_Shutdown    = nullptr;
    }
    LOG_INFO("LogitechLED: G HUB SDK not found — LED feedback disabled");
    return false;
}

void UpdateShiftLights(float rpm, float redline) {
    if (!g_ready || redline <= 0.0f) return;
    const float pct = std::clamp(rpm / redline, 0.0f, 1.1f);

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
}

void ShutdownLogitechLED() {
    if (!g_ready) return;
    if (g_LED_Shutdown) g_LED_Shutdown();
    if (g_hSDK) { FreeLibrary(g_hSDK); g_hSDK = nullptr; }
    g_LED_Init = nullptr; g_LED_SetLighting = nullptr; g_LED_Shutdown = nullptr;
    g_ready = false;
    LOG_INFO("LogitechLED: shutdown");
}
