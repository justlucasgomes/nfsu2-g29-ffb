// telemetry.cpp
// Reads live game data from NFSU2 process memory (in-process — direct access).
//
// NFSU2 NA retail (4,800,512 bytes  MD5: 665871070B0E4065CE446967294BCCFA)
// Image base: 0x00400000 (no ASLR on this era).
//
// Primary method: pattern scanner finds the speedometer read instruction,
// extracts the speed address from its operand.  Falls back to static
// pointer chain from config.ini if scan fails.
//
// All reads are guarded with VirtualQuery + __try/__except to ensure a wrong
// offset never crashes the game.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <algorithm>
#include "telemetry.h"
#include "config.h"
#include "logger.h"
#include "pattern_scan.h"
#include "nfsu2_addresses.h"

using namespace NFSU2_NA;
using namespace PatternScan;

// ─────────────────────────────────────────────────────────────────────────────

Telemetry& Telemetry::Get() {
    static Telemetry inst;
    return inst;
}

// ── Init ──────────────────────────────────────────────────────────────────────

bool Telemetry::Init() {
    bool ok = false;

    if (g_Config.telemetry.usePatternScan)
        ok = ResolveViaPatternScan();

    if (!ok)
        ok = ResolveViaStaticPtr();

    if (ok) {
        m_ready.store(true, std::memory_order_release);
        LOG_INFO("Telemetry: ready — carPtr=0x%08X", (DWORD)m_ptrCarPtr);
    } else {
        LOG_ERROR("Telemetry: could not resolve car pointer — "
                  "FFB will use steering-input-only fallback");
    }
    return ok;
}

bool Telemetry::ResolveViaPatternScan() {
    // Speedometer code reads: fld dword ptr [speed_addr]
    // Pattern: D9 05 ?? ?? ?? ??  followed by D8 4D (fmul)
    uintptr_t hit = ScanModule(PATTERN_SPEED_READ);
    if (hit) {
        uintptr_t sAddr = Deref(hit + 2);
        if (IsReadable(sAddr, sizeof(float))) {
            float v = SafeReadFloat(sAddr);
            if (v >= 0.0f && v < 300.0f) {
                m_addrSpeed = sAddr;
                LOG_INFO("Telemetry: speed pattern found at 0x%08X val=%.1f m/s",
                         (DWORD)sAddr, v);
            }
        }
    }

    bool staticOk = ResolveViaStaticPtr();
    return staticOk || (m_addrSpeed != 0);
}

bool Telemetry::ResolveViaStaticPtr() {
    DWORD ptr = g_Config.telemetry.ptrPlayerCarPtr;
    if (!IsReadable(ptr, sizeof(DWORD))) {
        LOG_ERROR("Telemetry: static ptr 0x%08X not readable", ptr);
        return false;
    }
    m_ptrCarPtr = ptr;
    LOG_INFO("Telemetry: using static carPtr=0x%08X", ptr);
    return true;
}

// ── Read (called every 10 ms) ──────────────────────────────────────────────────

TelemetryData Telemetry::Read() {
    TelemetryData d{};

    // Resolve car base address from static pointer chain
    uintptr_t carBase = m_ptrCarPtr ? Deref(m_ptrCarPtr) : 0;
    d.playerCarValid = (carBase != 0);

    if (carBase) {
        // ── Primary reads ────────────────────────────────────────────────────
        d.speed      = SafeReadFloat(carBase + g_Config.telemetry.ofsSpeedMps,    0.0f);
        d.lateralAccel = SafeReadFloat(carBase + g_Config.telemetry.ofsLateralAccel, 0.0f);
        d.longAccel  = SafeReadFloat(carBase + OFS_LONG_ACCEL,    0.0f);
        d.steerAngle = SafeReadFloat(carBase + g_Config.telemetry.ofsSteerAngle,  0.0f);

        // ── Wheel spin (4 floats starting at OFS_WHEEL_SPIN_FL) ──────────────
        float wfl = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL,      0.0f);
        float wfr = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL + 4,  0.0f);
        float wrl = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL + 8,  0.0f);
        float wrr = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL + 12, 0.0f);
        d.wheelSpinMax = std::max({ wfl, wfr, wrl, wrr });

        // ── Slip angle estimate (if not directly available) ───────────────────
        // NFSU2 may not expose raw slip angle; we estimate from lateral accel
        // and speed: slip_deg ≈ atan(lat_g / speed_component) × 57.3
        // This approximation is "good enough" for FFB vibration scaling.
        if (d.speed > 0.5f) {
            float latG  = std::fabsf(d.lateralAccel) / 9.81f;
            float ratio = latG / (d.speed * 0.3f);  // rough: lat/speed ratio
            d.slipAngle = std::min(20.0f, std::atanf(ratio) * 57.295779f);
        }

        // ── Wheel load (estimate from long accel — weight transfer proxy) ─────
        float longG = std::fabsf(d.longAccel) / 9.81f;
        d.wheelLoad = std::min(1.0f, longG / 1.5f);

        // ── Collision delta ────────────────────────────────────────────────────
        float dv = m_prevSpeed - d.speed;
        d.collision = (dv > 0.0f) ? std::min(1.0f, dv / 5.0f) : 0.0f;

    } else if (m_addrSpeed) {
        // Pattern-scan fallback: only speed is known
        d.speed = SafeReadFloat(m_addrSpeed, 0.0f);
        d.speed = std::max(0.0f, d.speed);
    }

    m_prevSpeed = d.speed;

    // ── Normalize ──────────────────────────────────────────────────────────────
    float maxSpd = g_Config.telemetry.maxSpeedMps;
    float maxLat = g_Config.telemetry.maxLateralAccelMs2;

    d.speed       = std::max(0.0f, d.speed);
    d.speedNorm   = std::max(0.0f, std::min(1.0f, d.speed / maxSpd));
    d.lateralNorm = std::max(0.0f, std::min(1.0f, std::fabsf(d.lateralAccel) / maxLat));
    d.steerAngle  = std::max(-1.0f, std::min(1.0f, d.steerAngle));
    d.wheelSpinMax = std::max(0.0f, std::min(1.0f, d.wheelSpinMax));

    // ── Race state guess: assume in race if speed > 0.5 m/s ───────────────────
    d.inRace = d.speed > 0.5f;

    return d;
}
