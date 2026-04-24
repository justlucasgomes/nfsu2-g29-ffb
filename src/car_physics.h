#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  CarPhysicsData — per-car parameters used by the FFB engine.
//
//  Values come from the static registry in car_physics.cpp.
//  carId values are confirmed from log output:
//    Telemetry: carId resolved 0x00XX = YY
//  then matched to the car selected in the NFSU2 garage.
// ─────────────────────────────────────────────────────────────────────────────

struct CarPhysicsData {
    uint32_t    id;            // carId as read from game memory
    const char* name;          // section name in cars.ini (must match exactly)
    float       mass;          // vehicle mass in kg
    float       frontGrip;     // normalized front tire grip (1.0 = baseline)
    float       rearGrip;      // normalized rear tire grip  (1.0 = baseline)
    float       steeringLock;  // steering lock in degrees (full left to center)
};

// Returns physics data for carId. Falls back to a safe default if unknown.
const CarPhysicsData& GetCarData(uint32_t carId);
