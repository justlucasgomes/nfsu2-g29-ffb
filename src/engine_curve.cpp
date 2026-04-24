#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <sstream>
#include "engine_curve.h"
#include "car_physics.h"
#include "logger.h"

static EngineCurve   g_curve;
static std::string   g_loadedCarName;  // last name passed to LoadEngineCurve
static std::string   g_carsIniPath;    // stored on first LoadEngineCurve call

const EngineCurve& GetEngineCurve() { return g_curve; }

// ── Parsing ───────────────────────────────────────────────────────────────────

static float ReadIniFloat(const char* section, const char* key,
                           float def, const std::string& path) {
    char buf[64] = {};
    char defStr[32];
    snprintf(defStr, sizeof(defStr), "%.1f", def);
    GetPrivateProfileStringA(section, key, defStr, buf, sizeof(buf), path.c_str());
    try { return std::stof(std::string(buf)); } catch (...) { return def; }
}

// Parse "rpm:torque,rpm:torque,..." into sorted TorquePoint vector.
static std::vector<TorquePoint> ParseCurveString(const std::string& s) {
    std::vector<TorquePoint> pts;
    if (s.empty()) return pts;

    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        auto colon = token.find(':');
        if (colon == std::string::npos) continue;
        try {
            float rpm  = std::stof(token.substr(0, colon));
            float torq = std::stof(token.substr(colon + 1));
            pts.push_back({ rpm, torq });
        } catch (...) {}
    }

    std::sort(pts.begin(), pts.end(), [](const TorquePoint& a, const TorquePoint& b) {
        return a.rpm < b.rpm;
    });
    return pts;
}

// ── Loader ────────────────────────────────────────────────────────────────────

bool LoadEngineCurve(const std::string& iniPath, const std::string& carName) {
    if (!iniPath.empty()) g_carsIniPath = iniPath;  // store for EnsureEngineCurveLoaded
    g_loadedCarName = carName;                       // track even on failure

    if (carName.empty()) {
        LOG_INFO("EngineCurve: no car name specified — using generic curve");
        g_curve = EngineCurve{};
        return false;
    }

    const char* sec = carName.c_str();

    g_curve.idleRpm    = ReadIniFloat(sec, "IdleRpm",    800.0f,  iniPath);
    g_curve.redlineRpm = ReadIniFloat(sec, "RedlineRpm", 8000.0f, iniPath);

    // Clamp to safe range
    g_curve.idleRpm    = std::max(100.0f,  g_curve.idleRpm);
    g_curve.redlineRpm = std::max(g_curve.idleRpm + 500.0f, g_curve.redlineRpm);

    char buf[512] = {};
    GetPrivateProfileStringA(sec, "TorqueCurve", "", buf, sizeof(buf), iniPath.c_str());
    g_curve.points = ParseCurveString(std::string(buf));
    g_curve.loaded = !g_curve.points.empty();

    if (g_curve.loaded) {
        LOG_INFO("EngineCurve: loaded [%s] idle=%.0f redline=%.0f points=%d",
                 sec, g_curve.idleRpm, g_curve.redlineRpm, (int)g_curve.points.size());
    } else {
        LOG_INFO("EngineCurve: [%s] not found in cars.ini — using generic curve", sec);
    }
    return g_curve.loaded;
}

// ── Hot-swap on car change ────────────────────────────────────────────────────

void EnsureEngineCurveLoaded(uint32_t carId) {
    if (g_carsIniPath.empty()) return;  // path not set yet (LoadEngineCurve not called)

    const CarPhysicsData& car = GetCarData(carId);
    if (std::string(car.name) == g_loadedCarName) return;  // already loaded

    LOG_INFO("EngineCurve: switching to carId=%u name=%s", carId, car.name);
    LoadEngineCurve(g_carsIniPath, car.name);
}

// ── Interpolation ─────────────────────────────────────────────────────────────

float EstimateTorqueNormForCar(float rpm) {
    const auto& pts = g_curve.points;

    if (pts.empty()) {
        // Generic 4-cylinder fallback — same values as the old EstimateTorqueNorm().
        static const float kRpm[]  = {  800.f, 1000.f, 2000.f, 3000.f, 4000.f,
                                        5000.f, 6000.f, 7000.f, 8000.f };
        static const float kTorq[] = { 0.45f,  0.55f,  0.70f,  0.85f,  1.00f,
                                       0.95f,  0.85f,  0.75f,  0.65f };
        constexpr int kN = 9;
        if (rpm <= kRpm[0])      return kTorq[0];
        if (rpm >= kRpm[kN - 1]) return kTorq[kN - 1];
        for (int i = 0; i < kN - 1; ++i) {
            if (rpm < kRpm[i + 1]) {
                float t = (rpm - kRpm[i]) / (kRpm[i + 1] - kRpm[i]);
                return kTorq[i] + t * (kTorq[i + 1] - kTorq[i]);
            }
        }
        return kTorq[kN - 1];
    }

    if (rpm <= pts.front().rpm) return pts.front().torqueNorm;
    if (rpm >= pts.back().rpm)  return pts.back().torqueNorm;

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        if (rpm < pts[i + 1].rpm) {
            float t = (rpm - pts[i].rpm) / (pts[i + 1].rpm - pts[i].rpm);
            return pts[i].torqueNorm + t * (pts[i + 1].torqueNorm - pts[i].torqueNorm);
        }
    }
    return pts.back().torqueNorm;
}
