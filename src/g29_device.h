#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  G29Device — wraps a DirectInput8 device handle for the Logitech G29.
//
//  Responsibilities:
//   - Enumerate and acquire the G29 exclusively for FFB
//   - Expose normalized axis values corrected for G29's DirectInput layout
//   - Create and hold the set of FFB effect objects
//   - Provide safe effect update helpers
//
//  G29 DirectInput axis layout (confirmed hardware):
//   Axis 0 (X)  → Steering wheel   [0 .. 65535, center 32767]
//   Axis 1 (Y)  → Gas pedal        [0 .. 65535, released = 65535]
//   Axis 2 (Z)  → Clutch           [0 .. 65535]
//   Axis 5 (Rz) → Brake pedal      [0 .. 65535, released = 65535]
// ─────────────────────────────────────────────────────────────────────────────

// Logitech G29 hardware GUID (VID 046D / PID C24F)
// {FCA68B40-DBBF-11D0-BC04-0000C040B234}  ← generic joystick class, not used
// Instead we match by VID/PID in the enumeration callback.
#define G29_VENDOR_ID   0x046D
#define G29_PRODUCT_ID  0xC24F

struct G29State {
    float steering;     // -1 (full left) .. +1 (full right)
    float gas;          // 0 (released) .. 1 (floored)
    float brake;        // 0 (released) .. 1 (floored)
    float clutch;       // 0 (released) .. 1 (depressed)
    bool  paddleLeft;   // left paddle shift
    bool  paddleRight;  // right paddle shift
    bool  acquired;
};

class G29Device {
public:
    G29Device();
    ~G29Device();

    // Must be called with the IDirectInput8 object the game created.
    bool Init(IDirectInput8A* pDI);
    void Shutdown();

    // Poll device, update internal state.
    bool Update();

    const G29State& State() const { return m_state; }

    // Raw DI device for FFB engine use.
    IDirectInputDevice8A* Device() const { return m_pDevice; }

    bool IsValid() const { return m_pDevice != nullptr && m_state.acquired; }

private:
    static BOOL CALLBACK EnumDevicesCallback(const DIDEVICEINSTANCEA* pDev, void* pCtx);

    bool AcquireDevice(IDirectInput8A* pDI, const GUID& devGuid);
    void SetupDataFormat();
    void SetupAxisRanges();

    IDirectInputDevice8A*  m_pDevice = nullptr;
    GUID                   m_devGuid{};
    bool                   m_found   = false;
    G29State               m_state{};

    // Raw DIJOYSTATE2 buffer
    DIJOYSTATE2            m_js{};
};
