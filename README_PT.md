# G29 FFB Wrapper — NFS Underground 2

Proxy `dinput8.dll` que dá ao **Logitech G29** suporte completo de volante no NFSU2, com force feedback modelado a partir do **Assetto Corsa** (precisão) e do **Forza Horizon 5** (sensação).

> Outros idiomas: [English](README.md) · [Español](README_ES.md) · [Français](README_FR.md) · [Italiano](README_IT.md)

---

## Requisitos

| | |
|---|---|
| Jogo | Need for Speed Underground 2 — versão NA (`SPEED2.EXE` ~4.8 MB) |
| Volante | Logitech G29 com **G HUB** instalado |
| Sistema | Windows 10 / 11 (64-bit) |
| Compilação | Visual Studio 2019/2022/2026 Community + CMake 3.20+ |

---

## Configuração do G HUB (obrigatório antes de jogar)

Abra o **Logitech G HUB** e configure:

| Configuração | Valor |
|---|---|
| **Faixa de operação (Ângulo)** | **180°** |
| Sensibilidade | 50 |
| Força da mola centralizadora | 20 |

![Configuração G HUB](logitechGHUB.jpg)

> O sistema de direção dinâmica do mod (240° → 180° → 120° com a velocidade) foi calibrado para 180° de faixa no G HUB.

---

## Instalação rápida

1. **Configurar o G HUB** (veja acima)
2. **Compilar** a DLL (veja [Compilação](#compilação))
3. Copiar `dinput8.dll` → pasta raiz do NFSU2
4. Copiar `config.ini` → pasta raiz do NFSU2
5. Iniciar o jogo — o volante já está ativo

> O mod inclui um carregador de ASI embutido. Mods existentes em `scripts\` continuam funcionando normalmente.

---

## Compilação

```bat
rem clique duplo, ou execute em qualquer prompt:
mods\g29_ffb\build.bat
```

Ou manualmente (x86 obrigatório — NFSU2 é 32-bit):

```cmd
cmake -S mods\g29_ffb -B build_output -A Win32
cmake --build build_output --config Release
```

O passo pós-build do CMake copia a `dinput8.dll` para a pasta do NFSU2 automaticamente.

---

## Funcionalidades

### Input
| Recurso | Detalhe |
|---|---|
| Detecção do G29 | VID=046D / PID=C24F via DirectInput 8 |
| Mapeamento de eixos | X=direção · Y=acelerador · Rz=freio · Slider[0]=embreagem |
| Alcance de direção | 900° físico, lock virtual configurável |
| Curva do freio | Gamma=2.4 (referência Assetto Corsa) — progressiva |
| Suavização | EMA por eixo (direção α=0.35, pedais α=0.5) |
| Lock dinâmico | Estilo FH5: 240° lento → 180° médio → 120° alta velocidade |
| Assistência de yaw | Amplificação progressiva em alta velocidade |
| Correção de menu | Eixos de pedal neutralizados nos menus — sem entradas fantasmas |

### Force Feedback — 8 efeitos DirectInput
| # | Efeito | Descrição |
|---|---|---|
| 1 | Spring | Curva de velocidade FH5 · transferência de carga · torque de retorno · SAT |
| 2 | Damper | Base FH5 + inércia da cremalheira + release + estabilidade em reta |
| 3 | ConstantForce | G lateral + SAT + assistência de sobreesterço + vibração do motor |
| 4 | SlipVibration | Senoide 26 Hz — perda de tração / giro de roda |
| 5 | RoadTexture | Senoide 47 Hz — textura do asfalto + rack kick em curva |
| 6 | Collision | Impulso em onda quadrada na desaceleração brusca |
| 7 | Curb | Rajada 50 Hz ao passar por guia/zebra |
| 8 | FrontScrub | Senoide 32 Hz — raspagem do pneu dianteiro próximo ao limite |

---

## Configuração

Todos os parâmetros ficam em `config.ini` na pasta raiz do NFSU2. Alterações têm efeito na próxima inicialização do jogo — sem necessidade de recompilar.

Consulte [INSTALL.md](INSTALL.md) para a referência completa de parâmetros.

### Ajustes rápidos mais comuns

| Sintoma | Parâmetro | Ajuste |
|---|---|---|
| Volante muito pesado | `HighSpeedWeight` · `CenterSpring` | Reduzir |
| Volante muito leve | `MidSpeedWeight` · `HighSpeedWeight` | Aumentar |
| Freio muito sensível | `BrakeGamma` | Reduzir para ~1.5 |
| Retorno ao centro fraco | `CasterReturnStrength` | Aumentar |
| Vibração de motor irritante | `IdleVibrationStrength` | Reduzir ou `EnableEngineIdleVibration=0` |

---

## Estrutura de arquivos

```
NFSU2\
├── SPEED2.EXE
├── dinput8.dll          ← este mod
├── config.ini           ← configuração do G29
├── logs\
│   └── g29_ffb.log      ← log de diagnóstico
└── scripts\
    └── *.asi            ← mods ASI existentes continuam funcionando
```

---

## Solução de problemas

| Sintoma | Solução |
|---|---|
| G29 não detectado | Iniciar o G HUB antes do jogo; usar modo de compatibilidade DirectInput no G HUB |
| Sem FFB | Verificar log: `FFB: device does not support force feedback` |
| Menu desce sozinho | Já corrigido — neutralização de pedais ativa nos menus |
| FFB desconectado da física | Ativar `LogLevel=3`, verificar linhas `Tele:` no log |
| Mods ASI não carregam | Confirmar `.asi` em `scripts\`; verificar linhas `ASI:` no log |

---

*Calibrado com base em:*
- *Assetto Corsa G29 — `Documents\Assetto Corsa\cfg\controls.ini`*
- *Forza Horizon 5 — `C:\XboxGames\Forza Horizon 5\Content\media\ControllerFFB.ini`*
- *NFSU2 NA retail — MD5: 665871070B0E4065CE446967294BCCFA*
