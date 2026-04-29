#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

// ─────────────────────────────────────────────────────────────────────────────
//  G29 shift-indicator LEDs — three-tier approach, best available wins:
//
//  1. DirectInput Escape (primary) — IDirectInputDevice8::Escape(cmd=0)
//     Uses the G29 device handle directly. No external DLL required.
//     Works with G HUB. Controls the 5 physical shift LEDs.
//
//  2. Steering Wheel SDK DLL (secondary) — LogitechSteeringWheel.dll
//     Place the x86 DLL in the NFSU2 game folder. Shipped with LGS.
//
//  3. Legacy LED SDK (fallback) — sdk_legacy_led_x86.dll (installed by G HUB)
//     Controls RGB lighting on compatible Logitech devices (not G29 LEDs).
// ─────────────────────────────────────────────────────────────────────────────

// pDev — G29 DirectInput device handle (used for primary DInput Escape method)
bool InitLogitechLED(IDirectInputDevice8A* pDev = nullptr);

void UpdateShiftLights(float rpm, float redline);
void ShutdownLogitechLED();
