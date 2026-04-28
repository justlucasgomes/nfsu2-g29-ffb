#pragma once
#include <windows.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NFSU2 NA (US retail) - SPEED2.EXE
//  File size : 4,800,512 bytes
//  MD5       : 665871070B0E4065CE446967294BCCFA
//  Image base: 0x00400000  (no ASLR on this era)
//
//  IMPORTANT: These offsets were derived from community reverse-engineering
//  research (NFSPlugins, NFSU2ExtraOptions, hex analysis).  If the game
//  binary differs (patched/cracked), run the pattern scanner fallback.
//  Use Cheat Engine to verify and update [Telemetry] in config.ini.
// ─────────────────────────────────────────────────────────────────────────────

namespace NFSU2_NA {

    // ── Static data pointers (in .data/.bss, relative to image base 0x400000) ──

    // Pointer to the active player car simulation object.
    // Dereference → base of NFSU2 car simulation struct.
    // 0x575748 was WRONG — it is inside .text (code), not .data.
    // Verified via Cheat Engine: ECX=0x03499C20 at fstp [ecx+0x400] (RPM write).
    // TODO: replace with the correct .data address found via CE pointer scan
    //       (scan for value 0x03499C20 in range 0x7E8000–0x8BFFFF).
    constexpr DWORD PTR_PLAYER_CAR  = 0x575748;   // PLACEHOLDER — needs update

    // ── Offsets from car simulation object ────────────────────────────────────

    // Speed in meters per second (float).
    // Multiply by 3.6 for km/h.  Typical range: 0 – 88 m/s (~317 km/h).
    constexpr DWORD OFS_SPEED_MPS       = 0x00DC;

    // Lateral acceleration in m/s² (float, signed: left negative / right positive).
    // Typical cornering: ±8–20 m/s².
    constexpr DWORD OFS_LATERAL_ACCEL   = 0x0160;

    // Longitudinal acceleration (braking / throttle), m/s² (float).
    constexpr DWORD OFS_LONG_ACCEL      = 0x015C;

    // Steering wheel angle in the physics simulation (-1 = full left, +1 = full right).
    constexpr DWORD OFS_STEER_ANGLE     = 0x0148;

    // Wheel spin / traction loss (0 = grip, 1 = full spin).  Per-wheel array.
    constexpr DWORD OFS_WHEEL_SPIN_FL   = 0x01C0;
    constexpr DWORD OFS_WHEEL_SPIN_FR   = 0x01C4;
    constexpr DWORD OFS_WHEEL_SPIN_RL   = 0x01C8;
    constexpr DWORD OFS_WHEEL_SPIN_RR   = 0x01CC;

    // Current gear (int, 0=neutral, 1-6=drive)
    constexpr DWORD OFS_GEAR            = 0x0068;

    // Engine RPM (float).
    // Verified via Cheat Engine "Find out what writes":
    //   005A5A51  fstp dword ptr [ecx+00000400]   ECX=0x03499C20
    //   Write address: 0x03499C20 + 0x400 = 0x0349A020  ✓ (matches CE scan)
    // Typical range: ~800 RPM (idle) to ~8000 RPM (redline).
    constexpr DWORD OFS_RPM             = 0x0400;

    // Damage state accumulator (0.0 – 1.0 float, increases on collision)
    constexpr DWORD OFS_DAMAGE          = 0x0240;

    // ── Pattern strings for dynamic scan (fallback) ───────────────────────────
    // Format: space-separated hex bytes, '??' = wildcard

    // Speedometer HUD code reads the player speed:
    //   D9 05 ?? ?? ?? ??   ; fld  dword ptr [speed_addr]
    //   D8 4D ??            ; fmul dword ptr [ebp+xx]
    // The ?? ?? ?? ?? after D9 05 is the speed address.
    constexpr const char* PATTERN_SPEED_READ =
        "D9 05 ?? ?? ?? ?? D8 4D ?? DD 5D ?? D9 05";

    // Input device update: game calls IDirectInputDevice8::GetDeviceState
    //   6A 1C   push 28
    //   8D ?? ?? 8B ??   lea + mov (varies)
    constexpr const char* PATTERN_INPUT_UPDATE =
        "6A 1C 8B ?? ?? ?? ?? ?? 8B 08 FF 51 ??";

    // ── Physical constants ─────────────────────────────────────────────────────
    constexpr float MAX_SPEED_MPS        = 88.0f;   // ~317 km/h (tuned car)
    constexpr float MAX_LATERAL_ACCEL    = 25.0f;   // 2.5 G cornering limit
    constexpr float COLLISION_THRESHOLD  = 0.15f;   // m/s delta/frame ≈ sudden impact

} // namespace NFSU2_NA
