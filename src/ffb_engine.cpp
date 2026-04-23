#include "ffb_engine.h"
#include "config.h"
#include "logger.h"
#include <cmath>
#include <algorithm>

// DirectInput magnitude constants
constexpr LONG DI_FFMAX = DI_FFNOMINALMAX;  // 10000

// ─────────────────────────────────────────────────────────────────────────────

FFBEngine::FFBEngine()  = default;
FFBEngine::~FFBEngine() { Shutdown(); }

// ── Init ─────────────────────────────────────────────────────────────────────

bool FFBEngine::Init(IDirectInputDevice8A* pDevice) {
    if (!pDevice) { LOG_ERROR("FFB: null device"); return false; }
    m_pDevice = pDevice;

    // Verify device supports FFB
    DIDEVCAPS caps{};
    caps.dwSize = sizeof(caps);
    pDevice->GetCapabilities(&caps);
    if (!(caps.dwFlags & DIDC_FORCEFEEDBACK)) {
        LOG_ERROR("FFB: device does not support force feedback!");
        return false;
    }

    bool ok = true;
    ok &= CreateSpringEffect();
    ok &= CreateDamperEffect();
    ok &= CreateConstForceEffect();
    ok &= CreateVibrationEffect();
    ok &= CreateCollisionEffect();
    // Road texture is a bonus effect — failure doesn't abort init
    CreateRoadTextureEffect();

    if (ok) {
        m_ready = true;
        LOG_INFO("FFB: all 6 effects created successfully (AC-inspired)");
    } else {
        LOG_ERROR("FFB: one or more effects failed to create");
    }

    LOG_INFO("FFB config: spring=%d speedWeight=%d minFF=%d road=%d slip=%d curb=%d understeer=%d",
             g_Config.ffb.centerSpring, g_Config.ffb.speedWeight, g_Config.ffb.minimumForce,
             g_Config.ffb.roadTexture, g_Config.ffb.slipVibration,
             g_Config.ffb.curbEffect, g_Config.ffb.understeerLightening);
    return ok;
}

// ── Effect creation helpers ────────────────────────────────────────────────

// Axis: steering wheel = X axis only (DIJOFS_X)
static DWORD     s_axes[1]       = { DIJOFS_X };
static LONG      s_dirs[1]       = { 0 };         // updated per effect

bool FFBEngine::CreateSpringEffect() {
    DICONDITION cond{};
    cond.lOffset              = 0;
    cond.lPositiveCoefficient = (LONG)(0.5 * DI_FFMAX);
    cond.lNegativeCoefficient = (LONG)(0.5 * DI_FFMAX);
    cond.dwPositiveSaturation = DI_FFMAX;
    cond.dwNegativeSaturation = DI_FFMAX;
    cond.lDeadBand            = (LONG)(0.02 * DI_FFMAX);  // matches config deadzone

    DIEFFECT eff{};
    eff.dwSize                = sizeof(DIEFFECT);
    eff.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration            = INFINITE;
    eff.dwSamplePeriod        = 0;
    eff.dwGain                = DI_FFMAX;
    eff.dwTriggerButton       = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes                 = 1;
    eff.rgdwAxes              = s_axes;
    eff.rglDirection          = s_dirs;
    eff.lpEnvelope            = nullptr;
    eff.cbTypeSpecificParams  = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = &cond;

    HRESULT hr = m_pDevice->CreateEffect(GUID_Spring, &eff, &m_pSpring, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: CreateEffect(Spring) 0x%08X", hr); return false; }
    m_pSpring->Start(1, 0);
    LOG_INFO("FFB: Spring effect created");
    return true;
}

bool FFBEngine::CreateDamperEffect() {
    DICONDITION cond{};
    cond.lPositiveCoefficient = (LONG)(0.4 * DI_FFMAX);
    cond.lNegativeCoefficient = (LONG)(0.4 * DI_FFMAX);
    cond.dwPositiveSaturation = DI_FFMAX;
    cond.dwNegativeSaturation = DI_FFMAX;
    cond.lDeadBand            = 0;

    DIEFFECT eff{};
    eff.dwSize                = sizeof(DIEFFECT);
    eff.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration            = INFINITE;
    eff.dwSamplePeriod        = 0;
    eff.dwGain                = DI_FFMAX;
    eff.dwTriggerButton       = DIEB_NOTRIGGER;
    eff.cAxes                 = 1;
    eff.rgdwAxes              = s_axes;
    eff.rglDirection          = s_dirs;
    eff.cbTypeSpecificParams  = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = &cond;

    HRESULT hr = m_pDevice->CreateEffect(GUID_Damper, &eff, &m_pDamper, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: CreateEffect(Damper) 0x%08X", hr); return false; }
    m_pDamper->Start(1, 0);
    LOG_INFO("FFB: Damper effect created");
    return true;
}

bool FFBEngine::CreateConstForceEffect() {
    DICONSTANTFORCE cf{};
    cf.lMagnitude = 0;

    LONG dir = 0;
    DIEFFECT eff{};
    eff.dwSize                = sizeof(DIEFFECT);
    eff.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration            = INFINITE;
    eff.dwSamplePeriod        = 0;
    eff.dwGain                = DI_FFMAX;
    eff.dwTriggerButton       = DIEB_NOTRIGGER;
    eff.cAxes                 = 1;
    eff.rgdwAxes              = s_axes;
    eff.rglDirection          = &dir;
    eff.cbTypeSpecificParams  = sizeof(DICONSTANTFORCE);
    eff.lpvTypeSpecificParams = &cf;

    HRESULT hr = m_pDevice->CreateEffect(GUID_ConstantForce, &eff, &m_pConstForce, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: CreateEffect(ConstForce) 0x%08X", hr); return false; }
    m_pConstForce->Start(1, 0);
    LOG_INFO("FFB: ConstantForce effect created");
    return true;
}

bool FFBEngine::CreateVibrationEffect() {
    // Sine wave for traction-loss rumble
    DIPERIODIC per{};
    per.dwMagnitude = 0;
    per.lOffset     = 0;
    per.dwPhase     = 0;
    per.dwPeriod    = 80000;  // 80 ms period = 12.5 Hz rumble

    DIEFFECT eff{};
    eff.dwSize                = sizeof(DIEFFECT);
    eff.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration            = INFINITE;
    eff.dwSamplePeriod        = 0;
    eff.dwGain                = DI_FFMAX;
    eff.dwTriggerButton       = DIEB_NOTRIGGER;
    eff.cAxes                 = 1;
    eff.rgdwAxes              = s_axes;
    eff.rglDirection          = s_dirs;
    eff.cbTypeSpecificParams  = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &per;

    HRESULT hr = m_pDevice->CreateEffect(GUID_Sine, &eff, &m_pVibration, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: CreateEffect(Vibration/Sine) 0x%08X", hr); return false; }
    m_pVibration->Start(1, 0);
    LOG_INFO("FFB: Vibration(Sine) effect created");
    return true;
}

bool FFBEngine::CreateRoadTextureEffect() {
    // High-frequency sine for road surface feel (separate from traction loss)
    DIPERIODIC per{};
    per.dwMagnitude = 0;
    per.dwPeriod    = 25000;  // 40 Hz

    DIEFFECT eff{};
    eff.dwSize                = sizeof(DIEFFECT);
    eff.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration            = INFINITE;
    eff.dwSamplePeriod        = 0;
    eff.dwGain                = DI_FFMAX;
    eff.dwTriggerButton       = DIEB_NOTRIGGER;
    eff.cAxes                 = 1;
    eff.rgdwAxes              = s_axes;
    eff.rglDirection          = s_dirs;
    eff.cbTypeSpecificParams  = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &per;

    HRESULT hr = m_pDevice->CreateEffect(GUID_Sine, &eff, &m_pRoadTexture, nullptr);
    if (FAILED(hr)) {
        LOG_ERROR("FFB: CreateEffect(RoadTexture) 0x%08X (non-fatal)", hr);
        return false;
    }
    m_pRoadTexture->Start(1, 0);
    LOG_INFO("FFB: RoadTexture effect created");
    return true;
}

bool FFBEngine::CreateCollisionEffect() {
    DIPERIODIC per{};
    per.dwMagnitude = 0;
    per.lOffset     = 0;
    per.dwPhase     = 0;
    per.dwPeriod    = 40000;  // 40 ms period = sharp square wave jolt

    DIEFFECT eff{};
    eff.dwSize                = sizeof(DIEFFECT);
    eff.dwFlags               = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration            = 200000;  // 200 ms default; overridden in trigger
    eff.dwSamplePeriod        = 0;
    eff.dwGain                = DI_FFMAX;
    eff.dwTriggerButton       = DIEB_NOTRIGGER;
    eff.cAxes                 = 1;
    eff.rgdwAxes              = s_axes;
    eff.rglDirection          = s_dirs;
    eff.cbTypeSpecificParams  = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &per;

    HRESULT hr = m_pDevice->CreateEffect(GUID_Square, &eff, &m_pCollision, nullptr);
    if (FAILED(hr)) { LOG_ERROR("FFB: CreateEffect(Collision/Square) 0x%08X", hr); return false; }
    LOG_INFO("FFB: Collision(Square) effect created");
    return true;
}

// ── Update  ───────────────────────────────────────────────────────────────────

LONG FFBEngine::ScaleMag(float norm0to1, int configPct) {
    float clamped = std::max(0.0f, std::min(1.0f, norm0to1));
    return static_cast<LONG>(clamped * (configPct / 100.0f) * DI_FFMAX);
}

void FFBEngine::Update(const TelemetryData& tele, float steeringInput) {
    if (!m_ready) return;

    const auto& cfg = g_Config.ffb;

    // ── 1. Spring — centering force, speed² curve (AC: FF_GAIN + speed scaling) ─
    float springFactor = tele.speedNorm * tele.speedNorm;
    float basePct      = cfg.centerSpring / 100.0f;
    float speedPct     = cfg.speedWeight  / 100.0f;
    float finalSpring  = basePct + springFactor * speedPct * (1.0f - basePct);

    // AC: UNDERSTEER — detect front grip loss (lateral accel drops while steering)
    float understeerFactor = 0.0f;
    if (std::fabsf(steeringInput) > 0.2f && tele.lateralNorm < 0.25f && tele.speedNorm > 0.1f) {
        // Steering applied but low lateral response = understeer
        understeerFactor = std::fabsf(steeringInput) * (1.0f - tele.lateralNorm / 0.25f);
        understeerFactor = std::min(1.0f, understeerFactor);
    }
    float understeerReduction = understeerFactor * (cfg.understeerLightening / 100.0f);
    finalSpring = std::max(0.1f, finalSpring - understeerReduction);

    UpdateSpring(finalSpring);
    if (understeerFactor > 0.05f) UpdateUndersteerEffect(understeerFactor);

    // ── 2. Damper — stabilizes steering (AC: DAMPER_GAIN) ────────────────────
    UpdateDamper();

    // ── 3. Constant force — lateral load (AC: cornering G) ───────────────────
    float latSign = (tele.lateralAccel > 0) ? -1.0f : 1.0f;
    UpdateConstForce(tele.lateralNorm, latSign);

    // ── 4. Slip Vibration — traction loss (AC: SLIPS) ────────────────────────
    UpdateSlipVibration(tele.wheelSpinMax);

    // ── 5. Road Texture — surface micro-vibration (AC: ROAD=0.5) ─────────────
    UpdateRoadTexture(tele.speedNorm);

    // ── 6. Collision / Curb — auto-detect from speed delta ───────────────────
    float speedDelta = m_prevSpeed - tele.speed;
    m_prevSpeed = tele.speed;

    if (speedDelta > cfg.collisionThreshold && !m_collisionActive) {
        float impact = std::min(1.0f, speedDelta / (cfg.collisionThreshold * 8.0f));
        LOG_DEBUG("FFB: auto-collision detected delta=%.2f impact=%.2f", speedDelta, impact);
        TriggerCollision(impact);
    }

    // Clear expired collision / curb
    if (m_collisionActive && GetTickCount() >= m_collisionEndTick) {
        m_collisionActive = false;
        if (m_pCollision) m_pCollision->Stop();
    }

    LOG_DEBUG("FFB: spd=%.1fkmh spring=%.2f lat=%.2f slip=%.2f us=%.2f",
              tele.speed * 3.6f, finalSpring, tele.lateralNorm,
              tele.wheelSpinMax, understeerFactor);
}

void FFBEngine::UpdateSpring(float factor) {
    if (!m_pSpring) return;

    LONG coeff = static_cast<LONG>(factor * DI_FFMAX);

    DICONDITION cond{};
    cond.lPositiveCoefficient = coeff;
    cond.lNegativeCoefficient = coeff;
    cond.dwPositiveSaturation = DI_FFMAX;
    cond.dwNegativeSaturation = DI_FFMAX;
    cond.lDeadBand            = (LONG)(g_Config.input.steeringDeadzone * DI_FFMAX);

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams= &cond;

    m_pSpring->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
}

void FFBEngine::UpdateDamper() {
    if (!m_pDamper) return;
    LONG coeff = ScaleMag(1.0f, g_Config.ffb.damperStrength);

    DICONDITION cond{};
    cond.lPositiveCoefficient = coeff;
    cond.lNegativeCoefficient = coeff;
    cond.dwPositiveSaturation = DI_FFMAX;
    cond.dwNegativeSaturation = DI_FFMAX;

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams= &cond;

    m_pDamper->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
}

void FFBEngine::UpdateConstForce(float lateralNorm, float lateralSign) {
    if (!m_pConstForce) return;

    LONG mag = ScaleMag(lateralNorm, g_Config.ffb.lateralForce);
    mag = static_cast<LONG>(mag * lateralSign);

    DICONSTANTFORCE cf{};
    cf.lMagnitude = mag;

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
    eff.lpvTypeSpecificParams= &cf;

    m_pConstForce->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
}

void FFBEngine::UpdateVibration(float wheelSpinMax) {
    if (!m_pVibration) return;

    LONG mag = ScaleMag(wheelSpinMax, g_Config.ffb.rumbleStrength);

    DIPERIODIC per{};
    per.dwMagnitude = static_cast<DWORD>(mag);
    per.lOffset     = 0;
    per.dwPhase     = 0;
    per.dwPeriod    = 80000;

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams= &per;

    m_pVibration->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
}

// ── TriggerCollision ──────────────────────────────────────────────────────────

void FFBEngine::TriggerCollision(float impact) {
    if (!m_pCollision || !m_ready) return;

    LONG mag = ScaleMag(impact, g_Config.ffb.collisionForce);
    DWORD durUs = static_cast<DWORD>(g_Config.ffb.collisionDurationMs) * 1000;

    DIPERIODIC per{};
    per.dwMagnitude = static_cast<DWORD>(mag);
    per.dwPeriod    = 40000;

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration           = durUs;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams= &per;

    m_pCollision->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_DURATION);
    m_pCollision->Stop();
    m_pCollision->Start(1, 0);

    m_collisionActive  = true;
    m_collisionEndTick = GetTickCount() + g_Config.ffb.collisionDurationMs;

    LOG_INFO("FFB: collision impulse mag=%ld dur=%ums", mag, g_Config.ffb.collisionDurationMs);
}

// ── AC-Inspired Functions ─────────────────────────────────────────────────────

// 1. MinimumForce — prevents "dead zone" at center (AC: MIN_FF=0.05)
// Any non-zero force is boosted by minimumForce percentage to ensure
// the driver can always feel steering input, even at low speed.
LONG FFBEngine::ApplyMinimumForce(LONG rawForce) {
    if (rawForce == 0) return 0;
    LONG minOffset = ScaleMag(1.0f, g_Config.ffb.minimumForce);
    LONG sign = (rawForce > 0) ? 1 : -1;
    // Raw + minimum offset, capped at DI_FFMAX
    return std::min(static_cast<LONG>(DI_FFMAX),
                    std::abs(rawForce) + minOffset) * sign;
}

// 2. RoadTexture — micro-vibration proportional to speed (AC: ROAD=0.5)
// Creates a subtle high-frequency sine on the steering axis to simulate
// road surface. Grows linearly with speed.
void FFBEngine::UpdateRoadTexture(float speedNorm) {
    if (!m_pRoadTexture) return;

    // Road texture is only felt at speed, not at rest
    float roadMag = speedNorm * speedNorm;  // quadratic: smooth at low speed
    DWORD mag = static_cast<DWORD>(ScaleMag(roadMag, g_Config.ffb.roadTexture));

    DIPERIODIC per{};
    per.dwMagnitude = mag;
    per.dwPeriod    = 25000;  // 25 ms = 40 Hz — fine road texture feel
    per.lOffset     = 0;

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams= &per;

    m_pRoadTexture->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
}

// 3. SlipVibration — traction-loss rumble (AC: SLIPS)
// Reuses m_pVibration; grows with wheel spin/slip ratio.
void FFBEngine::UpdateSlipVibration(float slipAmount) {
    if (!m_pVibration) return;

    LONG mag = ScaleMag(slipAmount, g_Config.ffb.slipVibration);

    DIPERIODIC per{};
    per.dwMagnitude = static_cast<DWORD>(mag);
    per.dwPeriod    = 80000;  // 12.5 Hz rumble (distinct from road texture)
    per.lOffset     = 0;

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams= &per;

    m_pVibration->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
}

// 4. CurbEffect — rumble strip impact (AC: CURBS=0.4)
// One-shot vibration triggered when the car hits a kerb.
// Detected externally and called via TriggerCollision-like mechanism.
void FFBEngine::UpdateCurbEffect(float curbImpact) {
    if (!m_pCollision || m_collisionActive) return;
    if (curbImpact < 0.05f) return;

    LONG mag = ScaleMag(curbImpact, g_Config.ffb.curbEffect);
    DWORD dur = 120000;  // 120 ms — shorter than full collision

    DIPERIODIC per{};
    per.dwMagnitude = static_cast<DWORD>(mag);
    per.dwPeriod    = 20000;  // 50 Hz — sharp curb feel

    DIEFFECT eff{};
    eff.dwSize               = sizeof(DIEFFECT);
    eff.dwFlags              = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration           = dur;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams= &per;

    m_pCollision->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_DURATION);
    m_pCollision->Stop();
    m_pCollision->Start(1, 0);

    m_collisionActive  = true;
    m_collisionEndTick = GetTickCount() + 120;

    LOG_DEBUG("FFB: curb effect mag=%ld", mag);
}

// 5. UndersteerEffect — reduce centering force when front grip is lost (AC: UNDERSTEER)
// When the car is understeering (front wheels slipping), the spring force
// is reduced proportionally to allow the driver to feel the loss of grip.
void FFBEngine::UpdateUndersteerEffect(float understeerFactor) {
    if (!m_pSpring) return;
    // This is applied as a gain modifier on the next spring update.
    // The FFBEngine::Update() calls UpdateSpring() with this factor subtracted.
    // understeerFactor: 0 = no understeer, 1 = full understeer
    float lightenFactor = understeerFactor * (g_Config.ffb.understeerLightening / 100.0f);
    LOG_DEBUG("FFB: understeer lighten=%.2f", lightenFactor);
    // Applied in UpdateSpring via a gain reduction — stored but used next Update()
    // (Implementation: modify the gain of the spring effect temporarily)
    DIEFFECT eff{};
    eff.dwSize  = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwGain  = static_cast<DWORD>((1.0f - lightenFactor) * DI_FFMAX);
    m_pSpring->SetParameters(&eff, DIEP_GAIN);
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void FFBEngine::Shutdown() {
    auto safe_release = [](IDirectInputEffect*& p) {
        if (p) { p->Stop(); p->Release(); p = nullptr; }
    };
    safe_release(m_pSpring);
    safe_release(m_pDamper);
    safe_release(m_pConstForce);
    safe_release(m_pVibration);
    safe_release(m_pCollision);
    safe_release(m_pRoadTexture);
    m_ready = false;
    LOG_INFO("FFB: all effects released");
}
