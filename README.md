# G29 FFB Wrapper — NFS Underground 2

A `dinput8.dll` proxy that gives the **Logitech G29** complete steering wheel support in NFSU2, with force feedback modelled after **Assetto Corsa** (precision) and **Forza Horizon 5** (feel).

> Other languages: [Português](README_PT.md) · [Español](README_ES.md) · [Français](README_FR.md) · [Italiano](README_IT.md)

---

## Requirements

| | |
|---|---|
| Game | Need for Speed Underground 2 — NA retail (`SPEED2.EXE` ~4.8 MB) |
| Wheel | Logitech G29 with **G HUB** installed |
| OS | Windows 10 / 11 (64-bit) |
| Build tools | Visual Studio 2019/2022/2026 Community + CMake 3.20+ |

---

## G HUB Setup (required before playing)

Open **Logitech G HUB** and set:

| Setting | Value |
|---|---|
| **Operating Range (Angle)** | **180°** |
| Sensitivity | 50 |
| Centering Spring | 20 |

![G HUB config](logitechGHUB.jpg)

> The mod's dynamic steering (240° → 180° → 120° with speed) is calibrated for 180° G HUB operating range.

---

## Quick Install

1. **Configure G HUB** (see above)
2. **Build** the DLL (see [Build](#build))
3. Copy `dinput8.dll` → NFSU2 root folder
4. Copy `config.ini` → NFSU2 root folder
5. Launch the game — wheel is ready

> The mod includes a built-in ASI loader. Existing mods in `scripts\` continue to work.

---

## Build

```bat
rem double-click, or run from any prompt:
mods\g29_ffb\build.bat
```

Or manually (x86 required — NFSU2 is 32-bit):

```cmd
cmake -S mods\g29_ffb -B build_output -A Win32
cmake --build build_output --config Release
```

The post-build step copies `dinput8.dll` to your NFSU2 folder automatically.

---

## Features

### Input
| Feature | Detail |
|---|---|
| G29 detection | VID=046D / PID=C24F via DirectInput 8 |
| Axis mapping | X=steering · Y=gas · Rz=brake · Slider[0]=clutch |
| Steering range | 900° physical, configurable virtual lock |
| Brake curve | Gamma=2.4 (Assetto Corsa reference) — progressive |
| Smoothing | EMA per axis (steering α=0.35, pedals α=0.5) |
| Dynamic steering lock | FH5-style: 240° city → 180° mid → 120° highway |
| Yaw assist | Progressive steering amplification at high speed |
| Menu fix | Pedal axes neutralized in menus — no phantom DOWN inputs |

### Force Feedback — 8 DirectInput effects
| # | Effect | Description |
|---|---|---|
| 1 | Spring | FH5 3-point speed curve · load transfer · caster return · SAT |
| 2 | Damper | FH5 base + rack inertia + rack release + straight-line stability |
| 3 | ConstantForce | Lateral G + SAT + rear slip assist + engine idle vibration |
| 4 | SlipVibration | 26 Hz sine — traction loss / wheel spin |
| 5 | RoadTexture | 47 Hz sine — road surface + cornering rack kick |
| 6 | Collision | Square wave impulse on sudden speed loss |
| 7 | Curb | 50 Hz square burst on rumble strip |
| 8 | FrontScrub | 32 Hz sine — front tire scrub near grip limit |

### Spring composition (per frame)
```
finalSpring = FH5SpeedScale(speed)
            × (1 − gripBlend × understeerReduction)
            + loadTransfer
            + frontLoadSAT
            + casterReturn
            + centerHold(with highSpeedRelax + loadFade)
            − lowSpeedAssist
            − rearGripLightening
            + roadLoadOscillation(~8 Hz, 2%)
```

### Damper composition (per frame)
```
finalDamper = highSpeedDamping
            + rackInertia
            − rackRelease
            + straightLineDamping
```

---

## File structure

```
NFSU2\
├── SPEED2.EXE
├── dinput8.dll          ← this mod
├── config.ini           ← G29 FFB configuration
├── logs\
│   └── g29_ffb.log      ← diagnostic log
└── scripts\
    └── *.asi            ← existing mods still work

mods\g29_ffb\
├── src\                 ← C++ source
├── config.ini           ← source template
├── build.bat
├── CMakeLists.txt
├── README.md            ← this file
├── README_PT.md
├── README_ES.md
├── README_FR.md
├── README_IT.md
└── INSTALL.md           ← full config reference + troubleshooting
```

---

## Configuration

All parameters live in `config.ini` in the NFSU2 root. Changes take effect on the next game launch (no recompile needed).

See [INSTALL.md](INSTALL.md) for the full parameter reference.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| G29 not detected | Start G HUB before the game; try DirectInput compatibility mode in G HUB |
| No FFB at all | Check log: `FFB: device does not support force feedback` |
| Wheel too heavy | Lower `HighSpeedWeight` + `CenterSpring` |
| Wheel too light | Raise `LowSpeedWeight` / `MidSpeedWeight` |
| Brake too sensitive | Lower `BrakeGamma` toward 1.0 |
| Menu scrolls alone | Already fixed — pedal axes are neutralized in menus |
| Physics feels wrong | Set `LogLevel=3`, check `Tele:` lines in log |
| ASI mods missing | Confirm `.asi` files are in `scripts\`; check log for `ASI:` lines |

---

## References

| Source | File | Values used |
|---|---|---|
| Assetto Corsa (G29) | `Documents\Assetto Corsa\cfg\controls.ini` | MIN_FF=0.05 · CURBS=0.4 · ROAD=0.5 · BRAKE_GAMMA=2.4 · LOCK=900 |
| Forza Horizon 5 | `XboxGames\FH5\Content\media\ControllerFFB.ini` | SpringScaleSpeed0/1 · InRaceSpringMaxForce=0.5 · DampingMaxForce=0.31 · RightVibrationWavelength=21ms · LeftVibrationWavelength=38ms |

*NFSU2 NA retail · MD5: 665871070B0E4065CE446967294BCCFA*
