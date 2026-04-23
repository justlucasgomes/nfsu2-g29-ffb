# G29 FFB Wrapper — NFS Underground 2

Proxy `dinput8.dll` offrant au **Logitech G29** un support complet de volant dans NFSU2, avec un force feedback modélisé d'après **Assetto Corsa** (précision) et **Forza Horizon 5** (ressenti).

> Autres langues : [English](README.md) · [Português](README_PT.md) · [Español](README_ES.md) · [Italiano](README_IT.md)

---

## Prérequis

| | |
|---|---|
| Jeu | Need for Speed Underground 2 — version NA (`SPEED2.EXE` ~4.8 Mo) |
| Volant | Logitech G29 avec **G HUB** installé |
| Système | Windows 10 / 11 (64-bit) |
| Compilation | Visual Studio 2019/2022/2026 Community + CMake 3.20+ |

---

## Configuration de G HUB (obligatoire avant de jouer)

Ouvrez **Logitech G HUB** et configurez :

| Réglage | Valeur |
|---|---|
| **Plage de fonctionnement (Angle)** | **180°** |
| Sensibilité | 50 |
| Force du ressort de centrage | 20 |

![Configuration G HUB](logitechGHUB.jpg)

> Le système de direction dynamique du mod (240° → 180° → 120° selon la vitesse) est calibré pour une plage de 180° dans G HUB.

---

## Installation rapide

1. **Configurer G HUB** (voir ci-dessus)
2. **Compiler** la DLL (voir [Compilation](#compilation))
3. Copier `dinput8.dll` → dossier racine de NFSU2
4. Copier `config.ini` → dossier racine de NFSU2
5. Lancer le jeu — le volant est prêt

> Le mod intègre un chargeur ASI. Les mods existants dans `scripts\` continuent de fonctionner.

---

## Compilation

```bat
rem double-clic, ou exécuter depuis n'importe quelle invite :
mods\g29_ffb\build.bat
```

Ou manuellement (x86 obligatoire — NFSU2 est 32 bits) :

```cmd
cmake -S mods\g29_ffb -B build_output -A Win32
cmake --build build_output --config Release
```

L'étape post-build de CMake copie automatiquement `dinput8.dll` dans le dossier NFSU2.

---

## Fonctionnalités

### Entrées
| Fonctionnalité | Détail |
|---|---|
| Détection G29 | VID=046D / PID=C24F via DirectInput 8 |
| Mappage des axes | X=direction · Y=accélérateur · Rz=frein · Slider[0]=embrayage |
| Plage de direction | 900° physique, butée virtuelle configurable |
| Courbe de frein | Gamma=2.4 (référence Assetto Corsa) — progressive |
| Lissage | EMA par axe (direction α=0.35, pédales α=0.5) |
| Butée dynamique | Style FH5 : 240° ville → 180° médium → 120° autoroute |
| Assistance en lacet | Amplification progressive à haute vitesse |
| Correction de menu | Axes de pédales neutralisés dans les menus — aucune entrée fantôme |

### Force Feedback — 8 effets DirectInput
| # | Effet | Description |
|---|---|---|
| 1 | Spring | Courbe de vitesse FH5 · transfert de charge · couple de rappel · SAT |
| 2 | Damper | Base FH5 + inertie de crémaillère + release + stabilité en ligne droite |
| 3 | ConstantForce | G latéral + SAT + assistance en survirage + vibration moteur |
| 4 | SlipVibration | Sinusoïde 26 Hz — perte de traction / patinage |
| 5 | RoadTexture | Sinusoïde 47 Hz — texture de route + rack kick en courbe |
| 6 | Collision | Impulsion en onde carrée lors d'une décélération brutale |
| 7 | Curb | Rafale 50 Hz sur les bordures |
| 8 | FrontScrub | Sinusoïde 32 Hz — crissement de pneu avant à la limite |

---

## Configuration

Tous les paramètres se trouvent dans `config.ini` à la racine de NFSU2. Les modifications prennent effet au prochain démarrage du jeu — sans recompilation.

Consultez [INSTALL.md](INSTALL.md) pour la référence complète des paramètres.

### Réglages rapides courants

| Symptôme | Paramètre | Réglage |
|---|---|---|
| Volant trop lourd | `HighSpeedWeight` · `CenterSpring` | Diminuer |
| Volant trop léger | `MidSpeedWeight` · `HighSpeedWeight` | Augmenter |
| Frein trop sensible | `BrakeGamma` | Réduire vers ~1.5 |
| Rappel au centre faible | `CasterReturnStrength` | Augmenter |
| Vibration moteur gênante | `IdleVibrationStrength` | Réduire ou `EnableEngineIdleVibration=0` |

---

## Structure des fichiers

```
NFSU2\
├── SPEED2.EXE
├── dinput8.dll          ← ce mod
├── config.ini           ← configuration G29
├── logs\
│   └── g29_ffb.log      ← journal de diagnostic
└── scripts\
    └── *.asi            ← mods ASI existants — toujours chargés
```

---

## Dépannage

| Symptôme | Solution |
|---|---|
| G29 non détecté | Démarrer G HUB avant le jeu ; utiliser le mode de compatibilité DirectInput dans G HUB |
| Pas de FFB | Vérifier le log : `FFB: device does not support force feedback` |
| Menu défile tout seul | Déjà corrigé — neutralisation des pédales active dans les menus |
| FFB déconnecté de la physique | Activer `LogLevel=3`, vérifier les lignes `Tele:` dans le log |
| Mods ASI non chargés | Vérifier que les `.asi` sont dans `scripts\` ; vérifier les lignes `ASI:` dans le log |

---

*Calibré d'après :*
- *Assetto Corsa G29 — `Documents\Assetto Corsa\cfg\controls.ini`*
- *Forza Horizon 5 — `C:\XboxGames\Forza Horizon 5\Content\media\ControllerFFB.ini`*
- *NFSU2 NA retail — MD5 : 665871070B0E4065CE446967294BCCFA*
