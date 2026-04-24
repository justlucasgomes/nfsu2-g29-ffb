#pragma once
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  EngineCurve — per-car torque curve loaded from cars.ini.
//
//  Format in cars.ini:
//    [240SX]
//    IdleRpm=800
//    RedlineRpm=7000
//    TorqueCurve=1000:0.55,2000:0.70,3000:0.90,4000:1.00,5000:0.95,6000:0.85,7000:0.75
//
//  Usage:
//    LoadEngineCurve(carsIniPath, "240SX");
//    float t = EstimateTorqueNormForCar(rpm);   // replaces generic curve
//    float idleRpm    = GetEngineCurve().idleRpm;
//    float redlineRpm = GetEngineCurve().redlineRpm;
// ─────────────────────────────────────────────────────────────────────────────

struct TorquePoint {
    float rpm;
    float torqueNorm;  // 0..1, normalized (1.0 = peak torque)
};

struct EngineCurve {
    float                   idleRpm    = 800.0f;
    float                   redlineRpm = 8000.0f;
    std::vector<TorquePoint> points;
    bool                    loaded     = false;  // true when car data is present
};

// Returns the currently loaded curve (or defaults when not loaded).
const EngineCurve& GetEngineCurve();

// Load per-car data from cars.ini. Returns true if the section was found.
// Falls back to generic curve internally if loading fails.
// Also stores iniPath internally for use by EnsureEngineCurveLoaded.
bool LoadEngineCurve(const std::string& carsIniPath, const std::string& carName);

// Called every frame from Telemetry::Read() when carId is known.
// Reloads the curve only when the active car changes (internal dedup).
// Uses the path stored by the last LoadEngineCurve call.
void EnsureEngineCurveLoaded(uint32_t carId);

// Linear interpolation of torqueNorm at the given rpm.
// Falls back to the generic 4-cylinder curve when no car data is loaded.
float EstimateTorqueNormForCar(float rpm);
