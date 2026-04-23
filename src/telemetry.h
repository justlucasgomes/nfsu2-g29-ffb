#pragma once
#include <atomic>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  TelemetryData — live snapshot of the NFSU2 physics simulation.
//
//  Read from NFSU2 process memory (in-process DLL — direct pointer access).
//  Pattern scanner resolves addresses dynamically; static fallback offsets
//  from nfsu2_addresses.h are used if scan fails.
//
//  If the car object pointer is null (menu, loading), defaults are safe zeros.
// ─────────────────────────────────────────────────────────────────────────────
struct TelemetryData {
    // ── Primary physics ───────────────────────────────────────────────────────
    float speed;         // m/s — 0..~88  (multiply by 3.6 for km/h)
    float speedNorm;     // 0..1  (normalized by maxSpeed)

    // ── Forces ────────────────────────────────────────────────────────────────
    float lateralAccel;  // m/s² signed (negative=left, positive=right)
    float lateralNorm;   // 0..1  |lateralAccel| / maxLateralAccel
    float longAccel;     // m/s² signed (negative=braking, positive=throttle)

    // ── Grip / traction ───────────────────────────────────────────────────────
    float slipAngle;     // degrees — 0..~20+ (front axle slip angle)
    float wheelSpinMax;  // 0..1   max of 4 wheels (0=grip, 1=full spin)
    float wheelLoad;     // 0..1   normalized vertical load (weight transfer)

    // ── Steering ──────────────────────────────────────────────────────────────
    float steerAngle;    // -1..+1 physics steering angle

    // ── Impact (derived) ──────────────────────────────────────────────────────
    float collision;     // 0..1  speed-delta spike this frame (for impulse FFB)

    // ── State ─────────────────────────────────────────────────────────────────
    bool  playerCarValid; // true when car object pointer is non-null
    bool  inRace;         // true when race is active (not menu/garage)
};

// ─────────────────────────────────────────────────────────────────────────────

class Telemetry {
public:
    static Telemetry& Get();

    bool Init();
    void Shutdown() { m_ready.store(false); }

    TelemetryData Read();
    bool IsReady() const { return m_ready.load(std::memory_order_acquire); }

private:
    Telemetry() = default;
    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;

    bool ResolveViaPatternScan();
    bool ResolveViaStaticPtr();

    std::atomic<bool> m_ready{false};

    uintptr_t m_ptrCarPtr  = 0;   // static ptr → car object
    uintptr_t m_addrSpeed  = 0;   // direct address if found via pattern scan
    float     m_prevSpeed  = 0.0f;
};
