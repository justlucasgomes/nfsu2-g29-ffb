#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "car_physics.h"

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

    // ── Drivetrain ────────────────────────────────────────────────────────────
    int            gear;    // 0=neutral, 1-6=drive
    float          rpm;     // engine RPM — 0 when OFS_RPM not yet configured
    uint32_t       carId;   // car identifier — 0 when not yet resolved
    CarPhysicsData physics; // per-car parameters (mass, grip, lock) — live every frame

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

    // ── CarId auto-resolution via runtime struct scan ──────────────────────────
    // Phase 1 (BuildCarIdCandidates): scans [carBase+0x04 … carBase+0x300] for
    //   DWORD values in [1, 511]. Called once on first valid carBase in Read().
    // Phase 2 (ValidateCarId): non-blocking state machine in Read().
    //   Capture (2 s) → WaitChange (3 s) → Resolved / Failed
    //   Capture measures stability — constant values score highest.
    //   WaitChange gives a bonus if the value changes when the player changes car.
    void BuildCarIdCandidates(uintptr_t carBase);
    void ValidateCarId(uintptr_t carBase);

    enum class CarIdValPhase : uint8_t { Capture, WaitChange, Resolved, Failed };
    struct CarIdCandState {
        uint32_t capturedVal = 0;   // value at start of Capture
        uint32_t prevVal     = 0;   // previous tick (change detection)
        int      changeCount = 0;   // # value changes during Capture
        float    score       = 0.f; // composite score (higher = better)
        bool     alive       = true;
    };

    // RPM auto-resolution via pattern scan + behavioral state machine.
    // Phase 1 (BuildRpmCandidates): scans the binary once in Init().
    // Phase 2 (ValidateRpmOffset): non-blocking state machine in Read().
    //   CaptureIdle → WaitRise → WaitFall → Resolved / Failed
    void BuildRpmCandidates();
    void ValidateRpmOffset(uintptr_t carBase);

    // Per-candidate state for behavioral validation
    enum class RpmValPhase : uint8_t { CaptureIdle, WaitRise, WaitFall, Resolved, Failed };
    struct RpmCandState {
        float idleVal  = 0.0f;  // baseline captured in CaptureIdle
        float peakVal  = 0.0f;  // highest value seen (updated in WaitRise)
        float prevVal  = -1.0f; // previous tick value (-1 = not yet seen)
        float finalVal = 0.0f;  // value at end of WaitFall (for returnScore)
        float maxDelta = 0.0f;  // largest abs(val - prevVal) seen (smoothness)
        float score    = 0.0f;  // final composite score (higher = more RPM-like)
        bool  alive    = true;  // false = disqualified
    };

    std::atomic<bool> m_ready{false};

    uintptr_t m_ptrCarPtr  = 0;   // static ptr → car object
    uintptr_t m_addrSpeed  = 0;   // direct address if found via pattern scan
    float     m_prevSpeed  = 0.0f;

    // RPM auto-resolution state
    std::vector<DWORD>         m_rpmCandidates;              // offsets from binary scan
    std::vector<RpmCandState>  m_rpmCandState;               // per-candidate tracking
    DWORD                      m_rpmOffset          = 0;    // resolved offset (0 = unresolved)
    bool                       m_rpmCandidatesBuilt = false;
    RpmValPhase                m_rpmValPhase        = RpmValPhase::CaptureIdle;
    int                        m_rpmValTick         = 0;

    // CarId auto-resolution state
    std::vector<DWORD>             m_carIdCandidates;
    std::vector<CarIdCandState>    m_carIdCandState;
    DWORD                          m_carIdOffset     = 0;   // resolved (0 = unresolved)
    uint32_t                       m_prevCarId       = 0;   // for change-log
    bool                           m_carIdCandBuilt  = false;
    CarIdValPhase                  m_carIdValPhase   = CarIdValPhase::Capture;
    int                            m_carIdValTick    = 0;
};
