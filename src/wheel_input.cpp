// wheel_input.cpp
// Logitech G29 device management, axis normalization, smoothing, pedal curves.
//
// Axis layout confirmed from Assetto Corsa controls.ini:
//   [STEER]    AXLE=0  → lX          steering
//   [THROTTLE] AXLE=1  → lY          gas      (MIN=1 MAX=-1 → inverted)
//   [BRAKES]   AXLE=5  → lRz         brake    (MIN=1 MAX=-1 → inverted, GAMMA=2.4)
//   [CLUTCH]   AXLE=6  → rglSlider[0] (MIN=1 MAX=-1 → inverted)

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include "wheel_input.h"
#include "force_feedback.h"
#include "telemetry.h"
#include "config.h"
#include "logger.h"
#include "logitech_led.h"
#include "engine_curve.h"

// ─────────────────────────────────────────────────────────────────────────────

WheelInput& WheelInput::Get() {
    static WheelInput inst;
    return inst;
}

// ── Axis helpers ──────────────────────────────────────────────────────────────

// Normalize DI axis [0..65535] → [0..1]. invert flips the range.
static float NormPedal(LONG raw, bool invert) {
    float v = static_cast<float>(raw) / 65535.0f;
    return invert ? 1.0f - v : v;
}

// Normalize steering axis [0..65535] → [-1..+1] centered at 32767.
static float NormSteer(LONG raw) {
    float v = (static_cast<float>(raw) - 32767.0f) / 32767.0f;
    return std::max(-1.0f, std::min(1.0f, v));
}

// Remove deadzone, remap [dz..1] → [0..1] symmetrically.
static float ApplyDeadzone(float v, float dz) {
    if (std::fabsf(v) < dz) return 0.0f;
    float s = (v > 0.0f) ? 1.0f : -1.0f;
    return s * (std::fabsf(v) - dz) / (1.0f - dz);
}

// Power curve (AC brake gamma=2.4): light pressure → little force,
// heavy pressure → large force.
static float ApplyGamma(float v, float gamma) {
    return std::powf(std::max(0.0f, v), gamma);
}

// Exponential moving average smoothing.
float WheelInput::Smooth(float prev, float next, float alpha) {
    return prev + alpha * (next - prev);
}

// ── Device enumeration ────────────────────────────────────────────────────────

struct EnumCtx { GUID guid; bool found; };

static BOOL CALLBACK EnumCallback(const DIDEVICEINSTANCEA* dev, void* ctx) {
    auto* c   = reinterpret_cast<EnumCtx*>(ctx);
    DWORD vid = dev->guidProduct.Data1 & 0xFFFF;
    DWORD pid = (dev->guidProduct.Data1 >> 16) & 0xFFFF;

    LOG_DEBUG("WheelEnum: '%s' VID=%04X PID=%04X", dev->tszProductName, vid, pid);

    if (vid == G29_VID && pid == G29_PID) {
        c->guid  = dev->guidInstance;
        c->found = true;
        LOG_INFO("WheelInput: G29 found — '%s'", dev->tszProductName);
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

bool WheelInput::EnumerateAndAcquire(IDirectInput8A* pDI) {
    EnumCtx ctx{};
    HRESULT hr = pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCallback, &ctx,
                                   DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    if (FAILED(hr)) { LOG_ERROR("WheelInput: EnumDevices 0x%08X", hr); return false; }
    if (!ctx.found) {
        LOG_ERROR("WheelInput: G29 not found (VID=046D PID=C24F). "
                  "Is G HUB running? Is the wheel in DirectInput mode?");
        return false;
    }

    hr = pDI->CreateDevice(ctx.guid, &m_pDev, nullptr);
    if (FAILED(hr)) { LOG_ERROR("WheelInput: CreateDevice 0x%08X", hr); return false; }

    SetupDevice();
    return true;
}

// ── Window lookup ─────────────────────────────────────────────────────────────

static HWND FindGameWindow() {
    // Try known NFSU2 window class first
    HWND hwnd = FindWindowA("Speed2 DirectX Window Class", nullptr);
    if (hwnd) return hwnd;

    // Fallback: first visible top-level window owned by this process
    struct Ctx { HWND result; DWORD pid; };
    Ctx ctx{ nullptr, GetCurrentProcessId() };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid && IsWindowVisible(h)) { c->result = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

static bool ApplyCooperativeLevel(IDirectInputDevice8A* dev) {
    HWND hwnd = FindGameWindow();
    if (!hwnd) return false;
    HRESULT hr = dev->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (FAILED(hr)) {
        LOG_ERROR("WheelInput: SetCooperativeLevel exclusive failed 0x%08X — trying non-exclusive", hr);
        hr = dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
        if (FAILED(hr)) {
            LOG_ERROR("WheelInput: SetCooperativeLevel non-exclusive failed 0x%08X", hr);
            return false;
        }
    }
    return true;
}

// ── Device setup ──────────────────────────────────────────────────────────────

static BOOL CALLBACK SetAxisRanges(const DIDEVICEOBJECTINSTANCEA* obj, void* ctx) {
    auto* dev = reinterpret_cast<IDirectInputDevice8A*>(ctx);
    if (!(obj->dwType & DIDFT_AXIS)) return DIENUM_CONTINUE;

    // Set range 0–65535 for all axes
    DIPROPRANGE r;
    r.diph = { sizeof(r), sizeof(DIPROPHEADER), obj->dwType, DIPH_BYID };
    r.lMin = 0; r.lMax = 65535;
    dev->SetProperty(DIPROP_RANGE, &r.diph);

    // Zero hardware deadzone — we handle it in software
    DIPROPDWORD dz;
    dz.diph = { sizeof(dz), sizeof(DIPROPHEADER), obj->dwType, DIPH_BYID };
    dz.dwData = 0;
    dev->SetProperty(DIPROP_DEADZONE, &dz.diph);

    return DIENUM_CONTINUE;
}

void WheelInput::SetupDevice() {
    m_pDev->SetDataFormat(&c_dfDIJoystick2);
    m_pDev->EnumObjects(SetAxisRanges, m_pDev, DIDFT_AXIS);

    // Disable hardware auto-center — FFB engine owns the spring
    DIPROPDWORD ac;
    ac.diph = { sizeof(ac), sizeof(DIPROPHEADER), 0, DIPH_DEVICE };
    ac.dwData = DIPROPAUTOCENTER_OFF;
    m_pDev->SetProperty(DIPROP_AUTOCENTER, &ac.diph);

    // Wait up to 5 s for the game window to appear, then set cooperative level
    for (int i = 0; i < 50; ++i) {
        if (ApplyCooperativeLevel(m_pDev)) break;
        Sleep(100);
    }

    HRESULT acq = m_pDev->Acquire();
    if (SUCCEEDED(acq)) {
        m_state.acquired = true;
        LOG_INFO("WheelInput: device acquired");
    } else {
        LOG_ERROR("WheelInput: Acquire 0x%08X (will retry in loop)", acq);
    }
}

// ── Init / Shutdown ────────────────────────────────────────────────────────────

bool WheelInput::Init(IDirectInput8A* pDI) {
    if (!EnumerateAndAcquire(pDI)) return false;

    ForceFeedback::Get().Init(m_pDev);
    Telemetry::Get().Init();
    InitLogitechLED(m_pDev);  // passes G29 handle for DInput Escape LED control

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&WheelInput::TelemetryLoop, this);

    m_ready.store(true, std::memory_order_release);
    LOG_INFO("WheelInput: ready");
    return true;
}

void WheelInput::Shutdown() {
    m_running.store(false, std::memory_order_release);
    // detach em vez de join — não bloquear DllMain/DLL_PROCESS_DETACH
    if (m_thread.joinable()) m_thread.detach();
    ShutdownLogitechLED();
    ForceFeedback::Get().Shutdown();
    Telemetry::Get().Shutdown();
    if (m_pDev) { m_pDev->Unacquire(); m_pDev->Release(); m_pDev = nullptr; }
    m_ready.store(false);
}

// ── Poll device ───────────────────────────────────────────────────────────────

bool WheelInput::PollDevice() {
    if (!m_pDev) return false;

    if (!m_state.acquired) {
        ApplyCooperativeLevel(m_pDev);
        if (SUCCEEDED(m_pDev->Acquire())) {
            m_state.acquired = true;
            LOG_INFO("WheelInput: re-acquired");
        } else {
            return false;
        }
    }

    HRESULT hr = m_pDev->Poll();
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        m_state.acquired = false;
        return false;
    }

    hr = m_pDev->GetDeviceState(sizeof(DIJOYSTATE2), &m_js);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        m_state.acquired = false;
        return false;
    }
    return SUCCEEDED(hr);
}

// ── Dynamic steering lock interpolation (FH5-style) ──────────────────────────
// Segmento 1: 0 → 11 m/s (0 → 40 km/h)  lowSpeedLock → midSpeedLock
// Segmento 2: 11 → 33 m/s (40 → 120 km/h) midSpeedLock → highSpeedLock
// Acima de 33 m/s: plato em highSpeedLock.
static float DynamicSteeringLock(float speedMps, const InputConfig& cfg) {
    const float low  = static_cast<float>(cfg.lowSpeedLock);
    const float mid  = static_cast<float>(cfg.midSpeedLock);
    const float high = static_cast<float>(cfg.highSpeedLock);
    if (speedMps <= 0.0f)   return low;
    if (speedMps <= 11.0f)  return low  + (mid  - low)  * (speedMps / 11.0f);
    if (speedMps <= 33.0f)  return mid  + (high - mid)  * ((speedMps - 11.0f) / 22.0f);
    return high;
}

// ── TelemetryLoop — 10 ms tick ────────────────────────────────────────────────

void WheelInput::TelemetryLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    LOG_INFO("TelemetryLoop: started");

    // Adaptive steering alpha: faster near center (immediate response),
    // slower at extremes (no oscillation on full lock).
    constexpr float STEER_ALPHA_CENTER = 0.65f;  // |steer| < 0.15
    constexpr float STEER_ALPHA_EDGE   = 0.35f;  // |steer| >= 0.15
    constexpr float PEDAL_ALPHA  = 0.5f;

    const auto& cfg = g_Config.input;
    int      logTick   = 0;
    uint32_t prevCarId = UINT32_MAX;  // for EnsureEngineCurveLoaded trigger

    while (m_running.load(std::memory_order_relaxed)) {
        auto t0 = std::chrono::steady_clock::now();

        // ── 1. Read telemetry ──────────────────────────────────────────────
        auto tele = Telemetry::Get().Read();

        // ── 1b. Engine curve hot-swap on car change ────────────────────────
        // EnsureEngineCurveLoaded kept out of Telemetry::Read() to avoid I/O
        // on the hot path. Triggered here, at most once per carId transition.
        if (tele.carId != prevCarId) {
            if (tele.carId > 0)
                EnsureEngineCurveLoaded(tele.carId);
            prevCarId = tele.carId;
        }

        // Latch-based race gate (speed-only, playerCarValid-independent):
        //
        //   ON  — latches true the moment speed > 0.5 m/s.  playerCarValid is
        //         intentionally ignored here because in NFSU2 free-roam the car
        //         pointer at PtrPlayerCarPtr is null even while driving, so a
        //         playerCarValid-based trigger would never fire in open world.
        //
        //   OFF — resets after STOP_TIMEOUT_FRAMES consecutive frames with
        //         speed ≤ 0.5 m/s (~20 s at 100 Hz).  Long enough to survive
        //         race countdowns and traffic stops; short enough to restore
        //         menu filtering after the player returns to the garage.
        //
        //   At the login screen speed is always 0 → latch never triggers →
        //   pedal axes are neutralized and cannot scroll menus.
        {
            constexpr int STOP_TIMEOUT_FRAMES = 500;  // ~5 s at 100 Hz
            bool cur = m_inRace.load(std::memory_order_relaxed);
            bool next = cur;
            if (tele.speed > 0.5f) {
                next = true;
                m_inRaceStopFrames = 0;
            } else if (cur) {
                if (++m_inRaceStopFrames >= STOP_TIMEOUT_FRAMES) {
                    next = false;
                    m_inRaceStopFrames = 0;
                }
            } else {
                m_inRaceStopFrames = 0;
            }
            if (next != cur)
                m_inRace.store(next, std::memory_order_relaxed);
        }

        // ── 2. Poll raw input ──────────────────────────────────────────────
        if (PollDevice()) {
            WheelState raw{};
            raw.acquired = true;

            // Steering: normalize → deadzone → dynamic lock → sensitivity → gamma
            float s = NormSteer(m_js.lX);
            if (cfg.invertSteering) s = -s;
            s = ApplyDeadzone(s, cfg.steeringDeadzone);

            // Dynamic steering lock: smaller lock at higher speed.
            // Ratio = steeringRange / effectiveLock — higher ratio = more responsive.
            // Per-car steering lock from physics registry; fallback to config.
            float effectiveLock = cfg.dynamicSteering
                ? DynamicSteeringLock(tele.speed, cfg)
                : (tele.physics.steeringLock > 0.0f
                   ? tele.physics.steeringLock
                   : static_cast<float>(cfg.virtualSteeringLock));
            if (effectiveLock > 0.0f && cfg.steeringRange > 0) {
                float ratio = static_cast<float>(cfg.steeringRange) / effectiveLock;
                s = std::max(-1.0f, std::min(1.0f, s * ratio));
            }

            // High-speed sensitivity: blends in above 40 km/h, full at 120 km/h.
            if (cfg.dynamicSteering && cfg.highSpeedSensitivity != 1.0f) {
                float t = std::max(0.0f, std::min(1.0f, (tele.speed - 11.0f) / 22.0f));
                float sens = 1.0f + (cfg.highSpeedSensitivity - 1.0f) * t;
                s = std::max(-1.0f, std::min(1.0f, s * sens));
            }

            // Yaw assist: progressive steering amplification by speed.
            // 0% at YawAssistStartSpeed km/h → YawAssistStrength% at YawAssistFullSpeed km/h.
            if (cfg.yawAssistStrength > 0.0f) {
                float startMps = cfg.yawAssistStartSpeed / 3.6f;
                float fullMps  = cfg.yawAssistFullSpeed  / 3.6f;
                float t = (fullMps > startMps)
                    ? std::max(0.0f, std::min(1.0f, (tele.speed - startMps) / (fullMps - startMps)))
                    : 0.0f;
                float assist = 1.0f + cfg.yawAssistStrength * t;
                s = std::max(-1.0f, std::min(1.0f, s * assist));
            }

            // Gamma: sign(x) * pow(abs(x), gamma) — progressive curve.
            if (cfg.steeringGamma != 1.0f && s != 0.0f) {
                float sgn = (s > 0.0f) ? 1.0f : -1.0f;
                s = sgn * std::powf(std::fabsf(s), cfg.steeringGamma);
            }

            raw.steering = s;

            // Throttle: inverted (G29: released=65535)
            float t = NormPedal(m_js.lY, true);
            t = ApplyDeadzone(t, cfg.pedalDeadzone);
            raw.throttle = t;

            // Brake: inverted + gamma=2.4 (AC reference)
            float b = NormPedal(m_js.lRz, true);
            b = ApplyDeadzone(b, cfg.pedalDeadzone);
            b = ApplyGamma(b, cfg.brakeGamma);
            raw.brake = b;

            // Clutch: rglSlider[0], inverted (AC: AXLE=6)
            float c = NormPedal(m_js.rglSlider[0], true);
            c = ApplyDeadzone(c, cfg.pedalDeadzone);
            raw.clutch = c;

            // Paddle shifts (buttons 4 = left, 5 = right on G29 wheel rim)
            raw.paddleLeft  = (m_js.rgbButtons[4] & 0x80) != 0;
            raw.paddleRight = (m_js.rgbButtons[5] & 0x80) != 0;

            // Apply smoothing
            std::lock_guard<std::mutex> lock(m_mutex);
            float steerAlpha = (std::fabsf(raw.steering) < 0.15f)
                               ? STEER_ALPHA_CENTER : STEER_ALPHA_EDGE;
            m_smoothed.steering    = Smooth(m_smoothed.steering, raw.steering, steerAlpha);
            m_smoothed.throttle    = Smooth(m_smoothed.throttle, raw.throttle, PEDAL_ALPHA);
            m_smoothed.brake       = Smooth(m_smoothed.brake,    raw.brake,    PEDAL_ALPHA);
            m_smoothed.clutch      = Smooth(m_smoothed.clutch,   raw.clutch,   PEDAL_ALPHA);
            m_smoothed.paddleLeft  = raw.paddleLeft;
            m_smoothed.paddleRight = raw.paddleRight;
            m_smoothed.acquired    = true;
        }

        // ── 3. Update FFB ──────────────────────────────────────────────────
        if (ForceFeedback::Get().IsReady() && g_Config.ffb.enabled) {
            float steer = m_smoothed.steering;
            ForceFeedback::Get().Update(tele, steer);
        }

        // ── 4. Periodic log (LogLevel=3 / debug only) ─────────────────────
        if (++logTick >= 100) {
            logTick = 0;
            if (tele.playerCarValid) {
                LOG_DEBUG("Tele: %.0f km/h latG=%.2f slip=%.2f steer=%.2f",
                          tele.speed * 3.6f, tele.lateralAccel / 9.81f,
                          tele.slipAngle, tele.steerAngle);
            }
            LOG_DEBUG("G29: steer=%.3f thr=%.2f brk=%.2f clt=%.2f",
                      m_smoothed.steering, m_smoothed.throttle,
                      m_smoothed.brake, m_smoothed.clutch);
        }

        // ── 5. Sleep remainder of 10 ms ────────────────────────────────────
        auto elapsed = std::chrono::steady_clock::now() - t0;
        auto rem = std::chrono::milliseconds(10) - elapsed;
        if (rem > std::chrono::milliseconds(0))
            std::this_thread::sleep_for(rem);
    }

    LOG_INFO("TelemetryLoop: stopped");
}

WheelState WheelInput::GetState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_smoothed;
}
