#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <atomic>
#include <mutex>
#include <thread>

// Logitech G29 — DirectInput hardware IDs
#define G29_VID 0x046D
#define G29_PID 0xC24F

// ─────────────────────────────────────────────────────────────────────────────
//  WheelState — normalized output of the G29 after axis correction,
//  deadzone removal, smoothing and pedal curves.
//
//  All values: 0.0 – 1.0 unless noted.
// ─────────────────────────────────────────────────────────────────────────────
struct WheelState {
    float steering;     // -1 (full left) .. +1 (full right)
    float throttle;     // 0 (released) .. 1 (floored)
    float brake;        // 0 (released) .. 1 (floored) — gamma curve applied
    float clutch;       // 0 (released) .. 1 (depressed)
    bool  paddleLeft;
    bool  paddleRight;
    bool  acquired;
};

// ─────────────────────────────────────────────────────────────────────────────
//  WheelInput — owns the G29 DirectInput device.
//  Also owns the TelemetryLoop thread (10 ms tick).
// ─────────────────────────────────────────────────────────────────────────────
class WheelInput {
public:
    static WheelInput& Get();

    // Called once the game's IDirectInput8A object is available.
    bool Init(IDirectInput8A* pDI);
    void Shutdown();

    // Thread-safe snapshot of the current wheel state.
    WheelState GetState() const;

    // Raw device handle for the FFB engine.
    IDirectInputDevice8A* Device() const { return m_pDev; }

    bool IsReady()  const { return m_ready.load(std::memory_order_acquire); }

    // True while a valid player car exists (i.e. in-race, not in menus).
    // Updated by TelemetryLoop every 10 ms; safe to read from any thread.
    bool IsInRace() const { return m_inRace.load(std::memory_order_relaxed); }

private:
    WheelInput() = default;
    ~WheelInput() { Shutdown(); }
    WheelInput(const WheelInput&) = delete;
    WheelInput& operator=(const WheelInput&) = delete;

    bool EnumerateAndAcquire(IDirectInput8A* pDI);
    void SetupDevice();
    bool PollDevice();
    void TelemetryLoop();

    // Smoothing: exponential moving average
    // alpha ∈ (0,1] — higher = less smoothing
    static float Smooth(float prev, float next, float alpha);

    IDirectInputDevice8A* m_pDev    = nullptr;
    std::atomic<bool>     m_ready{false};
    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_inRace{false};
    int                   m_inRaceStopFrames = 0;

    mutable std::mutex    m_mutex;
    WheelState            m_state{};
    WheelState            m_smoothed{};

    std::thread           m_thread;
    DIJOYSTATE2           m_js{};
};
