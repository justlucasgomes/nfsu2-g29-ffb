#include "car_physics.h"
#include "logger.h"

// ── Fallback ──────────────────────────────────────────────────────────────────
// Returned for carId == 0 or any carId not in the registry.
// Curves load as "default" from cars.ini when this is active.

static const CarPhysicsData kDefault = {
    0, "default", 1200.f, 1.00f, 1.00f, 35.0f
};

// ── Registry ──────────────────────────────────────────────────────────────────
// carId values must be confirmed from log output:
//   Telemetry: carId resolved 0x00XX = YY
// then matched to the car driven in the NFSU2 garage.
//
// Mass and grip values from nfsu2-car-tuning Python project.
// name must match a section header in cars.ini exactly.
//
// TODO: populate with confirmed carId → car-name mappings.

static const CarPhysicsData kRegistry[] = {
    // { id,  name,       mass,    fGrip, rGrip, lock }
    {  34, "240SX",   1270.f,  1.00f, 0.95f, 35.0f },
    {  12, "Supra",   1490.f,  1.05f, 1.00f, 34.0f },
    {  18, "RX8",     1310.f,  1.02f, 0.98f, 36.0f },
    {   8, "WRX",     1430.f,  1.08f, 1.05f, 33.0f },
    {  22, "LancerEvo", 1390.f, 1.10f, 1.05f, 32.0f },
    {   5, "EclipseGS", 1350.f, 1.00f, 0.97f, 35.0f },
    {  15, "Skyline", 1540.f,  1.05f, 1.02f, 34.0f },
    {  27, "CivicSi", 1120.f,  0.98f, 0.93f, 38.0f },
    {   3, "Sentra",  1180.f,  0.96f, 0.94f, 37.0f },
};
static constexpr int kRegistrySize = (int)(sizeof(kRegistry) / sizeof(kRegistry[0]));

// ── Lookup ────────────────────────────────────────────────────────────────────

const CarPhysicsData& GetCarData(uint32_t carId) {
    if (carId == 0) return kDefault;
    for (int i = 0; i < kRegistrySize; ++i) {
        if (kRegistry[i].id == carId) return kRegistry[i];
    }
    LOG_DEBUG("CarPhysics: unknown carId=%u, using default", carId);
    return kDefault;
}
