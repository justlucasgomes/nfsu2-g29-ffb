#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include "telemetry.h"

// ─────────────────────────────────────────────────────────────────────────────
//  FFBEngine — five DirectInput force feedback effects.
//
//  Effect stack (inspired by Assetto Corsa / ETS2 FFB philosophy):
//  ┌──────────────────────────────────────────────────────────────────────┐
//  │ 1. Spring     — centering force scaled by speed²  (always active)   │
//  │ 2. Damper     — reduces oscillation               (always active)   │
//  │ 3. ConstForce — lateral load (cornering G) effect (race only)       │
//  │ 4. Vibration  — traction-loss rumble (Sine)        (race only)       │
//  │ 5. Collision  — impulse on impact   (Square)       (triggered)       │
//  └──────────────────────────────────────────────────────────────────────┘
//
//  All magnitudes are DI units: 0 = 0%, 10000 = 100% (DI_FFNOMINALMAX).
// ─────────────────────────────────────────────────────────────────────────────

class FFBEngine {
public:
    FFBEngine();
    ~FFBEngine();

    // Initialise all effects on the given device. Must call after G29Device::Init().
    bool Init(IDirectInputDevice8A* pDevice);
    void Shutdown();

    // Update all continuous effects from fresh telemetry.
    // Call every 10 ms from the telemetry thread.
    void Update(const TelemetryData& tele, float steeringInput);

    // Trigger a one-shot collision impulse.
    void TriggerCollision(float impact);   // impact: 0 – 1

    bool IsReady() const { return m_ready; }

private:
    bool CreateSpringEffect();
    bool CreateDamperEffect();
    bool CreateConstForceEffect();
    bool CreateVibrationEffect();
    bool CreateCollisionEffect();
    bool CreateRoadTextureEffect();

    void UpdateSpring(float speedNorm);
    void UpdateDamper();
    void UpdateConstForce(float lateralNorm, float lateralSign);
    void UpdateVibration(float wheelSpinMax);
    void SetEffectMagnitude(IDirectInputEffect* eff, LONG magnitude);

    // AC-inspired functions
    LONG ApplyMinimumForce(LONG rawForce);
    void UpdateRoadTexture(float speedNorm);
    void UpdateSlipVibration(float slipAmount);
    void UpdateCurbEffect(float curbImpact);
    void UpdateUndersteerEffect(float understeerFactor);

    // Helpers
    static LONG ScaleMag(float norm0to1, int configPct);

    IDirectInputDevice8A* m_pDevice   = nullptr;
    bool                  m_ready     = false;

    IDirectInputEffect*   m_pSpring    = nullptr;
    IDirectInputEffect*   m_pDamper    = nullptr;
    IDirectInputEffect*   m_pConstForce= nullptr;
    IDirectInputEffect*   m_pVibration = nullptr;
    IDirectInputEffect*   m_pCollision = nullptr;

    // Collision state
    DWORD   m_collisionEndTick  = 0;
    bool    m_collisionActive   = false;

    // Road texture effect (separate periodic on rumble motor / steering axis)
    IDirectInputEffect* m_pRoadTexture = nullptr;

    // Track previous speed for delta-based collision detection inside Update()
    float   m_prevSpeed    = 0.0f;
    // Track previous lateral accel to detect understeer onset
    float   m_prevLatAccel = 0.0f;
};
