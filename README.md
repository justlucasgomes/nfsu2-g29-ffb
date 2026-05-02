<div align="center">

# Underground FFB

### Modern force feedback physics for Need for Speed Underground 2

[![Version](https://img.shields.io/badge/version-0.6.0-2ea44f?style=for-the-badge)](CHANGELOG.md)
[![Build](https://img.shields.io/badge/build-passing-brightgreen?style=for-the-badge)]()
[![Platform](https://img.shields.io/badge/Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)]()
[![License](https://img.shields.io/badge/license-MIT-blue?style=for-the-badge)](LICENSE)

**The only NFSU2 mod that brings Forza Horizon 5 + Assetto Corsa quality force feedback to a 2004 classic.**

[Download](#installation) · [Configuration](#configuration) · [Changelog](CHANGELOG.md) · [Report Bug](../../issues)

</div>

---

> **Other languages:** [Português](README_PT.md) · [Español](README_ES.md) · [Français](README_FR.md) · [Italiano](README_IT.md)

---

## Preview

```
┌─────────────────────────────────────────────────────────────────┐
│  Speed: 187 km/h    Lateral G: 1.8g    Rear slip: 0.42          │
│                                                                  │
│  Spring ████████░░  75%   [ SAT + speed curve active ]          │
│  Damper ███░░░░░░░  28%   [ rack inertia + high-speed ]         │
│  ConstF ██████░░░░  58%   [ lateral G + slip assist ]           │
│  Road   ██░░░░░░░░  18%   [ 47 Hz texture ]                     │
│  Slip   ████░░░░░░  40%   [ 26 Hz vibration ]                   │
│                                                                  │
│  AntiOsc: intent=0.31  held=0.38  holdTimer=0.18s  kill=0.94   │
└─────────────────────────────────────────────────────────────────┘
```

*A screen capture / GIF of the wheel in action can be added here.*

---

## Why This Mod Is Different

Most NFSU2 wheel mods just remap axes. This one builds a complete physics simulation layer on top of DirectInput:

| Feature | Generic mods | Underground FFB |
|---|---|---|
| Spring weight | Static | FH5-style 3-point speed curve |
| SAT | None | Pneumatic trail simulation |
| Countersteer assist | None | Rear slip assist with **anti-oscillation** |
| Load transfer | None | Lateral + longitudinal G → spring |
| Understeer feel | None | Front slip gradient + scrub vibration |
| Engine vibration | None | Dual-harmonic synthesis at idle |
| Per-car physics | None | Mass, grip, lock from `cars.ini` |
| Oscillation | Common problem | 5-layer elimination system |
| Configuration | None | 50+ parameters in `config.ini` |

---

## Features

### Core FFB Engine
- **7 simultaneous DirectInput effects** — Spring, Damper, ConstantForce, SlipVibration, RoadTexture, Collision, Curb
- **FH5-style speed curve** — 3-point interpolation, light at standstill, progressively heavier at speed
- **Self-Aligning Torque (SAT)** — pneumatic trail simulation via Spring channel; peaks before front saturation, falls during understeer
- **AC brake gamma 2.4** — precise, progressive pedal feel calibrated from real Assetto Corsa G29 profiles

### Countersteer & Oversteer
- **Rear Slip Assist** — corrective countersteer torque via ConstantForce, triggered by rear wheel spin
- **Anti-Oscillation System** *(v0.6.0)* — 5-layer architecture eliminating the zig-zag loop that plagues naive slip assists:
  - Driver intent smoothing (~55 ms EMA)
  - Rear slip hysteresis (fast rise, slow fall)
  - 250 ms hold window after threshold
  - Center force suppression during correction
  - Smoothed intent oscillation kill

### Road Feel
- **Front scrub vibration** — configurable Hz sine at front tire saturation limit
- **Road texture** — 47 Hz surface detail, speed-filtered
- **Rack kick** — corner texture additive to road feel
- **Curb pulse** — 50 Hz square wave on kerb contact
- **Collision impulse** — speed-delta triggered impact force

### Simulation Detail
- **Engine idle vibration** — dual-harmonic synthesis (base tone + torque load); dies at speed
- **Gear shift kick** — brief ConstantForce impulse + engineVib dip on gear change
- **Dynamic steering ratio** — lock reduces with speed, FH5-style (240° low → 120° high)
- **Per-car physics** — mass, frontGrip, rearGrip, steeringLock loaded per car from `cars.ini`
- **Logitech G HUB RGB** — shift indicator LEDs via G HUB Legacy SDK

### Integration
- **Built-in ASI loader** — loads `scripts\*.asi` mods transparently
- **Pattern scanner** — auto-detects NFSU2 game memory addresses at runtime
- **Race-gate filter** — pedal axes suppressed in menus (speed-based latch, prevents phantom scrolling)
- **Zero-configuration** — works out of the box with defaults; `config.ini` for tuning

---

## Requirements

| | Requirement |
|---|---|
| Game | Need for Speed Underground 2 — PC (NA retail recommended) |
| Wheel | Logitech G29 |
| Driver | Logitech G HUB (latest version) |
| OS | Windows 10 / 11 (64-bit host, 32-bit game process) |
| Runtime | [Visual C++ Redistributable 2022 (x86)](https://aka.ms/vs/17/release/vc_redist.x86.exe) |

> **G HUB setup:** In G HUB → G29 profile → set mode to **PC / DirectInput / Compatibility**. G HUB must be running *before* you launch the game.

---

## Installation

### Option A — Release zip (recommended)

1. Download `underground-ffb-v0.6.0.zip` from the [Releases](../../releases) page
2. Extract to your NFSU2 game folder (same directory as `SPEED2.EXE`):
   ```
   Need for Speed Underground 2\
   ├── SPEED2.EXE
   ├── dinput8.dll   ← from the zip (replaces any existing)
   ├── winmm.dll     ← from the zip
   ├── config.ini    ← from the zip (do NOT overwrite if you have custom settings)
   └── cars.ini      ← from the zip
   ```
3. Launch G HUB, then launch NFSU2

**Verify installation** — open `logs\g29_ffb.log` after the game starts:
```
[g29_ffb] NFSU2 G29 FFB Mod  v0.6.0
[g29_ffb] WheelInput: G29 found — 'Logitech G29 Racing Wheel'
[g29_ffb] FFB: 7 effects ready  (FH5 speed curve + AC enhancements)
```

### Option B — Build from source

```cmd
git clone https://github.com/justlucasgomes/nfsu2-g29-ffb
cd nfsu2-g29-ffb
build.bat
```

> Requires: Visual Studio 2019/2022 with **Desktop development with C++** workload.

The post-build step deploys `dinput8.dll` and `config.ini` automatically. To deploy to a custom path:
```cmd
cmake -S . -B build_output -A Win32 -DNFSU2_DIR="C:\Games\NFSU2"
cmake --build build_output --config Release
```

---

## Configuration

All settings are in `config.ini` in your NFSU2 game folder. Changes apply on next game launch.

---

### [ForceFeedback] — Core effects

| Parameter | Default | Range | Description |
|---|---|---|---|
| `Enabled` | `1` | 0 / 1 | Master switch for all FFB effects |
| `LowSpeedWeight` | `20` | 0–100 | Spring % at standstill (FH5 speed curve point 1) |
| `MidSpeedWeight` | `50` | 0–100 | Spring % at ~36 km/h (point 2) |
| `HighSpeedWeight` | `75` | 0–100 | Spring % at 108+ km/h (point 3, plateau) |
| `CenterSpring` | `50` | 0–100 | SAT (Self-Aligning Torque) gain |
| `MinimumForce` | `5` | 0–20 | Minimum force offset added to all non-zero forces (AC MIN_FF=0.05) |
| `DamperStrength` | `18` | 0–100 | Steering resistance base (adds to FH5 base damper) |
| `LateralForce` | `50` | 0–100 | Cornering G → ConstantForce gain |
| `RoadTexture` | `12` | 0–100 | Road surface vibration amplitude (47 Hz, FH5 right motor) |
| `CornerTextureStrength` | `18` | 0–100 | Additive rack kick in corners |
| `SlipVibration` | `25` | 0–100 | Tire slip vibration (26 Hz, FH5 left motor) |
| `CurbEffect` | `35` | 0–100 | Kerb strike pulse amplitude |
| `CurbPulseMs` | `80` | 20–500 | Kerb pulse duration (ms) |
| `UndersteerLightening` | `30` | 0–100 | Spring reduction when front tires saturate |
| `CollisionForce` | `55` | 0–100 | Impact impulse amplitude |
| `CollisionDurationMs` | `60` | 20–500 | Impact impulse duration (ms) |

#### Rear Slip Assist

| Parameter | Default | Range | Description |
|---|---|---|---|
| `RearSlipAssistStrength` | `35` | 0–100 | Countersteer corrective torque amplitude |
| `RearSlipThreshold` | `0.18` | 0.0–1.0 | Wheel spin level to activate assist |
| `RearSlipMaxTorque` | `30` | 0–100 | Assist torque hard clamp (% of max) |
| `RearSlipMinSpeed` | `50.0` | km/h | Speed below which assist is disabled |
| `RearLightenStrength` | `12` | 0–100 | Spring reduction proportional to rear slip |

#### Load Transfer

| Parameter | Default | Range | Description |
|---|---|---|---|
| `LoadTransferGain` | `35` | 0–100 | Lateral G spring stiffening gain |
| `LoadTransferMax` | `30` | 0–100 | Max lateral load transfer contribution |
| `LongLoadStrength` | `20` | 0–100 | Braking front-axle loading (trail braking feel) |
| `FrontLoadStrength` | `25` | 0–100 | Front tire lateral G SAT boost |

#### Steering Feel

| Parameter | Default | Range | Description |
|---|---|---|---|
| `CasterReturnStrength` | `22` | 0–100 | Return-to-center pull proportional to speed |
| `HighSpeedDampingStrength` | `20` | 0–100 | Damper boost for fast steering at highway speed |
| `RackInertiaStrength` | `18` | 0–100 | Resistance proportional to steering acceleration (column mass feel) |
| `RackReleaseStrength` | `12` | 0–100 | Damper reduction on fast wheel deceleration (fluid unwinding) |
| `StraightLineStability` | `15` | 0–100 | Micro-damp near center at low lateral G (filters road crown) |
| `LowSpeedAssistStrength` | `10` | 0–100 | Spring reduction at standstill (hydraulic assist simulation) |

#### Engine Vibration

| Parameter | Default | Range | Description |
|---|---|---|---|
| `EnableEngineIdleVibration` | `1` | 0 / 1 | Dual-harmonic engine texture at idle |
| `RevVibrationStrength` | `2` | 0–10 | Engine-load harmonic amplitude |
| `CutVibrationStrength` | `1` | 0–10 | Throttle-cut transient amplitude |
| `IdleSpeedThresholdKmh` | `10` | km/h | Speed above which idle vibration fades out |

---

### [AntiOscillation] — Zig-zag elimination (v0.6.0)

The anti-oscillation system solves the most common problem with slip assist implementations: the wheel oscillating left-right-left during countersteer correction. Five layers work together:

```
Raw input → [Intent smooth] → [Assist gate] → [Slip held] → [Hold window] → [Oscillation kill]
```

| Parameter | Default | Range | Description |
|---|---|---|---|
| `SteerIntentAlpha` | `0.18` | 0.01–1.0 | EMA α for intent smoothing. Lower = smoother/slower, higher = faster/noisier. 0.18 ≈ 55 ms at 10 ms tick |
| `SlipHeldRiseAlpha` | `0.35` | 0.01–1.0 | How fast the held-slip envelope rises when slip increases |
| `SlipHeldFallAlpha` | `0.12` | 0.01–1.0 | How fast it falls. Lower = longer hold after grip recovers |
| `HoldThreshold` | `0.35` | 0.0–1.0 | Slip magnitude that arms the hold window |
| `HoldDurationSec` | `0.25` | 0.0–2.0 | Seconds to sustain assist at full after threshold crossed |

**Tuning guide:**

| Feel | Adjustment |
|---|---|
| Wheel still oscillates | Lower `SlipHeldFallAlpha` → 0.08; raise `HoldDurationSec` → 0.35 |
| Assist feels sticky / late to release | Raise `SlipHeldFallAlpha` → 0.18; lower `HoldDurationSec` → 0.15 |
| Response feels delayed | Raise `SteerIntentAlpha` → 0.25 |
| Response feels twitchy | Lower `SteerIntentAlpha` → 0.12 |

---

### [CenterHold] — Stability at center

| Parameter | Default | Description |
|---|---|---|
| `CenterHoldRange` | `0.06` | ±fraction of full lock where the hold zone is active |
| `CenterHoldStrength` | `0.10` | Base hold force at low speed |
| `CenterHoldHighSpeedBoost` | `0.22` | Additional force at 180+ km/h |
| `CenterHoldSmooth` | `0.15` | EMA smoothing alpha for hold transitions |

---

### [FrontGripFeedback] — Understeer communication

| Parameter | Default | Description |
|---|---|---|
| `FrontSlipThreshold` | `0.35` | `abs(steerAngle) × lateralNorm` level to activate scrub vibration |
| `ScrubGain` | `20` | Scrub vibration amplitude (%) |
| `ScrubFrequency` | `32` | Scrub frequency (Hz) |
| `ScrubMax` | `18` | Maximum scrub amplitude cap (%) |
| `UndersteerThreshold` | `0.55` | frontSlip level to begin spring lightening |
| `UndersteerMaxReduction` | `25` | Maximum spring reduction under understeer (%) |

---

### [ShiftKick] — Gear-change feel

| Parameter | Default | Description |
|---|---|---|
| `ShiftKickEnabled` | `1` | Enable gear-change impulse |
| `ShiftKickStrength` | `15` | Impulse strength (%) |
| `ShiftKickDurationMs` | `30` | Impulse duration (ms) |

---

### [Input] — Axis configuration

| Parameter | Default | Description |
|---|---|---|
| `SteeringRange` | `900` | Physical wheel rotation in degrees |
| `VirtualSteeringLock` | `200` | In-game steering lock (degrees) |
| `SteeringGamma` | `1.15` | Response curve — 1.0 = linear, >1.0 = progressive center |
| `BrakeGamma` | `2.4` | Brake pedal curve (AC reference value) |
| `SteeringDeadzone` | `0.005` | Software deadzone after axis normalization |
| `PedalDeadzone` | `0.01` | Pedal deadzone (applied before gamma) |
| `DynamicSteering` | `1` | FH5-style lock reduction with speed |
| `LowSpeedLock` | `240` | Effective lock at 0 km/h |
| `MidSpeedLock` | `180` | Effective lock at 40 km/h |
| `HighSpeedLock` | `120` | Effective lock at 120+ km/h |
| `HighSpeedSensitivity` | `1.15` | Steering amplification at highway speed |
| `YawAssistStrength` | `0.25` | Progressive steering amplification at speed (0 = off) |
| `InvertSteering` | `0` | Flip steering axis |
| `InvertBrake` | `0` | Flip brake axis |

---

### [Telemetry] — Game memory

| Parameter | Default | Description |
|---|---|---|
| `UsePatternScan` | `1` | Auto-detect game addresses via pattern scan (recommended) |
| `PtrPlayerCarPtr` | `0x00000000` | Static fallback address (leave 0 when UsePatternScan=1) |
| `MaxSpeedMps` | `88.0` | Top speed for normalization (~317 km/h) |
| `MaxLateralAccelMs2` | `25.0` | Max lateral G for normalization (~2.5G) |

---

### [Engine] — Per-car RPM

| Parameter | Default | Description |
|---|---|---|
| `CarName` | `default` | Must match a section header in `cars.ini` |
| `IdleRpm` | `800` | Fallback idle RPM (used when CarName not in cars.ini) |
| `RedlineRpm` | `8000` | Fallback redline RPM |

---

### [General]

| Parameter | Default | Description |
|---|---|---|
| `LogLevel` | `1` | 0 = none, 1 = errors, 2 = info, 3 = debug |
| `DisableASI` | `0` | Disable the built-in ASI loader |

---

## Recommended Settings for Logitech G29

These values were calibrated against the G29's actual axis layout (confirmed from Assetto Corsa `controls.ini`) and reference profiles from both AC and FH5.

### Balanced (default profile)

```ini
[ForceFeedback]
LowSpeedWeight=20
MidSpeedWeight=50
HighSpeedWeight=75
CenterSpring=70
MinimumForce=5
LateralForce=65
RearSlipAssistStrength=35

[Input]
SteeringRange=900
VirtualSteeringLock=200
BrakeGamma=2.4
DynamicSteering=1
```

### Sim-style (more direct, less assist)

```ini
[AntiOscillation]
SteerIntentAlpha=0.25
SlipHeldFallAlpha=0.18
HoldDurationSec=0.18

[ForceFeedback]
RearSlipAssistStrength=25
CenterSpring=55
HighSpeedWeight=80
```

### Street / casual (more forgiving)

```ini
[AntiOscillation]
SteerIntentAlpha=0.12
SlipHeldFallAlpha=0.08
HoldDurationSec=0.35

[ForceFeedback]
RearSlipAssistStrength=45
LowSpeedWeight=15
```

---

## Troubleshooting

**Wheel not detected at startup**
- Verify G HUB is running *before* the game
- Check `logs\g29_ffb.log` — look for `G29 not found`
- In G HUB: set G29 to **PC / DirectInput / Compatibility** mode
- Try unplugging and reconnecting the wheel with G HUB already open

**Force feedback not working at all**
- Check `logs\g29_ffb.log` for `FFB: device does not support force feedback`
- In Windows **Game Controllers**: open G29 properties → disable any hardware auto-centering
- Ensure no other exclusive-mode FFB application is open (e.g., Logitech Gaming Software)

**Menus scroll by themselves (phantom input)**
- The race-gate latch prevents pedals from reaching menus once you're driving
- If you see scrolling *before* your first race, set `LogLevel=2` and check for `inRace=true` transitions
- Workaround: go into any race once, return to menu — gate resets cleanly

**Countersteer zig-zag / oscillation**
1. Set `LogLevel=3`, drive until oscillation appears, check the `AntiOsc` debug lines
2. Start with: `SlipHeldFallAlpha=0.08`, `HoldDurationSec=0.35`
3. If still oscillating: `SteerIntentAlpha=0.12`

**Too much force / hand fatigue**
- Reduce `LowSpeedWeight`, `MidSpeedWeight`, `HighSpeedWeight` by 10–15 each
- Reduce `LateralForce` to 45–50
- Keep `MinimumForce` ≤ 8

**Log shows `Telemetry: could not resolve car pointer`**
- FFB still works via steering-only fallback (no physics-based forces, basic spring + damper)
- Set `LogLevel=3` and file an issue with the full log attached

**G29 not in DirectInput mode after G HUB update**
- G HUB sometimes resets wheel profiles on updates
- Recreate the profile: G HUB → G29 → New Profile → set to PC mode → set as default for SPEED2.EXE

---

## FAQ

**Does this work with NFSOR (online restoration)?**
Yes. The mod operates entirely at the DirectInput layer and is invisible to the online component.

**Does this work with non-NA versions of the game?**
The pattern scanner finds game addresses dynamically, so it should work on most versions. If telemetry fails, adjust addresses manually in `[Telemetry]` using Cheat Engine.

**Will this break my existing ASI mods?**
No. The mod includes a transparent ASI loader that forwards all `scripts\*.asi` files. Your existing mods continue to work.

**Can I use this with a different wheel (G920, T300RS, etc.)?**
The wheel detection is hardcoded to the G29 VID/PID (`046D:C24F`). Other wheel support is planned — see [Roadmap](#roadmap).

**Does changing `config.ini` require a game restart?**
Yes — settings are loaded once at DLL load time. Hot-reload is planned.

**Why is there a `winmm.dll` in the package?**
NFSU2 uses legacy WinMM audio timing functions. The proxy stubs those calls to prevent conflicts with modern Windows audio, while forwarding all other calls to the real system DLL.

**The wheel feels too heavy at low speed, is that normal?**
Reduce `LowSpeedWeight` to 10–15. Low-speed steering is intentionally lighter to simulate hydraulic power steering.

---

## How It Works

```
SPEED2.EXE
  └─ LoadLibrary("dinput8.dll")          ← Underground FFB proxy
       ├─ DirectInput8Create()           ← forwards to system dinput8.dll
       ├─ GetDeviceData() / GetDeviceState()
       │    └─ race-gate filter          ← pedal axes blocked in menus
       └─ WheelInput thread (100 Hz)
            ├─ Telemetry::Read()
            │    └─ speed / lateralG / steerAngle / rpm / wheelSpin
            ├─ G29 poll
            │    └─ normalize → deadzone → dynamic lock → gamma → smooth
            └─ ForceFeedback::Update()
                 ├─ Spring   (FH5 speed curve + SAT + load transfer)
                 ├─ Damper   (rack inertia + high-speed + stability)
                 ├─ ConstF   (lateral G + rear slip assist + anti-osc)
                 ├─ Sine 26Hz (slip vibration)
                 ├─ Sine 47Hz (road texture)
                 ├─ Square   (collision impulse / curb pulse)
                 └─ ConstF   (shift kick)
```

---

## Roadmap

| Priority | Feature |
|---|---|
| High | Default controller profile patch (eliminate login-screen phantom scroll permanently) |
| High | `config.ini` hot-reload — apply changes without game restart |
| Medium | Force limiter global clamp — protect motor during long sessions |
| Medium | Wheel-agnostic detection — G920, G923, T300RS support |
| Medium | Per-car preset system — automatic profile switch on car change |
| Low | Telemetry dashboard — real-time FFB channel visualization |
| Low | NFSU1 compatibility |

---

## Project Structure

```
nfsu2-g29-ffb/
├── src/
│   ├── dllmain.cpp          Entry point, ASI loader, version string
│   ├── dinput_proxy.cpp     IDirectInput8A/Device proxy, race-gate filter
│   ├── wheel_input.cpp      G29 acquisition, axis pipeline, 10 ms loop
│   ├── force_feedback.cpp   7 FFB effects, full physics engine
│   ├── telemetry.cpp        In-process NFSU2 memory reads + pattern scan
│   ├── config.cpp           INI parser (GetPrivateProfileString)
│   ├── engine_curve.cpp     Per-car RPM curve hot-swap
│   ├── car_physics.cpp      Per-car mass/grip/lock registry
│   ├── pattern_scan.cpp     Dynamic address resolution from EXE patterns
│   └── logitech_led.cpp     G HUB Legacy LED SDK integration
├── config.ini               User configuration (deployed to game root)
├── cars.ini                 Per-car physics overrides
├── CMakeLists.txt
├── build.bat
├── CHANGELOG.md
└── LICENSE
```

---

## Contributing

Pull requests are welcome. For major changes, please open an issue first.

- Follow [Conventional Commits](https://www.conventionalcommits.org/) for commit messages
- Run the build with `/W3` (already set in CMakeLists.txt) — no new warnings
- Test on NA retail (`MD5: 665871070B0E4065CE446967294BCCFA`) before submitting

---

## Acknowledgements

- **Forza Horizon 5** — speed curve architecture, damping reference values (`ControllerFFB.ini`)
- **Assetto Corsa** — brake gamma 2.4, MIN_FF=0.05, G29 axis layout (`controls.ini`)
- **NFSU2 modding community** — reverse engineering research (NFSPlugins, NFSU2ExtraOptions, nfscars forums)

---

## License

[MIT](LICENSE) — free to use, modify, and distribute with attribution.
