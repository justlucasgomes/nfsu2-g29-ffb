# G29 FFB Wrapper — NFS Underground 2

Proxy `dinput8.dll` che offre al **Logitech G29** il supporto completo del volante in NFSU2, con un force feedback modellato su **Assetto Corsa** (precisione) e **Forza Horizon 5** (feeling).

> Altre lingue: [English](README.md) · [Português](README_PT.md) · [Español](README_ES.md) · [Français](README_FR.md)

---

## Requisiti

| | |
|---|---|
| Gioco | Need for Speed Underground 2 — versione NA (`SPEED2.EXE` ~4.8 MB) |
| Volante | Logitech G29 con **G HUB** installato |
| Sistema | Windows 10 / 11 (64-bit) |
| Compilazione | Visual Studio 2019/2022/2026 Community + CMake 3.20+ |

---

## Configurazione di G HUB (obbligatoria prima di giocare)

Aprire **Logitech G HUB** e impostare:

| Impostazione | Valore |
|---|---|
| **Intervallo operativo (Angolo)** | **180°** |
| Sensibilità | 50 |
| Forza molla di centraggio | 20 |

![Configurazione G HUB](logitechGHUB.jpg)

> Il sistema di sterzo dinamico del mod (240° → 180° → 120° con la velocità) è calibrato per un intervallo di 180° in G HUB.

---

## Installazione rapida

1. **Configurare G HUB** (vedi sopra)
2. **Compilare** la DLL (vedi [Compilazione](#compilazione))
3. Copiare `dinput8.dll` → cartella radice di NFSU2
4. Copiare `config.ini` → cartella radice di NFSU2
5. Avviare il gioco — il volante è pronto

> Il mod include un loader ASI integrato. I mod esistenti in `scripts\` continuano a funzionare normalmente.

---

## Compilazione

```bat
rem doppio clic, oppure eseguire da qualsiasi prompt:
mods\g29_ffb\build.bat
```

Oppure manualmente (x86 obbligatorio — NFSU2 è a 32 bit):

```cmd
cmake -S mods\g29_ffb -B build_output -A Win32
cmake --build build_output --config Release
```

Il passo post-build di CMake copia automaticamente `dinput8.dll` nella cartella di NFSU2.

---

## Funzionalità

### Input
| Funzione | Dettaglio |
|---|---|
| Rilevamento G29 | VID=046D / PID=C24F via DirectInput 8 |
| Mappatura assi | X=sterzo · Y=acceleratore · Rz=freno · Slider[0]=frizione |
| Angolo di sterzo | 900° fisico, blocco virtuale configurabile |
| Curva del freno | Gamma=2.4 (riferimento Assetto Corsa) — progressiva |
| Smoothing | EMA per asse (sterzo α=0.35, pedali α=0.5) |
| Blocco dinamico | Stile FH5: 240° bassa velocità → 180° media → 120° alta velocità |
| Assistenza imbardata | Amplificazione progressiva ad alta velocità |
| Correzione menu | Assi dei pedali neutralizzati nei menu — nessun input fantasma |

### Force Feedback — 8 effetti DirectInput
| # | Effetto | Descrizione |
|---|---|---|
| 1 | Spring | Curva di velocità FH5 · trasferimento di carico · coppia di ritorno · SAT |
| 2 | Damper | Base FH5 + inerzia cremagliera + release + stabilità in rettilineo |
| 3 | ConstantForce | G laterale + SAT + assistenza sovrasterzo + vibrazione motore |
| 4 | SlipVibration | Sinusoide 26 Hz — perdita di trazione / slittamento |
| 5 | RoadTexture | Sinusoide 47 Hz — texture asfalto + rack kick in curva |
| 6 | Collision | Impulso ad onda quadra su decelerazione brusca |
| 7 | Curb | Raffica 50 Hz sui cordoli |
| 8 | FrontScrub | Sinusoide 32 Hz — strisciamento pneumatico anteriore al limite |

---

## Configurazione

Tutti i parametri si trovano in `config.ini` nella cartella radice di NFSU2. Le modifiche hanno effetto al prossimo avvio del gioco — senza ricompilare.

Consultare [INSTALL.md](INSTALL.md) per il riferimento completo dei parametri.

### Regolazioni rapide più comuni

| Sintomo | Parametro | Regolazione |
|---|---|---|
| Volante troppo pesante | `HighSpeedWeight` · `CenterSpring` | Ridurre |
| Volante troppo leggero | `MidSpeedWeight` · `HighSpeedWeight` | Aumentare |
| Freno troppo sensibile | `BrakeGamma` | Ridurre verso ~1.5 |
| Ritorno al centro debole | `CasterReturnStrength` | Aumentare |
| Vibrazione motore fastidiosa | `IdleVibrationStrength` | Ridurre o `EnableEngineIdleVibration=0` |

---

## Struttura dei file

```
NFSU2\
├── SPEED2.EXE
├── dinput8.dll          ← questo mod
├── config.ini           ← configurazione G29
├── logs\
│   └── g29_ffb.log      ← log diagnostico
└── scripts\
    └── *.asi            ← mod ASI esistenti — ancora caricati
```

---

## Risoluzione problemi

| Sintomo | Soluzione |
|---|---|
| G29 non rilevato | Avviare G HUB prima del gioco; usare la modalità compatibilità DirectInput in G HUB |
| Nessun FFB | Verificare log: `FFB: device does not support force feedback` |
| Menu scorre da solo | Già corretto — neutralizzazione pedali attiva nei menu |
| FFB scollegato dalla fisica | Attivare `LogLevel=3`, controllare le righe `Tele:` nel log |
| Mod ASI non caricati | Verificare che i `.asi` siano in `scripts\`; controllare le righe `ASI:` nel log |

---

*Calibrato su:*
- *Assetto Corsa G29 — `Documents\Assetto Corsa\cfg\controls.ini`*
- *Forza Horizon 5 — `C:\XboxGames\Forza Horizon 5\Content\media\ControllerFFB.ini`*
- *NFSU2 NA retail — MD5: 665871070B0E4065CE446967294BCCFA*
