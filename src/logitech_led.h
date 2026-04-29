#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Logitech G29 RPM colour feedback via G HUB Legacy LED SDK.
//
//  sdk_legacy_led_x86.dll (installed by G HUB) is loaded dynamically.
//  Controls RGB lighting on compatible Logitech devices.
//  G29 physical shift LEDs require LogitechSteeringWheel.dll (not available).
//
//  If no SDK is found all calls are silent no-ops.
// ─────────────────────────────────────────────────────────────────────────────

bool InitLogitechLED();
void UpdateShiftLights(float rpm, float redline);
void ShutdownLogitechLED();
