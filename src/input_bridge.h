#pragma once
#include "g29_device.h"
#include "ffb_engine.h"
#include "telemetry.h"
#include <thread>
#include <atomic>
#include <mutex>

// ─────────────────────────────────────────────────────────────────────────────
//  InputBridge — central coordinator.
//
//  Responsibilities:
//  - Owns the G29Device and FFBEngine instances
//  - Runs the TelemetryLoop thread (10 ms tick):
//      1. Read telemetry from game memory
//      2. Update FFB effects (spring, damper, lat-force, vibration)
//      3. Auto-detect collision from speed delta → trigger impulse
//  - Provides GetState() for the DirectInput proxy to forward to the game
// ─────────────────────────────────────────────────────────────────────────────

class InputBridge {
public:
    static InputBridge& Get();

    // Called once the game's IDirectInput8 object is available.
    bool Init(IDirectInput8A* pDI);
    void Shutdown();

    // Query current wheel state (used by proxy to override game's DI reads).
    G29State GetState();

    bool IsReady() const { return m_ready.load(); }

private:
    InputBridge() = default;
    ~InputBridge() { Shutdown(); }
    InputBridge(const InputBridge&) = delete;
    InputBridge& operator=(const InputBridge&) = delete;

    void TelemetryLoop();

    G29Device             m_device;
    FFBEngine             m_ffb;
    std::thread           m_thread;
    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_ready{false};
    std::mutex            m_stateMutex;
    G29State              m_lastState{};
};
