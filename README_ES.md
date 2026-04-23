# G29 FFB Wrapper — NFS Underground 2

Proxy `dinput8.dll` que otorga al **Logitech G29** soporte completo de volante en NFSU2, con force feedback modelado según **Assetto Corsa** (precisión) y **Forza Horizon 5** (sensación).

> Otros idiomas: [English](README.md) · [Português](README_PT.md) · [Français](README_FR.md) · [Italiano](README_IT.md)

---

## Requisitos

| | |
|---|---|
| Juego | Need for Speed Underground 2 — versión NA (`SPEED2.EXE` ~4.8 MB) |
| Volante | Logitech G29 con **G HUB** instalado |
| Sistema | Windows 10 / 11 (64-bit) |
| Compilación | Visual Studio 2019/2022/2026 Community + CMake 3.20+ |

---

## Configuración de G HUB (obligatorio antes de jugar)

Abra **Logitech G HUB** y configure:

| Ajuste | Valor |
|---|---|
| **Rango de operación (Ángulo)** | **180°** |
| Sensibilidad | 50 |
| Fuerza del muelle centrador | 20 |

![Configuración G HUB](logitechGHUB.jpg)

> El sistema de dirección dinámica del mod (240° → 180° → 120° con la velocidad) está calibrado para 180° de rango en G HUB.

---

## Instalación rápida

1. **Configurar G HUB** (ver arriba)
2. **Compilar** la DLL (ver [Compilación](#compilación))
3. Copiar `dinput8.dll` → carpeta raíz de NFSU2
4. Copiar `config.ini` → carpeta raíz de NFSU2
5. Iniciar el juego — el volante ya está activo

> El mod incluye un cargador de ASI integrado. Los mods existentes en `scripts\` siguen funcionando.

---

## Compilación

```bat
rem doble clic, o ejecutar desde cualquier símbolo del sistema:
mods\g29_ffb\build.bat
```

O manualmente (x86 obligatorio — NFSU2 es de 32 bits):

```cmd
cmake -S mods\g29_ffb -B build_output -A Win32
cmake --build build_output --config Release
```

El paso post-build de CMake copia `dinput8.dll` a la carpeta de NFSU2 automáticamente.

---

## Características

### Entrada
| Función | Detalle |
|---|---|
| Detección del G29 | VID=046D / PID=C24F vía DirectInput 8 |
| Mapeo de ejes | X=dirección · Y=acelerador · Rz=freno · Slider[0]=embrague |
| Rango de dirección | 900° físico, bloqueo virtual configurable |
| Curva de freno | Gamma=2.4 (referencia Assetto Corsa) — progresiva |
| Suavizado | EMA por eje (dirección α=0.35, pedales α=0.5) |
| Bloqueo dinámico | Estilo FH5: 240° ciudad → 180° media → 120° autopista |
| Asistencia de guiñada | Amplificación progresiva a alta velocidad |
| Corrección de menú | Ejes de pedal neutralizados en menús — sin entradas fantasmas |

### Force Feedback — 8 efectos DirectInput
| # | Efecto | Descripción |
|---|---|---|
| 1 | Spring | Curva de velocidad FH5 · transferencia de carga · torque de retorno · SAT |
| 2 | Damper | Base FH5 + inercia de cremallera + release + estabilidad en recta |
| 3 | ConstantForce | G lateral + SAT + asistencia de sobreviraje + vibración del motor |
| 4 | SlipVibration | Senoide 26 Hz — pérdida de tracción / giro de rueda |
| 5 | RoadTexture | Senoide 47 Hz — textura del asfalto + rack kick en curva |
| 6 | Collision | Impulso en onda cuadrada ante desaceleración brusca |
| 7 | Curb | Ráfaga 50 Hz al pisar bordillos |
| 8 | FrontScrub | Senoide 32 Hz — deslizamiento del neumático delantero en el límite |

---

## Configuración

Todos los parámetros están en `config.ini` en la carpeta raíz de NFSU2. Los cambios surten efecto en el siguiente inicio del juego — sin necesidad de recompilar.

Consulte [INSTALL.md](INSTALL.md) para la referencia completa de parámetros.

### Ajustes rápidos más comunes

| Síntoma | Parámetro | Ajuste |
|---|---|---|
| Volante demasiado pesado | `HighSpeedWeight` · `CenterSpring` | Reducir |
| Volante demasiado ligero | `MidSpeedWeight` · `HighSpeedWeight` | Aumentar |
| Freno demasiado sensible | `BrakeGamma` | Reducir hacia ~1.5 |
| Retorno al centro débil | `CasterReturnStrength` | Aumentar |
| Vibración de motor molesta | `IdleVibrationStrength` | Reducir o `EnableEngineIdleVibration=0` |

---

## Estructura de archivos

```
NFSU2\
├── SPEED2.EXE
├── dinput8.dll          ← este mod
├── config.ini           ← configuración del G29
├── logs\
│   └── g29_ffb.log      ← registro de diagnóstico
└── scripts\
    └── *.asi            ← mods ASI existentes siguen funcionando
```

---

## Resolución de problemas

| Síntoma | Solución |
|---|---|
| G29 no detectado | Iniciar G HUB antes del juego; usar modo de compatibilidad DirectInput en G HUB |
| Sin FFB | Verificar log: `FFB: device does not support force feedback` |
| Menú baja solo | Ya corregido — neutralización de pedales activa en menús |
| FFB desconectado de la física | Activar `LogLevel=3`, verificar líneas `Tele:` en el log |
| Mods ASI no cargan | Confirmar `.asi` en `scripts\`; verificar líneas `ASI:` en el log |

---

*Calibrado con:*
- *Assetto Corsa G29 — `Documents\Assetto Corsa\cfg\controls.ini`*
- *Forza Horizon 5 — `C:\XboxGames\Forza Horizon 5\Content\media\ControllerFFB.ini`*
- *NFSU2 NA retail — MD5: 665871070B0E4065CE446967294BCCFA*
