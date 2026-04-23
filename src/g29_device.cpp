#include "g29_device.h"
#include "config.h"
#include "logger.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────

G29Device::G29Device()  = default;
G29Device::~G29Device() { Shutdown(); }

// ── Enumeration ──────────────────────────────────────────────────────────────

struct EnumCtx { GUID guid; bool found; DWORD vendorId; DWORD productId; };

BOOL CALLBACK G29Device::EnumDevicesCallback(const DIDEVICEINSTANCEA* pDev, void* pCtx) {
    auto* ctx = reinterpret_cast<EnumCtx*>(pCtx);

    // Match by VID/PID embedded in product GUID
    DWORD vidpid = pDev->guidProduct.Data1;
    DWORD vid = vidpid & 0xFFFF;
    DWORD pid = (vidpid >> 16) & 0xFFFF;

    LOG_DEBUG("G29Enum: found device '%s' VID=%04X PID=%04X",
              pDev->tszProductName, vid, pid);

    if (vid == ctx->vendorId && pid == ctx->productId) {
        ctx->guid  = pDev->guidInstance;
        ctx->found = true;
        LOG_INFO("G29: matched device '%s'", pDev->tszProductName);
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

// ── Init ─────────────────────────────────────────────────────────────────────

bool G29Device::Init(IDirectInput8A* pDI) {
    if (!pDI) { LOG_ERROR("G29::Init: null IDirectInput8"); return false; }

    EnumCtx ctx{ {}, false, G29_VENDOR_ID, G29_PRODUCT_ID };
    HRESULT hr = pDI->EnumDevices(DI8DEVCLASS_GAMECTRL,
                                   EnumDevicesCallback,
                                   &ctx,
                                   DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    if (FAILED(hr)) {
        LOG_ERROR("G29: EnumDevices HRESULT=0x%08X", hr);
        return false;
    }

    if (!ctx.found) {
        LOG_ERROR("G29: Logitech G29 not found (VID=046D PID=C24F). "
                  "Ensure G HUB is running or device is in DirectInput mode.");
        return false;
    }

    return AcquireDevice(pDI, ctx.guid);
}

bool G29Device::AcquireDevice(IDirectInput8A* pDI, const GUID& devGuid) {
    m_devGuid = devGuid;

    HRESULT hr = pDI->CreateDevice(devGuid, &m_pDevice, nullptr);
    if (FAILED(hr) || !m_pDevice) {
        LOG_ERROR("G29: CreateDevice failed 0x%08X", hr);
        return false;
    }

    SetupDataFormat();
    SetupAxisRanges();

    // Set cooperative level: exclusive foreground for FFB ownership
    HWND hwnd = FindWindowA("Speed2 DirectX Window Class", nullptr);
    if (!hwnd) hwnd = GetForegroundWindow();

    hr = m_pDevice->SetCooperativeLevel(hwnd,
             DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (FAILED(hr)) {
        LOG_ERROR("G29: SetCooperativeLevel failed 0x%08X — trying non-exclusive", hr);
        m_pDevice->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
    }

    // Auto-center OFF — we control the spring ourselves
    DIPROPDWORD dipdw;
    dipdw.diph.dwSize       = sizeof(DIPROPDWORD);
    dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dipdw.diph.dwObj        = 0;
    dipdw.diph.dwHow        = DIPH_DEVICE;
    dipdw.dwData            = DIPROPAUTOCENTER_OFF;
    m_pDevice->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

    hr = m_pDevice->Acquire();
    if (FAILED(hr)) {
        LOG_ERROR("G29: Acquire failed 0x%08X (game may not have focus yet)", hr);
        // Not fatal — we retry in Update()
    } else {
        m_state.acquired = true;
        LOG_INFO("G29: device acquired successfully");
    }

    return true;
}

void G29Device::SetupDataFormat() {
    HRESULT hr = m_pDevice->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) LOG_ERROR("G29: SetDataFormat failed 0x%08X", hr);
}

struct AxisEnumCtx { IDirectInputDevice8A* dev; };

static BOOL CALLBACK SetAxisRangeCallback(LPCDIDEVICEOBJECTINSTANCEA doi, LPVOID pCtx) {
    auto* ctx = reinterpret_cast<AxisEnumCtx*>(pCtx);
    if (doi->dwType & DIDFT_AXIS) {
        DIPROPRANGE dipr;
        dipr.diph.dwSize       = sizeof(DIPROPRANGE);
        dipr.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dipr.diph.dwObj        = doi->dwType;
        dipr.diph.dwHow        = DIPH_BYID;
        dipr.lMin = 0;
        dipr.lMax = 65535;
        ctx->dev->SetProperty(DIPROP_RANGE, &dipr.diph);

        // Zero deadzone — we manage deadzone in software
        DIPROPDWORD dz;
        dz.diph.dwSize       = sizeof(DIPROPDWORD);
        dz.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dz.diph.dwObj        = doi->dwType;
        dz.diph.dwHow        = DIPH_BYID;
        dz.dwData            = 0;
        ctx->dev->SetProperty(DIPROP_DEADZONE, &dz.diph);
        ctx->dev->SetProperty(DIPROP_SATURATION, nullptr); // leave default
    }
    return DIENUM_CONTINUE;
}

void G29Device::SetupAxisRanges() {
    AxisEnumCtx ctx{ m_pDevice };
    m_pDevice->EnumObjects(SetAxisRangeCallback, &ctx, DIDFT_AXIS);
}

// ── Update ───────────────────────────────────────────────────────────────────

static float NormAxis(LONG raw, bool invert = false) {
    float v = static_cast<float>(raw) / 65535.0f; // 0..1
    if (invert) v = 1.0f - v;
    return v;
}

static float NormSteer(LONG raw, bool invert = false) {
    // Center at 32767 → normalize to -1..+1
    float v = (static_cast<float>(raw) - 32767.0f) / 32767.0f;
    v = std::max(-1.0f, std::min(1.0f, v));
    if (invert) v = -v;
    return v;
}

static float ApplyDeadzone(float v, float dz) {
    if (std::fabsf(v) < dz) return 0.0f;
    float sign = (v > 0.0f) ? 1.0f : -1.0f;
    return sign * (std::fabsf(v) - dz) / (1.0f - dz);
}

bool G29Device::Update() {
    if (!m_pDevice) return false;

    // Re-acquire if lost (e.g. alt+tab)
    if (!m_state.acquired) {
        HRESULT hr = m_pDevice->Acquire();
        if (FAILED(hr)) return false;
        m_state.acquired = true;
        LOG_INFO("G29: re-acquired");
    }

    HRESULT hr = m_pDevice->Poll();
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        m_state.acquired = false;
        m_pDevice->Acquire();
        return false;
    }

    hr = m_pDevice->GetDeviceState(sizeof(DIJOYSTATE2), &m_js);
    if (FAILED(hr)) {
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            m_state.acquired = false;
        }
        return false;
    }

    const auto& cfg = g_Config.input;

    // ── Steering ──────────────────────────────────────────────────────────────
    m_state.steering = ApplyDeadzone(
        NormSteer(m_js.lX, cfg.invertSteering),
        cfg.steeringDeadzone);
    m_state.steering = std::max(-1.0f, std::min(1.0f,
        m_state.steering * cfg.steeringSensitivity));

    // ── Gas (Y axis, released = 65535 on G29) ─────────────────────────────────
    m_state.gas = ApplyDeadzone(
        NormAxis(m_js.lY, true /* inverted: 65535=released */),
        cfg.pedalDeadzone);

    // ── Brake (Rz axis, AC: AXLE=5, GAMMA=2.4) ───────────────────────────────
    // Apply power curve matching AC's brake gamma for progressive feel
    {
        float rawBrake = ApplyDeadzone(NormAxis(m_js.lRz, true), cfg.pedalDeadzone);
        m_state.brake = std::powf(rawBrake, cfg.brakeGamma);
    }

    // ── Clutch (rglSlider[0], AC: AXLE=6) ────────────────────────────────────
    m_state.clutch = ApplyDeadzone(
        NormAxis(m_js.rglSlider[0], true),
        cfg.pedalDeadzone);

    // ── Paddle shifts (buttons 4=left, 5=right on G29) ────────────────────────
    // G29 button mapping: 0-3=face buttons, 4=R2, 5=L2, 6=R1(pad-right), 7=L1(pad-left)
    m_state.paddleLeft  = (m_js.rgbButtons[4] & 0x80) != 0;
    m_state.paddleRight = (m_js.rgbButtons[5] & 0x80) != 0;

    return true;
}

// ── Shutdown ─────────────────────────────────────────────────────────────────

void G29Device::Shutdown() {
    if (m_pDevice) {
        m_pDevice->Unacquire();
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
    m_state = {};
    LOG_INFO("G29: device released");
}
