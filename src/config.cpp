#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

Config g_Config;

// ── helpers ────────────────────────────────────────────────────────────────

static int   ToInt(const std::string& s, int def)    { try { return std::stoi(s); } catch(...) { return def; } }
static float ToFloat(const std::string& s, float def){ try { return std::stof(s); } catch(...) { return def; } }
static bool  ToBool(const std::string& s, bool def)  {
    if (s == "1" || s == "true"  || s == "True"  || s == "TRUE")  return true;
    if (s == "0" || s == "false" || s == "False" || s == "FALSE") return false;
    return def;
}
static DWORD ToHex(const std::string& s, DWORD def) {
    try {
        return static_cast<DWORD>(std::stoul(s, nullptr, 0));
    } catch(...) { return def; }
}

std::string Config::ReadSection(const std::string& path, const std::string& section,
                                 const std::string& key, const std::string& def) {
    char buf[512] = {};
    GetPrivateProfileStringA(section.c_str(), key.c_str(), def.c_str(),
                              buf, sizeof(buf), path.c_str());
    return std::string(buf);
}

void Config::WritePrivate(const std::string& path, const std::string& section,
                           const std::string& key, const std::string& value) {
    WritePrivateProfileStringA(section.c_str(), key.c_str(), value.c_str(), path.c_str());
}

// ── Load ───────────────────────────────────────────────────────────────────

bool Config::Load(const std::string& iniPath) {
    auto R = [&](const std::string& sec, const std::string& key, const std::string& def = "") {
        return ReadSection(iniPath, sec, key, def);
    };

    // [ForceFeedback]
    ffb.enabled              = ToBool (R("ForceFeedback","Enabled","1"), true);
    ffb.lowSpeedWeight       = ToInt  (R("ForceFeedback","LowSpeedWeight","20"),  20);
    ffb.midSpeedWeight       = ToInt  (R("ForceFeedback","MidSpeedWeight","50"),  50);
    ffb.highSpeedWeight      = ToInt  (R("ForceFeedback","HighSpeedWeight","75"), 75);
    ffb.centerSpring         = ToInt  (R("ForceFeedback","CenterSpring","50"),    50);
    ffb.minimumForce         = ToInt  (R("ForceFeedback","MinimumForce","5"),      5);
    ffb.damperStrength       = ToInt  (R("ForceFeedback","DamperStrength","18"),  18);
    ffb.lateralForce         = ToInt  (R("ForceFeedback","LateralForce","50"),    50);
    ffb.slipVibration        = ToInt  (R("ForceFeedback","SlipVibration","25"),   25);
    ffb.roadTexture           = ToInt(R("ForceFeedback","RoadTexture","12"),             12);
    ffb.cornerTextureStrength = ToInt(R("ForceFeedback","CornerTextureStrength","18"),   18);
    ffb.curbEffect           = ToInt  (R("ForceFeedback","CurbEffect","35"),      35);
    ffb.curbPulseMs          = ToInt  (R("ForceFeedback","CurbPulseMs","80"),     80);
    ffb.understeerLightening = ToInt  (R("ForceFeedback","UndersteerLightening","30"), 30);
    ffb.collisionForce       = ToInt  (R("ForceFeedback","CollisionForce","55"),  55);
    ffb.collisionThreshold   = ToFloat(R("ForceFeedback","CollisionThreshold","0.15"), 0.15f);
    ffb.collisionDurationMs      = ToInt  (R("ForceFeedback","CollisionDurationMs","60"),   60);
    ffb.rearLightenStrength      = ToInt  (R("ForceFeedback","RearLightenStrength","12"),    12);
    ffb.rearSlipAssistStrength   = ToInt  (R("ForceFeedback","RearSlipAssistStrength","35"), 35);
    ffb.rearSlipThreshold        = ToFloat(R("ForceFeedback","RearSlipThreshold","0.18"),   0.18f);
    ffb.rearSlipMaxTorque        = ToInt  (R("ForceFeedback","RearSlipMaxTorque","30"),      30);
    ffb.rearSlipMinSpeed         = ToFloat(R("ForceFeedback","RearSlipMinSpeed","50.0"),    50.0f);
    ffb.frontLoadStrength        = ToInt(R("ForceFeedback","FrontLoadStrength","25"),        25);
    ffb.rackInertiaStrength      = ToInt(R("ForceFeedback","RackInertiaStrength","18"),      18);
    ffb.rackReleaseStrength      = ToInt(R("ForceFeedback","RackReleaseStrength","12"),      12);
    ffb.straightLineStability    = ToInt(R("ForceFeedback","StraightLineStability","15"),    15);
    ffb.lowSpeedAssistStrength   = ToInt(R("ForceFeedback","LowSpeedAssistStrength","10"),   10);
    ffb.casterReturnStrength     = ToInt(R("ForceFeedback","CasterReturnStrength","22"),     22);
    ffb.highSpeedDampingStrength = ToInt(R("ForceFeedback","HighSpeedDampingStrength","20"), 20);
    ffb.centerHoldRange          = ToFloat(R("CenterHold","CenterHoldRange","0.06"),          0.06f);
    ffb.centerHoldStrength       = ToFloat(R("CenterHold","CenterHoldStrength","0.10"),       0.10f);
    ffb.centerHoldHighSpeedBoost = ToFloat(R("CenterHold","CenterHoldHighSpeedBoost","0.22"), 0.22f);
    ffb.centerHoldSmooth         = ToFloat(R("CenterHold","CenterHoldSmooth","0.15"),         0.15f);
    ffb.loadTransferGain              = ToInt  (R("ForceFeedback","LoadTransferGain","35"),              35);
    ffb.loadTransferMax               = ToInt  (R("ForceFeedback","LoadTransferMax","30"),               30);
    ffb.loadTransferSmooth            = ToFloat(R("ForceFeedback","LoadTransferSmooth","0.15"),          0.15f);
    ffb.longLoadStrength              = ToInt  (R("ForceFeedback","LongLoadStrength","20"),              20);
    ffb.frontSlipThreshold            = ToFloat(R("FrontGripFeedback","FrontSlipThreshold","0.35"),      0.35f);
    ffb.scrubGain                     = ToInt  (R("FrontGripFeedback","ScrubGain","20"),                  20);
    ffb.scrubFrequency                = ToInt  (R("FrontGripFeedback","ScrubFrequency","32"),             32);
    ffb.scrubMax                      = ToInt  (R("FrontGripFeedback","ScrubMax","18"),                   18);
    ffb.understeerFeedbackThreshold   = ToFloat(R("FrontGripFeedback","UndersteerThreshold","0.55"),     0.55f);
    ffb.understeerMaxReduction        = ToInt  (R("FrontGripFeedback","UndersteerMaxReduction","25"),     25);
    ffb.enableEngineIdleVibration     = ToBool (R("ForceFeedback","EnableEngineIdleVibration","1"),        true);
    ffb.idleVibrationStrength         = ToInt  (R("ForceFeedback","IdleVibrationStrength","1"),            1);
    ffb.revVibrationStrength          = ToInt  (R("ForceFeedback","RevVibrationStrength","2"),             2);
    ffb.cutVibrationStrength          = ToInt  (R("ForceFeedback","CutVibrationStrength","1"),             1);
    ffb.idleSpeedThresholdKmh         = ToInt  (R("ForceFeedback","IdleSpeedThreshold","10"),              10);
    ffb.idleRpm                       = ToInt  (R("Engine","IdleRpm","800"),                               800);
    ffb.redlineRpm                    = ToInt  (R("Engine","RedlineRpm","8000"),                          8000);
    ffb.shiftKickEnabled              = ToBool (R("ShiftKick","ShiftKickEnabled","1"),                     true);
    ffb.shiftKickStrength             = ToInt  (R("ShiftKick","ShiftKickStrength","15"),                   15);
    ffb.shiftKickDurationMs           = ToInt  (R("ShiftKick","ShiftKickDurationMs","30"),                 30);

    auto clamp = [](int v, int lo, int hi){ return std::max(lo, std::min(hi, v)); };
    ffb.lowSpeedWeight       = clamp(ffb.lowSpeedWeight,       0, 100);
    ffb.midSpeedWeight       = clamp(ffb.midSpeedWeight,       0, 100);
    ffb.highSpeedWeight      = clamp(ffb.highSpeedWeight,      0, 100);
    ffb.centerSpring         = clamp(ffb.centerSpring,         0, 100);
    ffb.damperStrength       = clamp(ffb.damperStrength,       0, 100);
    ffb.lateralForce         = clamp(ffb.lateralForce,         0, 100);
    ffb.slipVibration        = clamp(ffb.slipVibration,        0, 100);
    ffb.roadTexture          = clamp(ffb.roadTexture,          0, 100);
    ffb.curbEffect           = clamp(ffb.curbEffect,           0, 100);
    ffb.understeerLightening = clamp(ffb.understeerLightening, 0, 100);
    ffb.collisionForce           = clamp(ffb.collisionForce,           0, 100);
    ffb.rearSlipAssistStrength   = clamp(ffb.rearSlipAssistStrength,   0, 100);
    ffb.rearSlipMaxTorque        = clamp(ffb.rearSlipMaxTorque,        0, 100);
    ffb.shiftKickStrength        = clamp(ffb.shiftKickStrength,        0, 100);
    ffb.longLoadStrength         = clamp(ffb.longLoadStrength,         0, 100);
    ffb.shiftKickDurationMs      = clamp(ffb.shiftKickDurationMs,     10, 200);

    // [Input]
    input.steeringDeadzone    = ToFloat(R("Input","SteeringDeadzone","0.005"), 0.005f);
    input.steeringSensitivity = ToFloat(R("Input","SteeringSensitivity","1.0"), 1.0f);
    input.pedalDeadzone       = ToFloat(R("Input","PedalDeadzone","0.01"), 0.01f);
    input.steeringRange       = ToInt  (R("Input","SteeringRange","900"),          900);
    input.virtualSteeringLock = ToInt  (R("Input","VirtualSteeringLock","200"),    200);
    input.steeringGamma       = ToFloat(R("Input","SteeringGamma","1.15"),         1.15f);
    input.brakeGamma          = ToFloat(R("Input","BrakeGamma","2.4"),             2.4f);
    input.invertSteering      = ToBool (R("Input","InvertSteering","0"), false);
    input.invertGas           = ToBool (R("Input","InvertGas","0"),      false);
    input.invertBrake         = ToBool (R("Input","InvertBrake","0"),    false);
    input.axisSteeringIdx     = ToInt  (R("Input","AxisSteeringIdx","0"), 0);
    input.axisGasIdx          = ToInt  (R("Input","AxisGasIdx","1"),      1);
    input.axisBrakeIdx        = ToInt  (R("Input","AxisBrakeIdx","5"),    5);
    input.axisClutchIdx       = ToInt  (R("Input","AxisClutchIdx","6"),   6);
    input.dynamicSteering      = ToBool (R("Input","DynamicSteering","1"),           true);
    input.lowSpeedLock         = ToInt  (R("Input","LowSpeedLock","240"),             240);
    input.midSpeedLock         = ToInt  (R("Input","MidSpeedLock","180"),             180);
    input.highSpeedLock        = ToInt  (R("Input","HighSpeedLock","120"),            120);
    input.highSpeedSensitivity = ToFloat(R("Input","HighSpeedSensitivity","1.15"),   1.15f);
    input.yawAssistStrength    = ToFloat(R("Input","YawAssistStrength","0.25"),      0.25f);
    input.yawAssistStartSpeed  = ToFloat(R("Input","YawAssistStartSpeed","40.0"),   40.0f);
    input.yawAssistFullSpeed   = ToFloat(R("Input","YawAssistFullSpeed","200.0"),  200.0f);

    // [Telemetry]
    telemetry.usePatternScan   = ToBool(R("Telemetry","UsePatternScan","1"), true);
    telemetry.ptrPlayerCarPtr  = ToHex(R("Telemetry","PtrPlayerCarPtr","0x00870910"), 0x00870910);
    telemetry.ofsCarBase       = ToHex(R("Telemetry","OfsCarBase","0x58"), 0x58);
    telemetry.ofsSpeedMps      = ToHex(R("Telemetry","OfsSpeedMps","0x00DC"), 0x00DC);
    telemetry.ofsLateralAccel  = ToHex(R("Telemetry","OfsLateralAccel","0x0160"), 0x0160);
    telemetry.ofsSteerAngle    = ToHex(R("Telemetry","OfsSteerAngle","0x0148"), 0x0148);
    telemetry.maxSpeedMps      = ToFloat(R("Telemetry","MaxSpeedMps","88.0"), 88.0f);
    telemetry.maxLateralAccelMs2 = ToFloat(R("Telemetry","MaxLateralAccelMs2","25.0"), 25.0f);

    // [General]
    general.logLevel   = ToInt (R("General","LogLevel","2"), 2);
    general.disableASI = ToBool(R("General","DisableASI","0"), false);

    LOG_INFO("Config loaded from: %s", iniPath.c_str());
    return true;
}

