#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include "telemetry.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ForceFeedback — 7 DirectInput effects on the G29 steering axis.
//
//  Design philosophy:
//    AC  = precision of spring/damper/lateral parameters
//    FH5 = feel curve (light at low speed, progressive at high speed)
//
//  FH5 ControllerFFB.ini reference values (C:\XboxGames\Forza Horizon 5\):
//    SpringScaleSpeed0      = 2    m/s  → SpeedScale0 = 0.15 (15% spring)
//    SpringScaleSpeed1      = 15   m/s  → SpeedScale1 = 1.0  (100% spring)
//    InRaceSpringMaxForce   = 0.5        → overall spring cap = 50%
//    InRaceDampingMaxForce  = 0.31
//    InRaceDampingScale     = 0.33
//    RightVibrationWavelength = 21 ms   → 47 Hz (road detail)
//    LeftVibrationWavelength  = 38 ms   → 26 Hz (road rumble)
//    DynFFBScale            = 0.42
//
//  AC controls.ini reference values:
//    MIN_FF=0.05  CURBS=0.4  ROAD=0.5  SLIPS=0  UNDERSTEER=0  DAMPER_GAIN=1
// ─────────────────────────────────────────────────────────────────────────────

class ForceFeedback {
public:
    static ForceFeedback& Get();

    bool Init(IDirectInputDevice8A* pDev);
    void Shutdown();

    // Update all continuous effects — call every 10 ms from WheelInput thread.
    void Update(const TelemetryData& tele, float steeringInput);

    // One-shot collision impulse (auto-triggered inside Update from speed delta).
    void TriggerCollision(float impact);

    bool IsReady() const { return m_ready; }

private:
    ForceFeedback() = default;
    ~ForceFeedback() { Shutdown(); }
    ForceFeedback(const ForceFeedback&) = delete;
    ForceFeedback& operator=(const ForceFeedback&) = delete;

    // Effect creation
    bool CreateSpring();
    bool CreateDamper();
    bool CreateConstForce();
    bool CreateSlipVibration();
    bool CreateRoadTexture();
    bool CreateCollision();
    bool CreateCurb();
    bool CreateScrub();

    // Per-frame update helpers
    void UpdateSpring(float speedNorm, float understeerFactor);
    void UpdateDamper(float boost);
    void UpdateConstForce(float lateralNorm, float lateralSign);
    void UpdateSlipVibration(float slipAmount);
    void UpdateRoadTexture(float speedNorm, float lateralNorm, float speedMps);
    void UpdateCurb(float curbImpact);
    void UpdateScrub(float scrubAmt);

    // Compute FH5 speed-based spring scale.
    // Linear interpolation: [speed0,scale0] → [speed1,scale1].
    // FH5: speed0=2 m/s → 15%, speed1=15 m/s → 100%
    static float FH5SpeedScale(float speedMps);

    // AC: apply MIN_FF offset to any non-zero force
    static LONG  ApplyMinFF(LONG rawMag, int minForcePct);

    // Scale normalized 0-1 to DI magnitude units (0-10000)
    static LONG  ScaleMag(float norm, int configPct);

    IDirectInputDevice8A* m_pDev  = nullptr;
    bool                  m_ready = false;

    IDirectInputEffect*   m_pSpring    = nullptr;
    IDirectInputEffect*   m_pDamper    = nullptr;
    IDirectInputEffect*   m_pConstF    = nullptr;
    IDirectInputEffect*   m_pSlip      = nullptr;
    IDirectInputEffect*   m_pRoad      = nullptr;
    IDirectInputEffect*   m_pCollision = nullptr;
    IDirectInputEffect*   m_pCurb      = nullptr;
    IDirectInputEffect*   m_pScrub     = nullptr;

    // Collision state
    float  m_prevSpeed        = 0.0f;
    bool   m_collisionActive  = false;
    DWORD  m_collisionEndTick = 0;

    // Rear slip assist smoothing (EMA)
    float  m_rearSlipSmoothed     = 0.0f;

    // Load transfer weight smoothing (EMA)
    float  m_loadTransferSmoothed = 0.0f;

    // Front grip feedback smoothing (EMA)
    float  m_scrubSmoothed              = 0.0f;
    float  m_understeerFeedbackSmoothed = 0.0f;

    // Front tire load SAT (EMA)
    float  m_frontLoadSmoothed  = 0.0f;

    // Center hold zone (EMA)
    float  m_centerHoldSmoothed = 0.0f;

    // Rack inertia / release torque (EMA)
    float  m_prevSteerVel           = 0.0f;
    float  m_rackInertiaSmoothed    = 0.0f;
    float  m_rackReleaseSmoothed    = 0.0f;

    // Straight-line stability micro-damp (EMA)
    float  m_straightLineDampSmoothed = 0.0f;

    // Road load oscillation (EMA)
    float  m_roadLoadOscSmoothed      = 0.0f;

    // Low speed hydraulic assist (EMA)
    float  m_lowSpeedAssistSmoothed   = 0.0f;

    // Engine idle vibration
    float  m_prevLongAccelForVib      = 0.0f;
    float  m_cutVibSmoothed           = 0.0f;
    float  m_engineVibFadeSmoothed    = 0.0f;

    // Caster return torque (EMA)
    float  m_casterReturnSmoothed   = 0.0f;

    // High-speed steering damping
    float  m_prevSteer              = 0.0f;
    float  m_highSpeedDampSmoothed  = 0.0f;
};
