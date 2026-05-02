#pragma once
#include <string>
#include <unordered_map>

struct FFBConfig {
    // ── Speed weight (três pontos — curva tipo FH5) ──────────────────────────
    int  lowSpeedWeight       = 20;    // % spring em repouso / baixa vel
    int  midSpeedWeight       = 50;    // % spring ~36 km/h
    int  highSpeedWeight      = 75;    // % spring 108+ km/h

    // ── Efeitos ──────────────────────────────────────────────────────────────
    int  minimumForce         = 5;
    int  damperStrength       = 18;
    int  lateralForce         = 50;    // cornering load + SAT
    int  slipVibration        = 25;
    int  roadTexture          = 12;    // micro-vibração sutil
    int  cornerTextureStrength = 18;  // modulação adicional em curva (rack kick)
    int  curbEffect           = 35;
    int  curbPulseMs          = 80;    // duração do pulso de zebra
    int  understeerLightening = 30;
    int  collisionForce       = 55;
    float collisionThreshold  = 0.15f;
    int  collisionDurationMs  = 60;    // impulso curto e forte

    // ── Rear Slip Assist ─────────────────────────────────────────────────────
    int   rearLightenStrength    = 12;   // % spring reduction proportional to rearSlipNorm
    int   rearSlipAssistStrength = 35;   // % de torque no escorregamento máximo
    float rearSlipThreshold      = 0.18f;// wheelSpinMax para ativar
    int   rearSlipMaxTorque      = 30;   // % máximo de saída (clamp)
    float rearSlipMinSpeed       = 50.0f;// km/h abaixo do qual desativa

    // ── Load Transfer Weight ──────────────────────────────────────────────────
    int   loadTransferGain   = 35;   // % de rigidez adicional por G lateral
    int   loadTransferMax    = 30;   // % máximo (clamp da adição ao spring)
    float loadTransferSmooth = 0.15f;// EMA alpha (menor = mais suave)

    // ── Longitudinal Load Transfer ────────────────────────────────────────────
    int   longLoadStrength   = 20;   // % spring added per 1G of braking (front axle loading)

    // ── Front Tire Load SAT ───────────────────────────────────────────────────
    int   frontLoadStrength        = 25;  // % — ganho de spring por G lateral dianteiro

    // ── Rack Inertia / Release Torque ────────────────────────────────────────
    int   rackInertiaStrength      = 18;  // % — damper boost from steer angular accel
    int   rackReleaseStrength      = 12;  // % — damper reduction on fast deceleration

    // ── Straight-Line Stability ───────────────────────────────────────────────
    int   straightLineStability    = 15;  // % — micro-damp near center at low lateral G

    // ── Low Speed Hydraulic Assist ────────────────────────────────────────────
    int   lowSpeedAssistStrength   = 10;  // % spring reduction at 0 km/h (gone by 15 km/h)

    // ── Caster Return Torque ─────────────────────────────────────────────────
    int   casterReturnStrength     = 22;  // % — spring boost: steerAbs * speedFactor

    // ── High-Speed Steering Damping ───────────────────────────────────────────
    int   highSpeedDampingStrength = 20;  // % — escala steerVelocity → damper boost

    // ── Center Hold Zone ──────────────────────────────────────────────────────
    float centerHoldRange          = 0.06f; // ±fraction of full lock where hold is active
    float centerHoldStrength       = 0.10f; // base hold force at low speed
    float centerHoldHighSpeedBoost = 0.22f; // extra force at 50+ m/s (180 km/h)
    float centerHoldSmooth         = 0.15f; // EMA alpha

    // ── Front Tire Scrub + Understeer Feedback ────────────────────────────────
    float frontSlipThreshold          = 0.35f; // abs(steerAngle)*lateralNorm para ativar scrub
    int   scrubGain                   = 20;    // % amplitude da vibração de scrub
    int   scrubFrequency              = 32;    // Hz
    int   scrubMax                    = 18;    // % amplitude máxima
    float understeerFeedbackThreshold = 0.55f; // frontSlip para aliviar spring
    int   understeerMaxReduction      = 25;    // % máximo de alívio adicional

    // ── Engine Idle Vibration ────────────────────────────────────────────────
    bool enableEngineIdleVibration = true;
    int  idleVibrationStrength     = 1;   // % amplitude at idle (keep ≤ 3)
    int  revVibrationStrength      = 2;   // % extra amplitude under throttle
    int  cutVibrationStrength      = 1;   // % transient on throttle lift
    int  idleSpeedThresholdKmh     = 10;  // km/h above which effect is fully gone
    // Per-car RPM range — export from nfsu2-car-tuning Python project.
    // Used to normalize tele.rpm into rpmNorm = (rpm - idle) / (redline - idle).
    int  idleRpm                   = 800; // TODO: set per car
    int  redlineRpm                = 8000;// TODO: set per car

    // ── Shift Kick ───────────────────────────────────────────────────────────
    bool  shiftKickEnabled    = true;
    int   shiftKickStrength   = 15;   // % of max force
    int   shiftKickDurationMs = 30;   // ms

    // ── Legado ───────────────────────────────────────────────────────────────
    int  centerSpring         = 50;    // SAT gain base
    int  speedWeight          = 60;    // overall speed-based spring scale
    bool enabled              = true;
};

struct InputConfig {
    float steeringDeadzone    = 0.005f;
    float steeringSensitivity = 1.0f;
    float pedalDeadzone       = 0.01f;
    int   steeringRange       = 900;   // graus físicos do volante
    int   virtualSteeringLock = 200;   // fallback estático quando DynamicSteering=0
    float steeringGamma       = 1.15f;
    float brakeGamma          = 2.4f;
    bool  invertSteering      = false;
    bool  invertGas           = false;
    bool  invertBrake         = false;
    int   axisSteeringIdx     = 0;
    int   axisGasIdx          = 1;
    int   axisBrakeIdx        = 5;
    int   axisClutchIdx       = 6;

    // ── Dynamic steering ratio (FH5-style) ───────────────────────────────────
    bool  dynamicSteering      = true;
    int   lowSpeedLock         = 240;   // lock efetivo em 0 km/h
    int   midSpeedLock         = 180;   // lock efetivo em ~40 km/h
    int   highSpeedLock        = 120;   // lock efetivo em 120+ km/h
    float highSpeedSensitivity = 1.15f; // multiplicador entre 40-120 km/h

    // ── Yaw assist (resposta agressiva em alta velocidade) ────────────────────
    float yawAssistStrength    = 0.25f; // max amplificação (0.25 = +25%)
    float yawAssistStartSpeed  = 40.0f; // km/h onde começa
    float yawAssistFullSpeed   = 200.0f;// km/h onde atinge máximo
};

struct TelemetryConfig {
    bool   usePatternScan      = true;
    // Fallback static addresses (NFSU2 NA, image base 0x400000)
    // Adjust with Cheat Engine if needed.
    //
    // Two-level pointer chain (verified via CE "Find out what writes"):
    //   container  = *(DWORD*)PtrPlayerCarPtr
    //   car_base   = *(DWORD*)(container + OfsCarBase)   [if OfsCarBase != 0]
    //   rpm        = *(float*)(car_base  + OFS_RPM)
    //
    // Candidates for PtrPlayerCarPtr (test each with CE pointer chain):
    //   0x0086B2E0 / 0x0086B2E8 / 0x0086B2F4 / 0x0086B390 / 0x0086B3FC
    DWORD  ptrPlayerCarPtr     = 0x00870910;  // static ptr → container object
    DWORD  ofsCarBase          = 0x0058;      // offset within container → car_base (0=direct)
    DWORD  ofsSpeedMps         = 0x00DC;    // float: speed in m/s
    DWORD  ofsLateralAccel     = 0x0160;    // float: lateral accel m/s²
    DWORD  ofsSteerAngle       = 0x0148;    // float: steer angle -1..1
    float  maxSpeedMps         = 88.0f;     // ~317 km/h (top speed NFSU2)
    float  maxLateralAccelMs2  = 25.0f;     // ~2.5 G clamp
};

struct AntiOscillationConfig {
    // Driver intent smoothing (Part 1)
    float steerIntentAlpha    = 0.18f;  // EMA α — lower = smoother, higher = faster

    // Rear slip hysteresis (Part 2)
    float slipHeldRiseAlpha   = 0.35f;  // how fast the held-slip level rises
    float slipHeldFallAlpha   = 0.12f;  // how fast it falls (slower = more hold)

    // Assist hold window (Part 3)
    float holdThreshold       = 0.35f;  // slip magnitude (0–1) that triggers hold
    float holdDurationSec     = 0.25f;  // seconds to sustain assist after threshold
};

struct GeneralConfig {
    int  logLevel      = 1;  // 0=none 1=error 2=info 3=debug
    bool disableASI    = false;
};

struct Config {
    FFBConfig             ffb;
    InputConfig           input;
    TelemetryConfig       telemetry;
    AntiOscillationConfig antiOsc;
    GeneralConfig         general;

    bool Load(const std::string& iniPath);

private:
    static std::string ReadSection(const std::string& path, const std::string& section,
                                   const std::string& key, const std::string& def = "");
    static void WritePrivate(const std::string& path, const std::string& section,
                              const std::string& key, const std::string& value);
};

extern Config g_Config;
