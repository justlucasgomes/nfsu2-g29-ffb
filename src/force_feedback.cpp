// force_feedback.cpp
// Seven DirectInput FFB effects for the Logitech G29 on NFSU2.
//
// Speed curve derived from Forza Horizon 5 ControllerFFB.ini:
//   SpringScaleSpeed0=2  / SpeedScale0=0.15  (15% @ 7.2 km/h)
//   SpringScaleSpeed1=15 / SpeedScale1=1.0   (100% @ 54 km/h, then plateau)
//   InRaceSpringMaxForce=0.5                 (50% absolute cap)
//
// Enhancement values from Assetto Corsa controls.ini (G29 profile):
//   MIN_FF=0.05  CURBS=0.4  ROAD=0.5  UNDERSTEER=0  DAMPER_GAIN=1

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cmath>
#include <algorithm>
#include "force_feedback.h"
#include "config.h"
#include "logger.h"
#include "engine_curve.h"

constexpr LONG DI_MAX = DI_FFNOMINALMAX;  // 10000

// Axis: steering X only
static DWORD  s_axis = DIJOFS_X;
static LONG   s_dir  = 0;

// ─────────────────────────────────────────────────────────────────────────────

ForceFeedback& ForceFeedback::Get() {
    static ForceFeedback inst;
    return inst;
}

// ── Curva de peso dinâmico (três pontos) ─────────────────────────────────────
// Segmento 1: 0 → 10 m/s  ( 0 → 36 km/h)  low  → mid
// Segmento 2: 10 → 30 m/s (36 → 108 km/h) mid  → high
// Acima de 30 m/s: plato em high.
float ForceFeedback::FH5SpeedScale(float speedMps) {
    const float low  = g_Config.ffb.lowSpeedWeight  / 100.0f;
    const float mid  = g_Config.ffb.midSpeedWeight  / 100.0f;
    const float high = g_Config.ffb.highSpeedWeight / 100.0f;

    if (speedMps <= 0.0f)  return low;
    if (speedMps <= 10.0f) return low  + (mid  - low)  * (speedMps / 10.0f);
    if (speedMps <= 30.0f) return mid  + (high - mid)  * ((speedMps - 10.0f) / 20.0f);
    return high;
}

// ── AC: MIN_FF offset ──────────────────────────────────────────────────────────
LONG ForceFeedback::ApplyMinFF(LONG rawMag, int minForcePct) {
    if (rawMag == 0) return 0;
    LONG minOff = static_cast<LONG>(minForcePct / 100.0f * DI_MAX);
    LONG sign   = (rawMag > 0) ? 1 : -1;
    return std::min(static_cast<LONG>(DI_MAX),
                    std::abs(rawMag) + minOff) * sign;
}

LONG ForceFeedback::ScaleMag(float norm, int configPct) {
    float v = std::max(0.0f, std::min(1.0f, norm));
    return static_cast<LONG>(v * (configPct / 100.0f) * DI_MAX);
}

// ── Init ──────────────────────────────────────────────────────────────────────

bool ForceFeedback::Init(IDirectInputDevice8A* pDev) {
    if (!pDev) return false;
    m_pDev = pDev;

    DIDEVCAPS caps{ sizeof(DIDEVCAPS) };
    pDev->GetCapabilities(&caps);
    if (!(caps.dwFlags & DIDC_FORCEFEEDBACK)) {
        LOG_ERROR("FFB: device does not support force feedback");
        return false;
    }

    bool ok = true;
    ok &= CreateSpring();
    ok &= CreateDamper();
    ok &= CreateConstForce();
    ok &= CreateSlipVibration();
    ok &= CreateRoadTexture();
    ok &= CreateCollision();
    CreateCurb();       // non-fatal
    CreateScrub();      // non-fatal
    CreateShiftKick();  // non-fatal

    if (ok) {
        m_ready = true;
        LOG_INFO("FFB: 7 effects ready  (FH5 speed curve + AC enhancements)");
        LOG_INFO("FFB cfg: spring=%d speedWt=%d minFF=%d road=%d slip=%d curb=%d understeer=%d damper=%d",
                 g_Config.ffb.centerSpring, g_Config.ffb.speedWeight,
                 g_Config.ffb.minimumForce, g_Config.ffb.roadTexture,
                 g_Config.ffb.slipVibration, g_Config.ffb.curbEffect,
                 g_Config.ffb.understeerLightening, g_Config.ffb.damperStrength);
    } else {
        LOG_ERROR("FFB: init incomplete");
    }
    return ok;
}

// ── Effect factory helpers ────────────────────────────────────────────────────

static DIEFFECT BaseEffect(DWORD duration = INFINITE) {
    DIEFFECT e{};
    e.dwSize               = sizeof(DIEFFECT);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration           = duration;
    e.dwSamplePeriod       = 0;
    e.dwGain               = DI_MAX;
    e.dwTriggerButton      = DIEB_NOTRIGGER;
    e.cAxes                = 1;
    e.rgdwAxes             = &s_axis;
    e.rglDirection         = &s_dir;
    return e;
}

bool ForceFeedback::CreateSpring() {
    DICONDITION c{};
    c.lPositiveCoefficient = static_cast<LONG>(0.5 * DI_MAX);
    c.lNegativeCoefficient = static_cast<LONG>(0.5 * DI_MAX);
    c.dwPositiveSaturation = DI_MAX;
    c.dwNegativeSaturation = DI_MAX;
    c.lDeadBand            = static_cast<LONG>(0.02 * DI_MAX);

    auto e = BaseEffect();
    e.cbTypeSpecificParams  = sizeof(c);
    e.lpvTypeSpecificParams = &c;

    HRESULT hr = m_pDev->CreateEffect(GUID_Spring, &e, &m_pSpring, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: Spring 0x%08X", hr); return false; }
    m_pSpring->Start(1, 0);
    LOG_INFO("FFB: Spring created");
    return true;
}

bool ForceFeedback::CreateDamper() {
    // FH5: InRaceDampingMaxForce=0.31, DampingScale=0.33
    // → initial coefficient = 31% of DI_MAX
    DICONDITION c{};
    c.lPositiveCoefficient = static_cast<LONG>(0.31 * DI_MAX);
    c.lNegativeCoefficient = static_cast<LONG>(0.31 * DI_MAX);
    c.dwPositiveSaturation = DI_MAX;
    c.dwNegativeSaturation = DI_MAX;

    auto e = BaseEffect();
    e.cbTypeSpecificParams  = sizeof(c);
    e.lpvTypeSpecificParams = &c;

    HRESULT hr = m_pDev->CreateEffect(GUID_Damper, &e, &m_pDamper, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: Damper 0x%08X", hr); return false; }
    m_pDamper->Start(1, 0);
    LOG_INFO("FFB: Damper created");
    return true;
}

bool ForceFeedback::CreateConstForce() {
    DICONSTANTFORCE cf{ 0 };
    LONG dir = 0;

    auto e = BaseEffect();
    e.rglDirection          = &dir;
    e.cbTypeSpecificParams  = sizeof(cf);
    e.lpvTypeSpecificParams = &cf;

    HRESULT hr = m_pDev->CreateEffect(GUID_ConstantForce, &e, &m_pConstF, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: ConstForce 0x%08X", hr); return false; }
    m_pConstF->Start(1, 0);
    LOG_INFO("FFB: ConstantForce created");
    return true;
}

bool ForceFeedback::CreateSlipVibration() {
    // FH5: LeftVibrationWavelength=38ms → 26 Hz — road rumble
    DIPERIODIC p{ 0, 0, 0, 38000 };
    auto e = BaseEffect();
    e.cbTypeSpecificParams  = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    HRESULT hr = m_pDev->CreateEffect(GUID_Sine, &e, &m_pSlip, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: SlipVibration 0x%08X", hr); return false; }
    m_pSlip->Start(1, 0);
    LOG_INFO("FFB: SlipVibration (26 Hz Sine) created");
    return true;
}

bool ForceFeedback::CreateRoadTexture() {
    // FH5: RightVibrationWavelength=21ms → 47 Hz — road detail/texture
    DIPERIODIC p{ 0, 0, 0, 21000 };
    auto e = BaseEffect();
    e.cbTypeSpecificParams  = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    HRESULT hr = m_pDev->CreateEffect(GUID_Sine, &e, &m_pRoad, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: RoadTexture 0x%08X", hr); return false; }
    m_pRoad->Start(1, 0);
    LOG_INFO("FFB: RoadTexture (47 Hz Sine) created");
    return true;
}

bool ForceFeedback::CreateCollision() {
    DIPERIODIC p{ 0, 0, 0, 40000 };
    auto e = BaseEffect(200000);  // 200 ms
    e.cbTypeSpecificParams  = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    HRESULT hr = m_pDev->CreateEffect(GUID_Square, &e, &m_pCollision, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: Collision 0x%08X", hr); return false; }
    LOG_INFO("FFB: Collision (Square) created");
    return true;
}

bool ForceFeedback::CreateCurb() {
    // AC: CURBS=0.4 → 50 Hz square wave, 120 ms
    DIPERIODIC p{ 0, 0, 0, 20000 };
    auto e = BaseEffect(120000);
    e.cbTypeSpecificParams  = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    HRESULT hr = m_pDev->CreateEffect(GUID_Square, &e, &m_pCurb, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: Curb (non-fatal) 0x%08X", hr); return false; }
    LOG_INFO("FFB: Curb (50 Hz Square) created");
    return true;
}

bool ForceFeedback::CreateScrub() {
    // Front tire scrub: configurable Hz (default 32 Hz = 31250 µs)
    int hz = std::max(10, g_Config.ffb.scrubFrequency);
    DWORD period = static_cast<DWORD>(1000000 / hz);
    DIPERIODIC p{ 0, 0, 0, period };
    auto e = BaseEffect();
    e.cbTypeSpecificParams  = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    HRESULT hr = m_pDev->CreateEffect(GUID_Sine, &e, &m_pScrub, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: Scrub (non-fatal) 0x%08X", hr); return false; }
    m_pScrub->Start(1, 0);
    LOG_INFO("FFB: FrontScrub (%d Hz Sine) created", hz);
    return true;
}

bool ForceFeedback::CreateShiftKick() {
    DICONSTANTFORCE cf{ 0 };
    LONG dir = 0;

    DIEFFECT e{};
    e.dwSize                = sizeof(e);
    e.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration            = 30000;  // 30 ms default; overridden in TriggerShiftKick
    e.dwGain                = DI_MAX;
    e.dwTriggerButton       = DIEB_NOTRIGGER;
    e.cAxes                 = 1;
    e.rgdwAxes              = &s_axis;
    e.rglDirection          = &dir;
    e.cbTypeSpecificParams  = sizeof(cf);
    e.lpvTypeSpecificParams = &cf;

    HRESULT hr = m_pDev->CreateEffect(GUID_ConstantForce, &e, &m_pShiftKick, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: ShiftKick (non-fatal) 0x%08X", hr); return false; }
    LOG_INFO("FFB: ShiftKick (ConstantForce) created");
    return true;
}

// ── Update (called every 10 ms) ───────────────────────────────────────────────

void ForceFeedback::Update(const TelemetryData& tele, float steeringInput) {
    if (!m_ready) return;
    const auto& cfg = g_Config.ffb;

    // ── Per-car physics scaling ───────────────────────────────────────────────
    // tele.physics is filled each frame by Telemetry::Read() from car_physics.cpp
    // registry, keyed on the auto-resolved carId.
    //
    // physFrontSlipThresh / physRearSlipThresh
    //   config value divided by grip — higher grip = lower threshold.
    //   Grippy cars react to slip earlier; loose cars react later.
    //   Clamped so extreme grip values don't produce unusable thresholds.
    //
    // physLoadScale: mass / 1300 kg (reference car mass).
    //   Heavier car → spring stiffens more under lateral G (more pronounced weight transfer).
    //
    // physUndersteerScale / physOversteerScale: 1 / grip.
    //   Less front grip → understeer feedback stronger. Less rear grip → oversteer assist stronger.
    //
    // Falls back to neutral (grip=1.0, mass=1200) when carId is unresolved.
    const float physFrontGrip       = std::max(0.1f, tele.physics.frontGrip);
    const float physRearGrip        = std::max(0.1f, tele.physics.rearGrip);
    const float physFrontSlipThresh = std::clamp(cfg.frontSlipThreshold / physFrontGrip,
                                                  0.20f, 0.45f);
    const float physRearSlipThresh  = std::clamp(cfg.rearSlipThreshold  / physRearGrip,
                                                  0.10f, 0.30f);
    const float physLoadScale       = std::clamp(tele.physics.mass / 1300.0f, 0.85f, 1.25f);
    const float physUndersteerScale = std::clamp(1.0f / physFrontGrip, 0.85f, 1.20f);
    const float physOversteerScale  = std::clamp(1.0f / physRearGrip,  0.85f, 1.20f);

    // Log once per car change (static — no extra member variable needed)
    {
        static uint32_t s_loggedId = UINT32_MAX;
        if (tele.physics.id != s_loggedId) {
            s_loggedId = tele.physics.id;
            LOG_INFO("FFB physics: mass=%.0f frontGrip=%.2f rearGrip=%.2f",
                     tele.physics.mass, tele.physics.frontGrip, tele.physics.rearGrip);
            LOG_INFO("FFB thresholds: front=%.2f rear=%.2f load=%.2f steer=%.0f",
                     physFrontSlipThresh, physRearSlipThresh, physLoadScale,
                     tele.physics.steeringLock);
        }
    }

    // ── 1. Spring — peso dinâmico de três pontos ──────────────────────────────
    // FH5SpeedScale() retorna 0.20→0.50→0.75 baseado na velocidade.
    // Understeer alivia o spring para dar feedback de falta de aderência.
    float dynWeight = FH5SpeedScale(tele.speed);

    // SAT spring coefficient: computed here so the spring budget can use it.
    // Physically: pneumatic trail stiffens steering proportional to speed².
    // Previously this lived in ConstantForce (incorrect channel).
    float satGain   = (g_Config.ffb.centerSpring / 100.0f) * 0.5f;
    float satSpring = std::fabsf(steeringInput) * tele.speedNorm * tele.speedNorm * satGain;

    // Rear slip magnitude (0..1) — computed early, used for both spring lightening
    // and the corrective torque (Rear Slip Assist) below.
    float rearSlipNorm = 0.0f;
    {
        float minSpeedNorm = (cfg.rearSlipMinSpeed / 3.6f) / g_Config.telemetry.maxSpeedMps;
        if (tele.wheelSpinMax > physRearSlipThresh && tele.speedNorm > minSpeedNorm)
            rearSlipNorm = std::min(1.0f, (tele.wheelSpinMax - physRearSlipThresh)
                                        / (1.0f - physRearSlipThresh));
    }

    // Front grip indicator: abs(steerAngle) * lateralNorm — 0..1 product.
    // High = tires working hard (scrub zone). Very high = front saturated (understeer).
    float frontSlip = std::fabsf(tele.steerAngle) * tele.lateralNorm;

    // Progressive Grip Transition: smooth blend factor that gates scrub and
    // understeer reduction in the 0.7-1.0 near-limit zone. Below 0.7 → 0 (no extra
    // gating), at 1.0 → full weight. Cubic smoothstep avoids velocity discontinuity.
    auto smoothstepF = [](float e0, float e1, float x) -> float {
        float t = std::max(0.0f, std::min(1.0f, (x - e0) / (e1 - e0)));
        return t * t * (3.0f - 2.0f * t);
    };
    float gripBlend = smoothstepF(0.7f, 1.0f, frontSlip);

    // Front scrub vibration (smoothed target, applied via UpdateScrub below)
    float scrubTarget = 0.0f;
    if (frontSlip > physFrontSlipThresh && tele.speedNorm > 0.05f) {
        float mag = (frontSlip - physFrontSlipThresh) / (1.0f - physFrontSlipThresh);
        scrubTarget = std::min(mag * (cfg.scrubGain / 100.0f), cfg.scrubMax / 100.0f);
    }
    m_scrubSmoothed += 0.25f * (scrubTarget - m_scrubSmoothed);

    // Understeer feedback: additional spring lightening when front saturates.
    float usFeedbackTarget = 0.0f;
    if (frontSlip > cfg.understeerFeedbackThreshold && tele.speedNorm > 0.05f) {
        float mag = (frontSlip - cfg.understeerFeedbackThreshold)
                  / (1.0f - cfg.understeerFeedbackThreshold);
        usFeedbackTarget = std::min(mag, 1.0f);
    }
    m_understeerFeedbackSmoothed += 0.20f * (usFeedbackTarget - m_understeerFeedbackSmoothed);

    // Existing understeer detection (low lateral response while turning)
    float understeerF = 0.0f;
    if (std::fabsf(steeringInput) > 0.15f && tele.lateralNorm < 0.3f && tele.speedNorm > 0.08f) {
        understeerF = std::fabsf(steeringInput) * (1.0f - tele.lateralNorm / 0.3f);
        understeerF = std::min(1.0f, understeerF);
    }

    // Combine both understeer factors, scale by front-grip inverse.
    // Cars with less front grip produce stronger understeer feedback signal.
    float totalUndersteer = (understeerF
        + m_understeerFeedbackSmoothed * (cfg.understeerMaxReduction / 100.0f))
        * physUndersteerScale;
    totalUndersteer = std::min(1.0f, totalUndersteer);

    // ── Spring Budget System ──────────────────────────────────────────────────
    // Each component is capped to its allocated budget slice. This prevents
    // any single source from silently saturating the spring and masking the
    // others — every effect is perceptible up to its maximum allocation.
    //   Speed curve      → 50 %  (FH5 3-point curve, foundation)
    //   SAT              → 10 %  (pneumatic trail — moved from ConstantForce)
    //   Longitudinal LT  → 10 %  (braking loads front axle → heavier steering)
    //   Lateral LT       → 15 %  (cornering weight transfer)
    //   Front tire load  → 10 %  (lateral G on front tires — pneumatic trail)
    //   Caster return    → 10 %  (return-to-center pull at speed)
    //   Center hold      →  5 %
    constexpr float kB_Speed    = 0.50f;
    constexpr float kB_SAT      = 0.10f;
    constexpr float kB_LongLoad = 0.10f;
    constexpr float kB_LatLoad  = 0.15f;
    constexpr float kB_FrontLoad= 0.10f;
    constexpr float kB_Caster   = 0.10f;
    constexpr float kB_Center   = 0.05f;

    // Speed curve + understeer fade (applies to speed slice only)
    float understeerFade = gripBlend * totalUndersteer * (cfg.understeerLightening / 100.0f);
    float springSpeed    = std::min(dynWeight * (1.0f - understeerFade), kB_Speed);

    // SAT: stiffness proportional to steer angle × speed² (computed at top of Update)
    float springSAT      = std::min(satSpring, kB_SAT);

    // Longitudinal load transfer: braking (longAccel < 0) loads front axle
    {
        float brakeG     = std::max(0.0f, -tele.longAccel / 9.81f);
        float longTarget = std::min(1.0f, brakeG) * (cfg.longLoadStrength / 100.0f);
        m_longLoadSmoothed += 0.20f * (longTarget - m_longLoadSmoothed);
    }
    float springLongLT   = std::min(m_longLoadSmoothed, kB_LongLoad);

    // Lateral load transfer (EMA updated in section 3 — 1-frame lag, negligible at 10ms)
    float springLatLT    = std::min(m_loadTransferSmoothed, kB_LatLoad);

    // Front tire load (inline EMA)
    {
        float loadTarget = std::powf(tele.lateralNorm, 1.5f) * (cfg.frontLoadStrength / 100.0f);
        m_frontLoadSmoothed += 0.20f * (loadTarget - m_frontLoadSmoothed);
    }
    float springFL       = std::min(m_frontLoadSmoothed, kB_FrontLoad);

    // Caster return torque (inline EMA)
    {
        float steerAbs     = std::fabsf(steeringInput);
        float speedF       = std::min(1.0f, tele.speed / 50.0f);
        float casterTarget = steerAbs * speedF * (cfg.casterReturnStrength / 100.0f);
        m_casterReturnSmoothed += 0.20f * (casterTarget - m_casterReturnSmoothed);
    }
    float springCaster   = std::min(m_casterReturnSmoothed, kB_Caster);

    // Center hold zone (inline EMA)
    {
        float absS     = std::fabsf(steeringInput);
        float chTarget = 0.0f;
        if (absS < cfg.centerHoldRange) {
            float normalized     = absS / std::max(cfg.centerHoldRange, 0.001f);
            float speedF         = std::min(1.0f, tele.speed / 50.0f);
            float highSpeedRelax = std::min(1.0f, std::max(0.0f, (tele.speed - 35.0f) / 25.0f));
            float loadFade       = std::min(1.0f, std::max(0.0f, (tele.lateralNorm - 0.30f) / 0.70f));
            float boost          = cfg.centerHoldHighSpeedBoost
                                 * (1.0f - 0.15f * highSpeedRelax)
                                 * (1.0f - 0.20f * loadFade);
            chTarget = (1.0f - normalized * normalized) * (cfg.centerHoldStrength + speedF * boost);
        }
        m_centerHoldSmoothed += cfg.centerHoldSmooth * (chTarget - m_centerHoldSmoothed);
    }
    float springCenter   = std::min(m_centerHoldSmoothed, kB_Center);

    // Assemble budgeted spring
    float finalSpring = springSpeed + springSAT + springLongLT + springLatLT
                      + springFL + springCaster + springCenter;

    // Global modifiers — scale the assembled total, not individual slices
    {
        float timeS     = GetTickCount() * 0.001f;
        float speedF    = std::min(1.0f, tele.speed / 50.0f);
        float oscTarget = std::sinf(timeS * 50.0f) * 0.02f * speedF;
        m_roadLoadOscSmoothed += 0.30f * (oscTarget - m_roadLoadOscSmoothed);
    }
    {
        constexpr float kFullAssistBelowMps = 15.0f / 3.6f;
        float lsTarget = std::max(0.0f, 1.0f - tele.speed / kFullAssistBelowMps)
                       * (cfg.lowSpeedAssistStrength / 100.0f);
        m_lowSpeedAssistSmoothed += 0.15f * (lsTarget - m_lowSpeedAssistSmoothed);
    }
    finalSpring *= (1.0f - m_lowSpeedAssistSmoothed);
    finalSpring *= (1.0f - rearSlipNorm * (cfg.rearLightenStrength / 100.0f));
    finalSpring  = std::max(0.05f, finalSpring + m_roadLoadOscSmoothed);
    UpdateSpring(finalSpring, understeerF);

    // ── 2. Damper — boost proporcional à velocidade angular do volante ───────
    // steerVelocity: quanto o esterço mudou por segundo (loop = 10 ms).
    // normVel: normalizado pelo limite de 3 lock/s (giro rápido = ~3 em pânico).
    // speedFactor: 0 em repouso → 1 acima de 55 m/s (198 km/h).
    {
        constexpr float DT      = 0.01f;  // 10 ms tick
        constexpr float MAX_VEL = 3.0f;   // lock-units/s at which high-speed boost saturates
        float steerVel  = std::fabsf(steeringInput - m_prevSteer) / DT;
        m_prevSteer     = steeringInput;

        // High-speed damping: opposes fast inputs at highway speed
        float normVel    = std::min(steerVel / MAX_VEL, 1.0f);
        float speedF     = std::min(1.0f, tele.speed / 55.0f);
        float dampTarget = normVel * speedF * (cfg.highSpeedDampingStrength / 100.0f);
        m_highSpeedDampSmoothed += 0.30f * (dampTarget - m_highSpeedDampSmoothed);

        // Rack inertia: brief resistance proportional to angular acceleration.
        // Simulates rack/column mass — felt at turn-in, gone once speed stabilizes.
        float steerAccel    = std::fabsf(steerVel - m_prevSteerVel) / DT;
        float steerVelDrop  = std::max(0.0f, m_prevSteerVel - steerVel);
        m_prevSteerVel      = steerVel;
        float accelNorm     = std::min(1.0f, steerAccel / 8.0f);
        float inertiaTarget = accelNorm * (cfg.rackInertiaStrength / 100.0f);
        m_rackInertiaSmoothed += 0.25f * (inertiaTarget - m_rackInertiaSmoothed);

        // Rack release: brief damper reduction on fast deceleration of the wheel.
        // Simulates the rack unloading — makes unwinding feel fluid, not sticky.
        float releaseNorm   = std::min(1.0f, steerVelDrop / 3.0f);
        float releaseTarget = releaseNorm * (cfg.rackReleaseStrength / 100.0f);
        m_rackReleaseSmoothed += 0.20f * (releaseTarget - m_rackReleaseSmoothed);

        // Straight-line stability: micro-damp active only near center + low lateral G.
        // Filters road crown / surface noise without affecting cornering response.
        float slTarget = 0.0f;
        if (std::fabsf(steeringInput) < 0.03f && tele.lateralNorm < 0.15f)
            slTarget = steerVel * (cfg.straightLineStability / 100.0f);
        m_straightLineDampSmoothed += 0.15f * (slTarget - m_straightLineDampSmoothed);
    }
    float finalDamper = m_highSpeedDampSmoothed + m_rackInertiaSmoothed
                      - m_rackReleaseSmoothed   + m_straightLineDampSmoothed;
    UpdateDamper(std::max(0.0f, finalDamper));

    // ── 3. Lateral G ──────────────────────────────────────────────────────────
    // SAT moved to Spring budget (section 1) — correct physical channel.
    // ConstantForce now carries only offset forces: lateral G + rear slip assist.
    float latSign  = (tele.lateralAccel >= 0.0f) ? -1.0f : 1.0f;
    float latForce = tele.lateralNorm * latSign * (cfg.lateralForce / 100.0f);

    // Rear Slip Assist: torque de contra-esterço suavizado via EMA.
    // Ativa quando wheelSpinMax > threshold E velocidade >= rearSlipMinSpeed.
    float rearSlipTarget = 0.0f;
    if (cfg.rearSlipAssistStrength > 0 && rearSlipNorm > 0.0f) {
        float slipSign  = (tele.lateralAccel >= 0.0f) ? 1.0f : -1.0f;
        rearSlipTarget  = slipSign * rearSlipNorm * tele.speedNorm
                        * (cfg.rearSlipAssistStrength / 100.0f) * physOversteerScale;
        float maxT = cfg.rearSlipMaxTorque / 100.0f;
        rearSlipTarget  = std::max(-maxT, std::min(maxT, rearSlipTarget));
    }
    m_rearSlipSmoothed += 0.2f * (rearSlipTarget - m_rearSlipSmoothed);

    // Load Transfer Weight: enrijece o spring proporcional ao G lateral (EMA).
    float loadTarget = std::fabsf(tele.lateralNorm) * (cfg.loadTransferGain / 100.0f) * physLoadScale;
    float maxLT      = cfg.loadTransferMax / 100.0f;
    loadTarget       = std::min(loadTarget, maxLT);
    m_loadTransferSmoothed += cfg.loadTransferSmooth * (loadTarget - m_loadTransferSmoothed);

    // ── Engine Idle Vibration ─────────────────────────────────────────────────
    // rpmNorm drives frequency and amplitude.
    //
    // Source priority:
    //   1. tele.rpm > 0  → real RPM (OFS_RPM configured in nfsu2_addresses.h)
    //   2. fallback      → longAccel / 8.0f proxy (active while OFS_RPM == 0)
    //
    // Per-car RPM range from config.ini [Engine] section.
    // Export idle_rpm / redline_rpm from nfsu2-car-tuning Python project and set here.
    // Defaults (800 / 8000) are used when values are not car-specific.
    // RPM range: prefer loaded curve, fall back to config.ini [Engine] values.
    const EngineCurve& curve = GetEngineCurve();
    const float kIdleRpm    = curve.loaded
                              ? curve.idleRpm
                              : std::max(100.0f, static_cast<float>(cfg.idleRpm));
    const float kRedlineRpm = curve.loaded
                              ? curve.redlineRpm
                              : std::max(kIdleRpm + 100.0f, static_cast<float>(cfg.redlineRpm));

    float rpmNorm;
    if (tele.rpm > 0.0f) {
        rpmNorm = std::max(0.0f, std::min(1.0f,
                  (tele.rpm - kIdleRpm) / (kRedlineRpm - kIdleRpm)));
    } else {
        // Fallback: positive longAccel = throttle applied ≈ engine above idle
        rpmNorm = std::max(0.0f, std::min(1.0f, tele.longAccel / 8.0f));
    }

    float engineVib = 0.0f;
    if (cfg.enableEngineIdleVibration && tele.playerCarValid) {
        float speedKmh  = tele.speed * 3.6f;
        float threshold = std::max(1.0f, static_cast<float>(cfg.idleSpeedThresholdKmh));
        float speedFade = std::max(0.0f, 1.0f - speedKmh / threshold);

        if (speedFade > 0.0f) {
            float timeS = GetTickCount() * 0.001f;

            // Frequency from 2nd engine order (dominant in 4-cylinder engines):
            //   f = 2 × RPM / 60  →  800 RPM → 26.7 Hz, plateau at ~50 Hz from ~1500 RPM
            //   G29 FFB bandwidth caps at ~50 Hz; above that energy is lost in the motor.
            // With real RPM: physically accurate.
            // With fallback rpmNorm: linear approximation, same 25-55 Hz range.
            float idleFreq;
            if (tele.rpm > 0.0f)
                idleFreq = std::max(20.0f, std::min(50.0f, 2.0f * tele.rpm / 60.0f));
            else
                idleFreq = 25.0f + rpmNorm * 30.0f;

            // ── Harmonic 1 — base idle oscillation ───────────────────────────
            // Always present at idle. Amplitude is flat (not RPM-scaled) so
            // the driver feels the motor even when not accelerating.
            float ampBase = 0.18f * (cfg.revVibrationStrength / 100.0f);

            // ── Harmonic 2 — torque × load oscillation ────────────────────────
            // torqueNorm: where on the torque curve the engine is (peak ~4000 RPM).
            //   Falls off near redline even at high RPM — engine "thinning out".
            // loadFactor: how hard the engine is pulling, from longAccel proxy.
            //   0.25 = engine braking / coasting   1.0 = full throttle load
            // Fallback when tele.rpm == 0: rpmNorm replaces torqueNorm.
            float torqueNorm = (tele.rpm > 0.0f)
                               ? EstimateTorqueNormForCar(tele.rpm)
                               : rpmNorm;
            float loadFactor = std::max(0.25f,
                               std::min(1.0f, 0.4f + tele.longAccel / 6.0f));
            float ampLoad    = torqueNorm * loadFactor
                             * (cfg.revVibrationStrength / 100.0f);

            // ── Free-rev amplitude ────────────────────────────────────────────
            // ampLoad is near-zero in neutral because loadFactor floors at 0.25 and
            // torqueNorm is low at idle RPM. Without freeRevAmp the second harmonic
            // would be inaudible when the driver revs the engine in neutral.
            // freeRevAmp rises linearly with rpmNorm so the driver feels the
            // tachometer climbing even without drivetrain load.
            float freeRevAmp = rpmNorm * 0.55f * (cfg.revVibrationStrength / 100.0f);

            // ── Throttle cut — transient on fundamental ───────────────────────
            float longDrop  = std::max(0.0f, m_prevLongAccelForVib - tele.longAccel);
            float cutTarget = std::min(1.0f, longDrop / 4.0f)
                            * (cfg.cutVibrationStrength / 100.0f);
            m_cutVibSmoothed += 0.20f * (cutTarget - m_cutVibSmoothed);

            // ── Combine harmonics ─────────────────────────────────────────────
            float freq2     = idleFreq * 1.8f;
            float harmonic1 = std::sinf(timeS * idleFreq) * (ampBase + m_cutVibSmoothed);
            float harmonic2 = std::sinf(timeS * freq2)    * (ampLoad + freeRevAmp);

            m_engineVibFadeSmoothed += 0.15f * (speedFade - m_engineVibFadeSmoothed);
            engineVib = (harmonic1 + harmonic2) * m_engineVibFadeSmoothed;

            // Periodic amplitude snapshot — debug only (LogLevel=3)
            {
                static int s_engTick = 0;
                if (++s_engTick >= 100) {
                    s_engTick = 0;
                    LOG_DEBUG("FFB engine: base=%.2f freeRev=%.2f load=%.2f total=%.2f",
                              ampBase, freeRevAmp, ampLoad,
                              ampBase + freeRevAmp + ampLoad + m_cutVibSmoothed);
                }
            }
        } else {
            m_engineVibFadeSmoothed += 0.15f * (0.0f - m_engineVibFadeSmoothed);
        }
        m_prevLongAccelForVib = tele.longAccel;
    }

    // ── Center weight ─────────────────────────────────────────────────────────
    // Adds a light centering pull to ConstantForce that simulates caster feel
    // and steering column weight at speed.
    //
    // centerWeight = (1 - |steer|) × 0.18 × speedFactor
    //   At zero steer:  (1-0)×0.18×sf = 0.18×sf, but (-steer)=0 → net force = 0.
    //   At slight steer: non-zero pull toward center, magnitude grows with speed.
    //   At full lock:   (1-1)×... = 0 → no artificial resistance at full lock.
    //
    // speedFactor saturates at 40 km/h so effect is felt in city driving.
    // Fully absent at standstill — no artificial weight while parking.
    {
        float cwSpeedFactor = std::clamp(tele.speed * 3.6f / 40.0f, 0.0f, 1.0f);
        float centerWeight  = (1.0f - std::min(1.0f, std::fabsf(steeringInput)))
                            * 0.18f * cwSpeedFactor;
        float centerForce   = centerWeight * (-steeringInput);

        float combined = std::max(-1.0f, std::min(1.0f,
                         latForce + m_rearSlipSmoothed + engineVib + centerForce));
        LONG  cfMag    = ApplyMinFF(static_cast<LONG>(combined * DI_MAX), 0);
        if (m_pConstF) {
            DICONSTANTFORCE cf{ cfMag };
            DIEFFECT e{};
            e.dwSize = sizeof(e); e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
            e.cbTypeSpecificParams = sizeof(cf); e.lpvTypeSpecificParams = &cf;
            m_pConstF->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
        }

        // Center weight snapshot — debug only (LogLevel=3)
        static int s_cwTick = 0;
        if (++s_cwTick >= 100) {
            s_cwTick = 0;
            LOG_DEBUG("FFB centerWeight: speed=%.0f center=%.2f",
                      tele.speed * 3.6f, centerWeight);
        }
    }

    // ── 4. Slip vibration ─────────────────────────────────────────────────────
    float slip = tele.slipAngle > 0.0f
        ? std::min(1.0f, tele.slipAngle / 15.0f)
        : tele.wheelSpinMax;
    UpdateSlipVibration(slip);

    // ── 5. Road texture + rack kick ───────────────────────────────────────────
    UpdateRoadTexture(tele.speedNorm, tele.lateralNorm, tele.speed);

    // ── 5b. Front tire scrub ──────────────────────────────────────────────────
    UpdateScrub(m_scrubSmoothed);

    // ── 6. Collision ──────────────────────────────────────────────────────────
    float dSpeed = m_prevSpeed - tele.speed;
    m_prevSpeed  = tele.speed;
    if (dSpeed > cfg.collisionThreshold && !m_collisionActive) {
        float impact = std::min(1.0f, dSpeed / (cfg.collisionThreshold * 6.0f));
        TriggerCollision(impact);
    }
    if (m_collisionActive && GetTickCount() >= m_collisionEndTick) {
        m_collisionActive = false;
        if (m_pCollision) m_pCollision->Stop();
    }

    // Shift kick expiry
    if (m_shiftKickActive && GetTickCount() >= m_shiftKickEndTick) {
        m_shiftKickActive = false;
        if (m_pShiftKick) m_pShiftKick->Stop();
    }

    // ── 7. Curb ───────────────────────────────────────────────────────────────
    // Detectar lateralAccel spike (zebra/calçada = pico de aceleração lateral)
    float curbImpact = 0.0f;
    if (std::fabsf(tele.longAccel) > 15.0f)
        curbImpact = std::min(1.0f, (std::fabsf(tele.longAccel) - 15.0f) / 35.0f);
    UpdateCurb(curbImpact);
}

// ── Update helpers ────────────────────────────────────────────────────────────

void ForceFeedback::UpdateSpring(float springNorm, float /*unused*/) {
    if (!m_pSpring) return;
    LONG coeff = ApplyMinFF(
        static_cast<LONG>(springNorm * DI_MAX),
        g_Config.ffb.minimumForce);

    DICONDITION c{};
    c.lPositiveCoefficient = coeff;
    c.lNegativeCoefficient = coeff;
    c.dwPositiveSaturation = DI_MAX;
    c.dwNegativeSaturation = DI_MAX;
    c.lDeadBand = static_cast<LONG>(g_Config.input.steeringDeadzone * DI_MAX);

    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.cbTypeSpecificParams = sizeof(c);
    e.lpvTypeSpecificParams = &c;
    m_pSpring->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

void ForceFeedback::UpdateDamper(float boost) {
    if (!m_pDamper) return;
    // FH5: InRaceDampingScale=0.33 × DampingMaxForce=0.31 ≈ 10% base
    float fh5D  = 0.33f * 0.31f;
    float total = fh5D + (g_Config.ffb.damperStrength / 100.0f) * (1.0f - fh5D) + boost;
    LONG coeff  = static_cast<LONG>(std::min(total, 1.0f) * DI_MAX);

    DICONDITION c{};
    c.lPositiveCoefficient = coeff;
    c.lNegativeCoefficient = coeff;
    c.dwPositiveSaturation = DI_MAX;
    c.dwNegativeSaturation = DI_MAX;

    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.cbTypeSpecificParams = sizeof(c);
    e.lpvTypeSpecificParams = &c;
    m_pDamper->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

void ForceFeedback::UpdateConstForce(float lateralNorm, float lateralSign) {
    if (!m_pConstF) return;
    LONG mag = ApplyMinFF(ScaleMag(lateralNorm, g_Config.ffb.lateralForce), 0);
    mag = static_cast<LONG>(mag * lateralSign);

    DICONSTANTFORCE cf{ mag };
    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.cbTypeSpecificParams = sizeof(cf);
    e.lpvTypeSpecificParams = &cf;
    m_pConstF->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

void ForceFeedback::UpdateSlipVibration(float slipAmt) {
    if (!m_pSlip) return;
    DWORD mag = static_cast<DWORD>(ScaleMag(slipAmt, g_Config.ffb.slipVibration));

    DIPERIODIC p{ mag, 0, 0, 38000 };  // FH5: 26 Hz left motor
    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.cbTypeSpecificParams = sizeof(p);
    e.lpvTypeSpecificParams = &p;
    m_pSlip->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

void ForceFeedback::UpdateRoadTexture(float speedNorm, float lateralNorm, float speedMps) {
    if (!m_pRoad) return;
    // Base road texture: scales with speed² then filtered down at high speed
    // (saturates at 60 m/s = 216 km/h → 50% intensity), keeping the texture
    // readable without becoming harsh. Rack kick (cornerMag) is unaffected.
    float speedFilter = 1.0f - std::min(1.0f, speedMps / 60.0f) * 0.50f;
    DWORD speedMag  = static_cast<DWORD>(ScaleMag(speedNorm * speedNorm * speedFilter,
                                                    g_Config.ffb.roadTexture));
    DWORD cornerMag = static_cast<DWORD>(ScaleMag(lateralNorm,
                                                    g_Config.ffb.cornerTextureStrength));
    DWORD mag = std::min(static_cast<DWORD>(DI_MAX), speedMag + cornerMag);

    DIPERIODIC p{ mag, 0, 0, 21000 };  // FH5: 47 Hz right motor
    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.cbTypeSpecificParams = sizeof(p);
    e.lpvTypeSpecificParams = &p;
    m_pRoad->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

void ForceFeedback::UpdateCurb(float curbImpact) {
    if (!m_pCurb || m_collisionActive || curbImpact < 0.05f) return;

    LONG  mag = ScaleMag(curbImpact, g_Config.ffb.curbEffect);
    DWORD dur = static_cast<DWORD>(g_Config.ffb.curbPulseMs) * 1000; // µs

    DIPERIODIC p{ static_cast<DWORD>(mag), 0, 0, 20000 };
    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration           = dur;
    e.cbTypeSpecificParams = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    m_pCurb->SetParameters(&e, DIEP_TYPESPECIFICPARAMS | DIEP_DURATION);
    m_pCurb->Stop();
    m_pCurb->Start(1, 0);
}

void ForceFeedback::TriggerCollision(float impact) {
    if (!m_pCollision) return;

    LONG mag = ScaleMag(impact, g_Config.ffb.collisionForce);
    DWORD dur = static_cast<DWORD>(g_Config.ffb.collisionDurationMs) * 1000;

    DIPERIODIC p{ static_cast<DWORD>(mag), 0, 0, 40000 };
    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration           = dur;
    e.cbTypeSpecificParams = sizeof(p);
    e.lpvTypeSpecificParams = &p;

    m_pCollision->SetParameters(&e, DIEP_TYPESPECIFICPARAMS | DIEP_DURATION);
    m_pCollision->Stop();
    m_pCollision->Start(1, 0);

    m_collisionActive  = true;
    m_collisionEndTick = GetTickCount() + g_Config.ffb.collisionDurationMs;
    LOG_INFO("FFB: collision impulse mag=%ld dur=%ums", mag, g_Config.ffb.collisionDurationMs);
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void ForceFeedback::UpdateScrub(float scrubAmt) {
    if (!m_pScrub) return;
    int hz = std::max(10, g_Config.ffb.scrubFrequency);
    DWORD period = static_cast<DWORD>(1000000 / hz);
    DWORD mag = static_cast<DWORD>(ScaleMag(scrubAmt, 100));

    DIPERIODIC p{ mag, 0, 0, period };
    DIEFFECT e{};
    e.dwSize               = sizeof(e);
    e.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.cbTypeSpecificParams = sizeof(p);
    e.lpvTypeSpecificParams = &p;
    m_pScrub->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

void ForceFeedback::TriggerShiftKick(int gearDelta) {
    if (!m_pShiftKick || !g_Config.ffb.shiftKickEnabled) return;
    if (m_shiftKickActive) return;  // don't interrupt active kick

    const auto& cfg = g_Config.ffb;
    LONG mag = ScaleMag(1.0f, cfg.shiftKickStrength);
    // Upshift → brief forward nudge (negative = left on X axis).
    // Downshift → brief backward nudge (positive = right on X axis).
    LONG dir = (gearDelta > 0) ? -mag : mag;

    DICONSTANTFORCE cf{ dir };
    DWORD durUs = static_cast<DWORD>(cfg.shiftKickDurationMs) * 1000;

    DIEFFECT e{};
    e.dwSize                = sizeof(e);
    e.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration            = durUs;
    e.cbTypeSpecificParams  = sizeof(cf);
    e.lpvTypeSpecificParams = &cf;

    m_pShiftKick->SetParameters(&e, DIEP_TYPESPECIFICPARAMS | DIEP_DURATION);
    m_pShiftKick->Stop();
    m_pShiftKick->Start(1, 0);

    m_shiftKickActive  = true;
    m_shiftKickEndTick = GetTickCount() + static_cast<DWORD>(cfg.shiftKickDurationMs);

    LOG_DEBUG("FFB: shift kick gearDelta=%d mag=%ld dur=%ums", gearDelta, dir, cfg.shiftKickDurationMs);
}

void ForceFeedback::Shutdown() {
    auto rel = [](IDirectInputEffect*& p) {
        if (p) { p->Stop(); p->Release(); p = nullptr; }
    };
    rel(m_pSpring); rel(m_pDamper); rel(m_pConstF);
    rel(m_pSlip);   rel(m_pRoad);   rel(m_pCollision);
    rel(m_pCurb);   rel(m_pScrub);  rel(m_pShiftKick);
    m_ready = false;
    LOG_INFO("FFB: all effects released");
}
