#include "input_bridge.h"
#include "config.h"
#include "logger.h"
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────

InputBridge& InputBridge::Get() {
    static InputBridge instance;
    return instance;
}

bool InputBridge::Init(IDirectInput8A* pDI) {
    LOG_INFO("InputBridge: initializing...");

    // Initialize G29 device
    if (!m_device.Init(pDI)) {
        LOG_ERROR("InputBridge: G29 device init failed");
        return false;
    }

    // Initialize FFB engine on the acquired device
    if (!m_ffb.Init(m_device.Device())) {
        LOG_ERROR("InputBridge: FFB engine init failed");
        // Not fatal — continue without FFB
    }

    // Initialize telemetry (game address resolution)
    Telemetry::Get().Init();  // non-fatal if it fails

    // Start telemetry + FFB update thread
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&InputBridge::TelemetryLoop, this);

    m_ready.store(true, std::memory_order_release);
    LOG_INFO("InputBridge: ready");
    return true;
}

// ── TelemetryLoop — runs at ~100 Hz (10 ms) ───────────────────────────────

void InputBridge::TelemetryLoop() {
    // Slightly higher thread priority to reduce jitter
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    LOG_INFO("TelemetryLoop: started (10 ms tick)");

    while (m_running.load(std::memory_order_relaxed)) {
        auto tick_start = std::chrono::steady_clock::now();

        // ── 1. Poll G29 input ─────────────────────────────────────────────
        bool inputOk = m_device.Update();

        if (inputOk) {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_lastState = m_device.State();
        }

        // ── 2. Read game telemetry ────────────────────────────────────────
        TelemetryData tele{};
        if (Telemetry::Get().IsReady()) {
            tele = Telemetry::Get().Read();
        }

        // ── 3. Update FFB ─────────────────────────────────────────────────
        if (m_ffb.IsReady() && g_Config.ffb.enabled) {
            float steeringInput = m_device.IsValid() ? m_device.State().steering : 0.0f;
            m_ffb.Update(tele, steeringInput);
        }

        // ── 4. Log diagnostics periodically ──────────────────────────────
        static int logCounter = 0;
        if (++logCounter >= 100) {  // every ~1 second
            logCounter = 0;
            if (tele.playerCarValid) {
                LOG_INFO("Tele: spd=%.1f km/h latG=%.2f wspin=%.2f steer=%.2f",
                         tele.speed * 3.6f,
                         tele.lateralAccel / 9.81f,
                         tele.wheelSpinMax,
                         tele.steerAngle);
                if (m_device.IsValid()) {
                    const auto& s = m_device.State();
                    LOG_INFO("G29: steer=%.3f gas=%.2f brake=%.2f clutch=%.2f",
                             s.steering, s.gas, s.brake, s.clutch);
                }
            }
        }

        // ── 5. Sleep remainder of 10 ms slot ──────────────────────────────
        auto elapsed = std::chrono::steady_clock::now() - tick_start;
        auto remaining = std::chrono::milliseconds(10) - elapsed;
        if (remaining > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(remaining);
        }
    }

    LOG_INFO("TelemetryLoop: stopped");
}

G29State InputBridge::GetState() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastState;
}

void InputBridge::Shutdown() {
    if (m_running.exchange(false)) {
        if (m_thread.joinable()) m_thread.join();
    }
    m_ffb.Shutdown();
    m_device.Shutdown();
    m_ready.store(false);
    LOG_INFO("InputBridge: shut down");
}
