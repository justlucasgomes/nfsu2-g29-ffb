# Changelog

All notable changes to Underground FFB are documented in this file.  
Format follows [Keep a Changelog 1.0.0](https://keepachangelog.com/en/1.0.0/).

---

## [0.6.0] — 2026-05-01

> **Focus:** Eliminate countersteer oscillation. Expose all tuning parameters to config. Code quality pass.

### Added

- **`[AntiOscillation]` config section** — 5 new parameters covering the entire
  anti-oscillation pipeline; all previously hardcoded values are now user-adjustable:
  `SteerIntentAlpha`, `SlipHeldRiseAlpha`, `SlipHeldFallAlpha`,
  `HoldThreshold`, `HoldDurationSec`

- **Driver intent smoothing** (`m_steerIntentSmoothed`, α=0.18 default)  
  Raw `steeringInput` is now passed through an EMA filter before reaching the
  slip assist and oscillation logic. Converts frame-by-frame jitter into a
  continuous intent signal; replaces raw input in `assistGate` and the
  smoothed-intent oscillation kill (Layer 5).

- **Rear slip hysteresis** (`m_rearSlipHeld`)  
  Tracks `rearSlipNorm` with asymmetric EMA: rise α=0.35 (fast), fall α=0.12
  (slow). Prevents the assist from toggling on/off during momentary grip
  fluctuations mid-correction. Replaces raw `rearSlipNorm` in `reboundGate`.

- **Assist hold window** (`m_assistHoldTimer`)  
  When `m_rearSlipHeld` exceeds `HoldThreshold`, a 250 ms timer is set.
  `cfRear` is scaled by `max(m_rearSlipHeld, holdFactor)` — full force during
  the window, then gradual release as the hysteresis envelope decays.

- **Center force late return** (Part 4)  
  `centerGate` now uses `m_rearSlipHeld` (quadratic: `1 - held²`) AND is fully
  zeroed while the hold timer is active. Center only returns after the
  correction cycle is complete.

- **Smoothed intent oscillation kill** (Part 5, `m_prevSteerIntent`)  
  Second oscillation damper using `|m_steerIntentSmoothed − m_prevSteerIntent|`
  with 30% max attenuation on `combined` ConstantForce. Targets slower
  intent reversals not caught by the existing raw-delta damper (25%).

- **Version string in startup log**  
  `[g29_ffb] NFSU2 G29 FFB Mod  v0.6.0` printed at `DLL_PROCESS_ATTACH`.

- **`CHANGELOG.md`** — this file.

### Changed

- `reboundGate` (SAT elastic rebound suppressor) now uses `m_rearSlipHeld`
  instead of raw `rearSlipNorm` — smoother, hysteresis-aware suppression
- `assistGate` alignment check now uses `m_steerIntentSmoothed` instead of
  raw `steeringInput`
- `centerGate` formula migrated from `kCF_RearSlip`-normalised `rearSlipMag`
  to `m_rearSlipHeld` (avoids forward-reference to a constant defined later
  in the function scope)
- `config.cpp`: `LogLevel` default changed from `"2"` (INFO) to `"1"` (errors
  only) to match the `GeneralConfig` struct default
- `CMakeLists.txt`: project version `2.0.0` → `0.6.0`

### Fixed

- **Dead code: `UpdateConstForce()`** — method defined and declared since v0.4.0
  but never called (ConstantForce is always set inline in `Update()`). Removed
  from both `force_feedback.cpp` and `force_feedback.h`.
- **`kDT` constant duplication** — `constexpr float DT/kDT/kHoldDT = 0.01f`
  appeared in 4 independent inner blocks within `ForceFeedback::Update()`.
  Consolidated into a single declaration at function scope.
- **Diagnostic thread in release build** — `InputMgrDumpThread` added during
  save-file reverse engineering was cleaned up before tagging this release.

### Removed

- `UpdateConstForce(float, float)` — dead method, never called

---

## [0.5.x] — 2026-04 (incremental development)

### Added

- **Center force quadratic suppression** — `centerGate` changed from
  `1 − rearSlipMag` (linear) to `1 − rearSlipMag²` (quadratic) for a faster
  collapse when rear slip is present
- **Rear slip intent filter** (`assistGate`) — `alignment = steeringInput ×
  m_rearSlipSmoothed`; reduces assist by up to 70% when driver is already
  correcting in the correct direction
- **SAT rebound gate** — elastic rebound contribution zeroed proportionally
  to `rearSlipNorm`; prevents spring from amplifying countersteer forces
- **Raw steering rate damper** — `deltaSteer = steeringInput − m_prevSteer`
  applies up to 25% attenuation to ConstantForce on fast steering reversals
- **SAT micro-texture layer** (v0.5.0) — micro-granulation from
  `|m_frontSlipTrend|` in the 65–100% threshold zone, max +4% spring
- **SAT slip frequency signature** (v0.5.1) — 2nd derivative of slip rate
  adds up to +3% spring when front axle exhibits rapid direction changes
- **Front grip memory** (v0.4.5) — tracks peak `frontSlip` and generates
  SAT recharge proportional to recovered grip after an overload event
- **SAT elastic rebound** (v0.4.8) — two-layer EMA with decaying impulse
  on grip re-seating; produces elastic "settle" feel vs. overdamped snap
- **Dynamic front slip gradient** (v0.4.4) — trend-aware SAT: same
  `frontSlip` value produces different torque depending on grip direction
- **Per-car physics multipliers** — `car_physics.cpp` supplies mass,
  frontGrip, rearGrip, steeringLock per `carId`; applied as scaling factors
  to slip thresholds, load transfer, and oversteer/understeer feedback
- **Engine curve hot-swap** — `engine_curve.cpp` loads per-car idle/redline
  RPM from `cars.ini`; drives accurate harmonic idle vibration without
  relying on `longAccel` proxy
- **Gear shift kick** — brief ConstantForce impulse + engineVib dip on
  gear change
- **Logitech G HUB RGB LEDs** — shift indicator via G HUB Legacy LED SDK

### Changed

- `ForceFeedback::Update()`: SAT moved from ConstantForce channel to Spring
  channel (physically correct — SAT is a spring torque, not a constant offset)
- Spring budget system introduced: each component (speed, SAT, longitudinal LT,
  lateral LT, front load, caster, center hold) is capped to its allocated slice
  before summing; prevents silent saturation

---

## [0.4.0] — 2026-03 (initial public prototype)

### Added

- DirectInput8 proxy architecture (`dinput8.dll` replaces system DLL)
- G29 device enumeration (VID `046D`, PID `C24F`) and full axis normalization
- 4 initial FFB effects: Spring, Damper, ConstantForce, SlipVibration
- FH5-style 3-point speed curve for spring weight
- Rear Slip Assist — asymmetric EMA (rise α=0.20, fall α=0.06)
- Lateral load transfer: G → spring stiffening
- Longitudinal load transfer: braking loads front axle
- Front tire load SAT: lateral G proportional spring boost
- Caster return torque
- Center hold zone
- Rack inertia / release torque
- Straight-line stability micro-damp
- Low-speed hydraulic assist
- High-speed steering damping
- Road texture (47 Hz) + corner rack kick
- Front scrub vibration (configurable Hz)
- Understeer feedback: spring lightening when front saturates
- Collision impulse (speed-delta spike)
- Curb pulse (50 Hz square wave on kerb contact)
- Engine idle vibration: dual-harmonic synthesis
- Throttle cut transient
- AC MIN_FF offset applied to all non-zero forces
- `config.ini` with full parameter exposure
- Built-in ASI loader for `scripts\*.asi`
- Pattern scanner for dynamic NFSU2 address resolution
- Race-gate filter: pedal axes suppressed in menus (speed-based latch)
- Per-car physics from `cars.ini`
