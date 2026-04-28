// telemetry.cpp
// Reads live game data from NFSU2 process memory (in-process — direct access).
//
// NFSU2 NA retail (4,800,512 bytes  MD5: 665871070B0E4065CE446967294BCCFA)
// Image base: 0x00400000 (no ASLR on this era).
//
// Primary method: pattern scanner finds the speedometer read instruction,
// extracts the speed address from its operand.  Falls back to static
// pointer chain from config.ini if scan fails.
//
// All reads are guarded with VirtualQuery + __try/__except to ensure a wrong
// offset never crashes the game.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include "telemetry.h"
#include "config.h"
#include "logger.h"
#include "pattern_scan.h"
#include "nfsu2_addresses.h"
#include "car_physics.h"

using namespace NFSU2_NA;
using namespace PatternScan;

// ── RPM pattern scan tables ───────────────────────────────────────────────────
//
// NFSU2 accesses car struct fields through a two-instruction sequence:
//   1. Load carBase pointer:  mov <reg>, [0x00575748]
//   2. Read the field:        fld dword ptr [<reg> + disp32]
//
// Opcodes for step 1:
//   A1 48 57 57 00       mov eax, [0x575748]
//   8B 0D 48 57 57 00    mov ecx, [0x575748]
//   8B 15 48 57 57 00    mov edx, [0x575748]
//   8B 35 48 57 57 00    mov esi, [0x575748]
//   8B 3D 48 57 57 00    mov edi, [0x575748]
//
// Opcodes for step 2 (fld [reg + disp32]):
//   D9 80 XX XX XX XX    fld [eax + disp32]
//   D9 81 XX XX XX XX    fld [ecx + disp32]
//   D9 82 XX XX XX XX    fld [edx + disp32]
//   D9 86 XX XX XX XX    fld [esi + disp32]
//   D9 87 XX XX XX XX    fld [edi + disp32]
//
// For disp8 (offset ≤ 0x7F, which covers gear at 0x68 but not speed/RPM):
//   D9 40 XX             fld [eax + disp8]   (included as fallback)
//
// The offset field is the 4-byte DWORD starting at `offPos` within the pattern.
// ─────────────────────────────────────────────────────────────────────────────

struct RpmScanPattern {
    const char* pattern;
    int         offPos;  // byte index of the 4-byte offset within the matched sequence
};

static const RpmScanPattern kRpmPatterns[] = {
    { "A1 48 57 57 00 D9 80 ?? ?? ?? ??", 7 },  // mov eax; fld [eax+disp32]
    { "8B 0D 48 57 57 00 D9 81 ?? ?? ?? ??", 8 },// mov ecx; fld [ecx+disp32]
    { "8B 15 48 57 57 00 D9 82 ?? ?? ?? ??", 8 },// mov edx; fld [edx+disp32]
    { "8B 35 48 57 57 00 D9 86 ?? ?? ?? ??", 8 },// mov esi; fld [esi+disp32]
    { "8B 3D 48 57 57 00 D9 87 ?? ?? ?? ??", 8 },// mov edi; fld [edi+disp32]
    // disp8 fallback — RPM unlikely here (offset > 0x7F), but included for completeness
    { "A1 48 57 57 00 D9 40 ??", -1 },           // -1 = disp8, extract 1 byte
};

// Offsets already known — exclude from RPM candidates to avoid re-finding them.
static const DWORD kKnownOffsets[] = {
    0x00DC,              // OFS_SPEED_MPS
    0x0148,              // OFS_STEER_ANGLE
    0x015C,              // OFS_LONG_ACCEL
    0x0160,              // OFS_LATERAL_ACCEL
    0x01C0, 0x01C4, 0x01C8, 0x01CC,  // OFS_WHEEL_SPIN_*
    0x0240,              // OFS_DAMAGE
};

// ─────────────────────────────────────────────────────────────────────────────

Telemetry& Telemetry::Get() {
    static Telemetry inst;
    return inst;
}

// ── RPM auto-resolution ───────────────────────────────────────────────────────

void Telemetry::BuildRpmCandidates() {
    if (m_rpmCandidatesBuilt) return;
    m_rpmCandidatesBuilt = true;

    // Walk PE header to get image bounds for scanning
    HMODULE    hMod  = GetModuleHandleA(nullptr);
    auto*      dos   = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    auto*      nt    = reinterpret_cast<IMAGE_NT_HEADERS*>(
                          reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    uintptr_t  base  = reinterpret_cast<uintptr_t>(hMod);
    size_t     imgSz = nt->OptionalHeader.SizeOfImage;

    auto isKnown = [](DWORD off) {
        for (DWORD k : kKnownOffsets)
            if (k == off) return true;
        return false;
    };

    for (const auto& pe : kRpmPatterns) {
        uintptr_t cursor = base;
        while (cursor < base + imgSz) {
            size_t    remaining = base + imgSz - cursor;
            uintptr_t hit       = Scan(cursor, remaining, pe.pattern);
            if (!hit) break;

            DWORD off;
            if (pe.offPos < 0) {
                // disp8 pattern: 1-byte unsigned offset
                off = static_cast<DWORD>(*reinterpret_cast<const uint8_t*>(hit + 7));
            } else {
                off = *reinterpret_cast<const DWORD*>(hit + pe.offPos);
            }

            // Accept offsets in the plausible car-struct RPM zone.
            // Upper bound raised to 0x0500: NFSU2 RPM verified at 0x0400.
            if (off >= 0x0050 && off <= 0x0500 && !isKnown(off)) {
                if (std::find(m_rpmCandidates.begin(), m_rpmCandidates.end(), off)
                    == m_rpmCandidates.end())
                    m_rpmCandidates.push_back(off);
            }

            cursor = hit + 1;
        }
    }

    // Sort candidates: prefer [0x0080, 0x00FF] — most likely zone for RPM in
    // NFSU2's car physics struct (speed is at 0x00DC, so RPM is likely nearby).
    std::sort(m_rpmCandidates.begin(), m_rpmCandidates.end(), [](DWORD a, DWORD b) {
        bool aP = (a >= 0x0080 && a <= 0x00FF);
        bool bP = (b >= 0x0080 && b <= 0x00FF);
        if (aP != bP) return static_cast<int>(aP) > static_cast<int>(bP);
        return a < b;
    });

    if (m_rpmCandidates.empty())
        LOG_INFO("Telemetry: RPM pattern scan — no candidates found");
    else
        LOG_INFO("Telemetry: RPM pattern scan — %d candidates", (int)m_rpmCandidates.size());

    for (DWORD c : m_rpmCandidates)
        LOG_DEBUG("Telemetry: RPM candidate offset 0x%04X", c);
}

void Telemetry::ValidateRpmOffset(uintptr_t carBase) {
    // ── Non-blocking behavioral state machine ─────────────────────────────────
    // Runs one step per Read() tick (~10 ms). No loops, no Sleep.
    //
    // States:
    //   CaptureIdle  (30 ticks = 300 ms)
    //     Read each candidate. Record first value as idleVal.
    //     Reject candidates where idleVal ∉ [500, 1500].
    //     [500, 1500] eliminates wheel/drivetrain RPM (=0 when stopped),
    //     torque (Nm, typically < 500 at idle), and normalized values (0-1).
    //
    //   WaitRise  (500 ticks = 5 s)
    //     Track peak value. Reject candidates where smoothness fails.
    //     Transition when timeout or all alive candidates rose.
    //
    //   WaitFall  (500 ticks = 5 s)
    //     Check every tick: if val < peakVal - 500 → candidate confirmed.
    //     First to confirm wins. At timeout: final check, then fail.
    //
    //   Smoothness (every tick, every phase):
    //     |val - prevVal| ≥ 2500 → disqualify. RPM can't jump 2500/10ms.
    // ─────────────────────────────────────────────────────────────────────────

    if (m_rpmValPhase == RpmValPhase::Resolved ||
        m_rpmValPhase == RpmValPhase::Failed) return;

    // First call: initialize per-candidate state vector
    if (m_rpmCandState.empty()) {
        m_rpmCandState.assign(m_rpmCandidates.size(), RpmCandState{});
        m_rpmValPhase = RpmValPhase::CaptureIdle;
        m_rpmValTick  = 0;
        LOG_INFO("Telemetry: RPM validation started (%d candidates)",
                 (int)m_rpmCandidates.size());
    }

    constexpr int   kCaptureTicks  = 30;     // 300 ms baseline capture
    constexpr int   kRiseTicks     = 500;    // 5 s max wait for throttle rise
    constexpr int   kFallTicks     = 500;    // 5 s max wait for throttle release
    constexpr float kSmoothLimit   = 2500.f; // max RPM delta per 10 ms tick
    constexpr float kIdleMin       = 500.f;
    constexpr float kIdleMax       = 1500.f;
    constexpr float kRiseThresh    = 500.f;  // must rise this much above idle
    constexpr float kFallThresh    = 500.f;  // must fall this much below peak

    // ── Per-tick update for all alive candidates ──────────────────────────────
    for (size_t i = 0; i < m_rpmCandidates.size(); ++i) {
        RpmCandState& cs = m_rpmCandState[i];
        if (!cs.alive) continue;

        float val = SafeReadFloat(carBase + m_rpmCandidates[i], -1.0f);
        if (val < 0.0f) { cs.alive = false; continue; }

        // Smoothness gate — applies in every phase
        if (cs.prevVal >= 0.0f && std::fabsf(val - cs.prevVal) >= kSmoothLimit) {
            LOG_DEBUG("Telemetry: 0x%04X disqualified (jump %.0f→%.0f)",
                      m_rpmCandidates[i], cs.prevVal, val);
            cs.alive = false;
            continue;
        }

        // Smoothness tracking (all phases): record largest jump seen
        if (cs.prevVal >= 0.0f) {
            float delta = std::fabsf(val - cs.prevVal);
            if (delta > cs.maxDelta) cs.maxDelta = delta;
        }

        // Phase-specific updates
        switch (m_rpmValPhase) {
        case RpmValPhase::CaptureIdle:
            if (cs.prevVal < 0.0f) {   // first reading this candidate sees
                cs.idleVal = val;
                cs.peakVal = val;
            }
            break;

        case RpmValPhase::WaitRise:
            if (val > cs.peakVal) cs.peakVal = val;
            break;

        case RpmValPhase::WaitFall:
            cs.finalVal = val;  // keep updating — we'll read it at phase end
            break;

        default: break;
        }

        cs.prevVal = val;
    }

    ++m_rpmValTick;

    // Helper: count alive candidates
    auto alive = [&]() {
        int n = 0;
        for (auto& s : m_rpmCandState) if (s.alive) ++n;
        return n;
    };

    // ── Phase transitions ─────────────────────────────────────────────────────
    switch (m_rpmValPhase) {

    case RpmValPhase::CaptureIdle:
        if (m_rpmValTick >= kCaptureTicks) {
            for (size_t i = 0; i < m_rpmCandidates.size(); ++i) {
                auto& cs = m_rpmCandState[i];
                if (!cs.alive) continue;
                if (cs.idleVal < kIdleMin || cs.idleVal > kIdleMax) {
                    LOG_DEBUG("Telemetry: 0x%04X disqualified (idle=%.0f ∉ [%.0f,%.0f])",
                              m_rpmCandidates[i], cs.idleVal, kIdleMin, kIdleMax);
                    cs.alive = false;
                }
            }
            int n = alive();
            if (n == 0) {
                m_rpmValPhase = RpmValPhase::Failed;
                LOG_INFO("Telemetry: RPM validation failed — no idle candidates");
            } else {
                m_rpmValPhase = RpmValPhase::WaitRise;
                m_rpmValTick  = 0;
                LOG_INFO("Telemetry: RPM → WaitRise (%d candidates, accelerate the car)", n);
            }
        }
        break;

    case RpmValPhase::WaitRise:
        if (m_rpmValTick >= kRiseTicks) {
            for (size_t i = 0; i < m_rpmCandidates.size(); ++i) {
                auto& cs = m_rpmCandState[i];
                if (!cs.alive) continue;
                if (cs.peakVal < cs.idleVal + kRiseThresh) {
                    LOG_DEBUG("Telemetry: 0x%04X disqualified (no rise: idle=%.0f peak=%.0f)",
                              m_rpmCandidates[i], cs.idleVal, cs.peakVal);
                    cs.alive = false;
                }
            }
            int n = alive();
            if (n == 0) {
                m_rpmValPhase = RpmValPhase::Failed;
                LOG_INFO("Telemetry: RPM validation failed — no rise detected in 5s");
            } else {
                m_rpmValPhase = RpmValPhase::WaitFall;
                m_rpmValTick  = 0;
                LOG_INFO("Telemetry: RPM → WaitFall (%d candidates, release throttle)", n);
            }
        }
        break;

    case RpmValPhase::WaitFall:
        if (m_rpmValTick >= kFallTicks) {
            // ── Score all surviving candidates, select the best ───────────────
            // idleScore:  how close idleVal is to 800 RPM (target idle)
            // peakScore:  how close peakVal is to 4000-8000 RPM (driving range)
            // returnScore: how much finalVal returned toward idle after fall
            // smoothScore: inverse of maxDelta (smoother = more engine-like)
            //
            // Engine RPM scores highest because:
            //   - idle ~800 (idleScore near 1.0)
            //   - peak 3000-7000 during driving (peakScore near 1.0)
            //   - returns to idle on release (returnScore near 1.0)
            //   - changes smoothly (smoothScore near 1.0)
            //
            // Wheel/shaft RPM scores lower because:
            //   - idle often 0 (fails WaitRise) or very low (low idleScore)
            //   - peak proportional to speed, not necessarily 4000-8000
            //   - may not return to idle-like value (returnScore low)

            int    bestIdx   = -1;
            float  bestScore = -1.0f;

            for (size_t i = 0; i < m_rpmCandidates.size(); ++i) {
                auto& cs = m_rpmCandState[i];
                if (!cs.alive) continue;

                // Require that the fall actually happened
                if (cs.finalVal >= cs.peakVal - kFallThresh ||
                    cs.peakVal  <  cs.idleVal + kRiseThresh) {
                    cs.alive = false;
                    continue;
                }

                // idleScore: 1.0 when idle == 800, 0.0 when diff >= 800
                float idleScore = std::max(0.0f, 1.0f - std::fabsf(cs.idleVal - 800.0f) / 800.0f);

                // peakScore: 1.0 inside [4000, 8000], falls off outside
                float peakScore;
                if (cs.peakVal >= 4000.0f && cs.peakVal <= 8000.0f)
                    peakScore = 1.0f;
                else if (cs.peakVal < 4000.0f)
                    peakScore = std::max(0.0f, cs.peakVal / 4000.0f);
                else
                    peakScore = std::max(0.0f, 1.0f - (cs.peakVal - 8000.0f) / 4000.0f);

                // returnScore: 1.0 when finalVal returned to within 200 of idle
                float returnDiff  = std::fabsf(cs.finalVal - cs.idleVal);
                float returnScore = std::max(0.0f, 1.0f - returnDiff / 2000.0f);

                // smoothScore: 1.0 when maxDelta == 0, 0.0 when maxDelta >= 2500
                float smoothScore = std::max(0.0f, 1.0f - cs.maxDelta / 2500.0f);

                cs.score = idleScore + peakScore + returnScore + smoothScore;

                LOG_INFO("Telemetry: 0x%04X score=%.2f "
                         "(idle=%.0f idleS=%.2f peak=%.0f peakS=%.2f "
                         "final=%.0f retS=%.2f maxΔ=%.0f smS=%.2f)",
                         m_rpmCandidates[i], cs.score,
                         cs.idleVal, idleScore,
                         cs.peakVal, peakScore,
                         cs.finalVal, returnScore,
                         cs.maxDelta, smoothScore);

                if (cs.score > bestScore) {
                    bestScore = cs.score;
                    bestIdx   = static_cast<int>(i);
                }
            }

            if (bestIdx >= 0) {
                m_rpmOffset   = m_rpmCandidates[bestIdx];
                m_rpmValPhase = RpmValPhase::Resolved;
                LOG_INFO("Telemetry: RPM resolved 0x%04X (score=%.2f)",
                         m_rpmCandidates[bestIdx], bestScore);
            } else {
                m_rpmValPhase = RpmValPhase::Failed;
                LOG_INFO("Telemetry: RPM validation failed — no qualifying candidate");
            }
        }
        break;

    default: break;
    }
}

// ── CarId auto-resolution ─────────────────────────────────────────────────────

void Telemetry::BuildCarIdCandidates(uintptr_t carBase) {
    if (m_carIdCandBuilt) return;
    m_carIdCandBuilt = true;

    // Offsets to exclude — known ints/floats that could produce false positives.
    // Floats are naturally filtered by the value range check [1, 511], but we
    // explicitly exclude OFS_GEAR (an int) and m_rpmOffset (if already resolved).
    auto isExcluded = [this](DWORD off) {
        if (off == OFS_GEAR)     return true;
        if (off == m_rpmOffset)  return true;
        return false;
    };

    // Scan the car struct in 4-byte steps.
    // Values in [1, 511]: floats (IEEE754) almost never fall in this range;
    // small int metadata (carId, difficulty, race flags) often do.
    for (DWORD off = 0x04; off <= 0x300; off += 4) {
        if (isExcluded(off)) continue;
        DWORD val = SafeReadDword(carBase + off, 0xFFFFFFFF);
        if (val >= 1 && val < 512) {
            m_carIdCandidates.push_back(off);
        }
    }

    // Sort: prefer [0x0050, 0x0150] — car metadata typically lives near the
    // beginning of the struct, before physics state variables.
    std::sort(m_carIdCandidates.begin(), m_carIdCandidates.end(), [](DWORD a, DWORD b) {
        bool aP = (a >= 0x0050 && a <= 0x0150);
        bool bP = (b >= 0x0050 && b <= 0x0150);
        if (aP != bP) return static_cast<int>(aP) > static_cast<int>(bP);
        return a < b;
    });

    LOG_INFO("Telemetry: CarID scan found %d candidates", (int)m_carIdCandidates.size());
    for (DWORD c : m_carIdCandidates)
        LOG_DEBUG("Telemetry: CarID candidate 0x%04X = %u", c,
                  SafeReadDword(carBase + c, 0));
}

void Telemetry::ValidateCarId(uintptr_t carBase) {
    // ── Non-blocking CarId stability state machine ────────────────────────────
    // One step per Read() tick (~10 ms). No loops, no Sleep.
    //
    // Capture (200 ticks = 2 s):
    //   Count how many times each candidate changes.
    //   A genuine carId never changes during a race — zero changes = perfect score.
    //   Range bonus: prefer values 1-100 (NFSU2 roster is ~30 cars).
    //
    // WaitChange (300 ticks = 3 s):
    //   If the user switches cars, the correct candidate changes once then re-stabilizes.
    //   Adding a changeScore bonus makes car-switching a strong discriminator.
    //   If no change is detected (normal case), we pick by Capture score alone.
    // ─────────────────────────────────────────────────────────────────────────

    if (m_carIdValPhase == CarIdValPhase::Resolved ||
        m_carIdValPhase == CarIdValPhase::Failed) return;

    if (m_carIdCandidates.empty()) return;

    // Initialize state on first call
    if (m_carIdCandState.empty()) {
        m_carIdCandState.resize(m_carIdCandidates.size());
        for (size_t i = 0; i < m_carIdCandidates.size(); ++i) {
            DWORD v = SafeReadDword(carBase + m_carIdCandidates[i], 0);
            m_carIdCandState[i].capturedVal = v;
            m_carIdCandState[i].prevVal     = v;
        }
        m_carIdValPhase = CarIdValPhase::Capture;
        m_carIdValTick  = 0;
        LOG_INFO("Telemetry: CarID validation started (%d candidates)",
                 (int)m_carIdCandidates.size());
    }

    constexpr int kCaptureTicks    = 200;  // 2 s
    constexpr int kWaitChangeTicks = 300;  // 3 s

    // ── Per-tick reads ────────────────────────────────────────────────────────
    for (size_t i = 0; i < m_carIdCandidates.size(); ++i) {
        auto& cs = m_carIdCandState[i];
        if (!cs.alive) continue;
        DWORD val = SafeReadDword(carBase + m_carIdCandidates[i], 0);

        switch (m_carIdValPhase) {
        case CarIdValPhase::Capture:
            if (val != cs.prevVal) ++cs.changeCount;
            break;
        case CarIdValPhase::WaitChange:
            if (val != cs.prevVal && val >= 1 && val < 256) {
                cs.score += 1.0f;  // change bonus: correct candidate switches cars
                LOG_DEBUG("Telemetry: CarID 0x%04X changed %u→%u",
                          m_carIdCandidates[i], cs.prevVal, val);
            }
            break;
        default: break;
        }
        cs.prevVal = val;
    }

    ++m_carIdValTick;

    auto bestAlive = [&]() -> int {
        int idx = -1; float best = -1.f;
        for (size_t i = 0; i < m_carIdCandState.size(); ++i) {
            if (m_carIdCandState[i].alive && m_carIdCandState[i].score > best) {
                best = m_carIdCandState[i].score;
                idx  = (int)i;
            }
        }
        return idx;
    };

    // ── Phase transitions ─────────────────────────────────────────────────────
    switch (m_carIdValPhase) {

    case CarIdValPhase::Capture:
        if (m_carIdValTick >= kCaptureTicks) {
            for (size_t i = 0; i < m_carIdCandidates.size(); ++i) {
                auto& cs = m_carIdCandState[i];
                // stabilityScore: 1.0 if never changed, 0.0 if changed ≥ 10×
                float stabilityScore = (cs.changeCount == 0)
                    ? 1.0f
                    : std::max(0.0f, 1.0f - static_cast<float>(cs.changeCount) / 10.0f);
                // rangeScore: bonus for values typical of a car roster index
                float rangeScore = (cs.capturedVal >= 1 && cs.capturedVal <= 100) ? 0.5f : 0.0f;
                cs.score = stabilityScore + rangeScore;
                if (stabilityScore < 0.8f) cs.alive = false;  // too unstable

                LOG_DEBUG("Telemetry: CarID 0x%04X = %u stable=%.2f range=%.2f score=%.2f",
                          m_carIdCandidates[i], cs.capturedVal,
                          stabilityScore, rangeScore, cs.score);
            }
            int n = (int)std::count_if(m_carIdCandState.begin(), m_carIdCandState.end(),
                                       [](const CarIdCandState& s) { return s.alive; });
            if (n == 0) {
                m_carIdValPhase = CarIdValPhase::Failed;
                LOG_INFO("Telemetry: CarID validation failed — no stable candidates");
            } else {
                m_carIdValPhase = CarIdValPhase::WaitChange;
                m_carIdValTick  = 0;
                LOG_INFO("Telemetry: CarID → WaitChange (%d candidates, "
                         "switch car in garage for bonus score)", n);
            }
        }
        break;

    case CarIdValPhase::WaitChange:
        if (m_carIdValTick >= kWaitChangeTicks) {
            int idx = bestAlive();
            if (idx >= 0) {
                m_carIdOffset   = m_carIdCandidates[idx];
                m_carIdValPhase = CarIdValPhase::Resolved;
                DWORD val = SafeReadDword(carBase + m_carIdOffset, 0);
                LOG_INFO("Telemetry: CarID resolved 0x%04X = %u (score=%.2f)",
                         m_carIdOffset, val, m_carIdCandState[idx].score);
            } else {
                m_carIdValPhase = CarIdValPhase::Failed;
                LOG_INFO("Telemetry: CarID validation failed");
            }
        }
        break;

    default: break;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

bool Telemetry::Init() {
    bool ok = false;

    if (g_Config.telemetry.usePatternScan)
        ok = ResolveViaPatternScan();

    if (!ok)
        ok = ResolveViaStaticPtr();

    // Build RPM candidate list from binary scan.
    // Done once at startup — safe because the game module is fully loaded by now.
    // Validation (picking the right offset) happens lazily in Read().
    BuildRpmCandidates();

    if (ok) {
        m_ready.store(true, std::memory_order_release);
        LOG_INFO("Telemetry: ready — carPtr=0x%08X", (DWORD)m_ptrCarPtr);
    } else {
        LOG_ERROR("Telemetry: could not resolve car pointer — "
                  "FFB will use steering-input-only fallback");
    }
    return ok;
}

bool Telemetry::ResolveViaPatternScan() {
    // Speedometer code reads: fld dword ptr [speed_addr]
    // Pattern: D9 05 ?? ?? ?? ??  followed by D8 4D (fmul)
    uintptr_t hit = ScanModule(PATTERN_SPEED_READ);
    if (hit) {
        uintptr_t sAddr = Deref(hit + 2);
        if (IsReadable(sAddr, sizeof(float))) {
            float v = SafeReadFloat(sAddr);
            if (v >= 0.0f && v < 300.0f) {
                m_addrSpeed = sAddr;
                LOG_INFO("Telemetry: speed pattern found at 0x%08X val=%.1f m/s",
                         (DWORD)sAddr, v);
            }
        }
    }

    bool staticOk = ResolveViaStaticPtr();
    return staticOk || (m_addrSpeed != 0);
}

bool Telemetry::ResolveViaStaticPtr() {
    DWORD ptr = g_Config.telemetry.ptrPlayerCarPtr;
    if (!IsReadable(ptr, sizeof(DWORD))) {
        LOG_ERROR("Telemetry: static ptr 0x%08X not readable", ptr);
        return false;
    }
    m_ptrCarPtr = ptr;
    DWORD ofs = g_Config.telemetry.ofsCarBase;
    if (ofs)
        LOG_INFO("Telemetry: 2-level chain: *(*(0x%08X) + 0x%02X) = car_base", ptr, ofs);
    else
        LOG_INFO("Telemetry: 1-level chain: *(0x%08X) = car_base", ptr);
    return true;
}

// ── Read (called every 10 ms) ──────────────────────────────────────────────────

TelemetryData Telemetry::Read() {
    TelemetryData d{};

    // Resolve car base via configurable pointer chain.
    // OfsCarBase == 0: carBase = *(DWORD*)ptrPlayerCarPtr              (1-level)
    // OfsCarBase != 0: carBase = *(DWORD*)(*(DWORD*)ptrPlayerCarPtr + OfsCarBase) (2-level)
    uintptr_t carBase = 0;
    if (m_ptrCarPtr) {
        uintptr_t level1 = Deref(m_ptrCarPtr);
        DWORD ofsCarBase = g_Config.telemetry.ofsCarBase;
        if (ofsCarBase && level1 > 0x00010000u)
            carBase = Deref(level1 + ofsCarBase);
        else
            carBase = level1;
    }
    d.playerCarValid = (carBase != 0);

    if (carBase) {
        // ── Primary reads ────────────────────────────────────────────────────
        d.speed      = SafeReadFloat(carBase + g_Config.telemetry.ofsSpeedMps,    0.0f);
        d.lateralAccel = SafeReadFloat(carBase + g_Config.telemetry.ofsLateralAccel, 0.0f);
        d.longAccel  = SafeReadFloat(carBase + OFS_LONG_ACCEL,    0.0f);
        d.steerAngle = SafeReadFloat(carBase + g_Config.telemetry.ofsSteerAngle,  0.0f);

        // ── Wheel spin (4 floats starting at OFS_WHEEL_SPIN_FL) ──────────────
        float wfl = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL,      0.0f);
        float wfr = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL + 4,  0.0f);
        float wrl = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL + 8,  0.0f);
        float wrr = SafeReadFloat(carBase + OFS_WHEEL_SPIN_FL + 12, 0.0f);
        d.wheelSpinMax = std::max({ wfl, wfr, wrl, wrr });

        // ── Slip angle estimate (if not directly available) ───────────────────
        // NFSU2 may not expose raw slip angle; we estimate from lateral accel
        // and speed: slip_deg ≈ atan(lat_g / speed_component) × 57.3
        // This approximation is "good enough" for FFB vibration scaling.
        if (d.speed > 0.5f) {
            float latG  = std::fabsf(d.lateralAccel) / 9.81f;
            float ratio = latG / (d.speed * 0.3f);  // rough: lat/speed ratio
            d.slipAngle = std::min(20.0f, std::atanf(ratio) * 57.295779f);
        }

        // ── Wheel load (estimate from long accel — weight transfer proxy) ─────
        float longG = std::fabsf(d.longAccel) / 9.81f;
        d.wheelLoad = std::min(1.0f, longG / 1.5f);

        // ── Gear ──────────────────────────────────────────────────────────────
        d.gear = static_cast<int>(SafeReadDword(carBase + OFS_GEAR, 0));
        if (d.gear < 0 || d.gear > 6) d.gear = 0; // clamp to valid range

        // ── RPM ───────────────────────────────────────────────────────────────
        // Priority:
        //   1. OFS_RPM != 0  — manual override in nfsu2_addresses.h (highest trust)
        //   2. m_rpmOffset   — auto-resolved via pattern scan + runtime validation
        //   3. 0.0f          — unresolved; FFB falls back to longAccel proxy
        {
            DWORD rpmOfs = (OFS_RPM != 0) ? OFS_RPM : m_rpmOffset;
            if (rpmOfs != 0) {
                d.rpm = SafeReadFloat(carBase + rpmOfs, 0.0f);
                d.rpm = std::max(0.0f, std::min(d.rpm, 15000.0f));
            } else {
                d.rpm = 0.0f;
                // Lazy validation: try to pick a candidate from binary scan.
                // Runs every frame until resolved — cheap (small vector, safe reads).
                if (!m_rpmCandidates.empty())
                    ValidateRpmOffset(carBase);
            }
        }

        // ── CarId + physics + engine curve ────────────────────────────────────
        if (!m_carIdCandBuilt)
            BuildCarIdCandidates(carBase);

        if (m_carIdOffset != 0) {
            d.carId = SafeReadDword(carBase + m_carIdOffset, 0);
        } else {
            d.carId = 0;
            if (!m_carIdCandidates.empty())
                ValidateCarId(carBase);
        }

        // Physics lookup — fast O(N) table search, safe to call every frame
        d.physics = GetCarData(d.carId);

        // On car change: log physics (engine curve hot-swap handled in wheel_input.cpp
        // TelemetryLoop to keep I/O out of this read path)
        if (d.carId != m_prevCarId) {
            if (m_prevCarId != 0 && d.carId != 0)
                LOG_INFO("Telemetry: carId changed %u → %u", m_prevCarId, d.carId);
            if (d.carId > 0)
                LOG_INFO("Telemetry: physics loaded mass=%.0f "
                         "frontGrip=%.2f rearGrip=%.2f",
                         d.physics.mass, d.physics.frontGrip, d.physics.rearGrip);
            m_prevCarId = d.carId;
        }

        // ── Collision delta ────────────────────────────────────────────────────
        float dv = m_prevSpeed - d.speed;
        d.collision = (dv > 0.0f) ? std::min(1.0f, dv / 5.0f) : 0.0f;

    } else if (m_addrSpeed) {
        // Pattern-scan fallback: only speed is known
        d.speed = SafeReadFloat(m_addrSpeed, 0.0f);
        d.speed = std::max(0.0f, d.speed);
    }

    m_prevSpeed = d.speed;

    // ── Normalize ──────────────────────────────────────────────────────────────
    float maxSpd = g_Config.telemetry.maxSpeedMps;
    float maxLat = g_Config.telemetry.maxLateralAccelMs2;

    d.speed       = std::max(0.0f, d.speed);
    d.speedNorm   = std::max(0.0f, std::min(1.0f, d.speed / maxSpd));
    d.lateralNorm = std::max(0.0f, std::min(1.0f, std::fabsf(d.lateralAccel) / maxLat));
    d.steerAngle  = std::max(-1.0f, std::min(1.0f, d.steerAngle));
    d.wheelSpinMax = std::max(0.0f, std::min(1.0f, d.wheelSpinMax));

    // ── Race state guess: assume in race if speed > 0.5 m/s ───────────────────
    d.inRace = d.speed > 0.5f;

    return d;
}
