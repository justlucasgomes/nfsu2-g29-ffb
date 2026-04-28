#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Logitech G29 shift-indicator LEDs — driven by the Logitech Steering Wheel SDK.
//
//  The SDK DLL (LogitechSteeringWheel.dll) is loaded dynamically at runtime.
//  If it is not present, all calls are silently no-ops so the mod continues
//  to work without Logitech Gaming Software / G HUB installed.
//
//  Usage:
//    InitLogitechLED();                       // once, after wheel is detected
//    UpdateShiftLights(tele.rpm, redline);    // every FFB update frame
//    ShutdownLogitechLED();                   // on DLL_PROCESS_DETACH
// ─────────────────────────────────────────────────────────────────────────────

// Returns true if the SDK was found and initialized successfully.
bool InitLogitechLED();

// Updates G29 shift-indicator LEDs based on current RPM and redline.
// rpm    — live engine RPM (0 = all LEDs off)
// redline — RPM at which all LEDs light and blink begins
void UpdateShiftLights(float rpm, float redline);

// Releases the SDK and turns off all LEDs.
void ShutdownLogitechLED();
