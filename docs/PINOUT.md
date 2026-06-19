# Bioreactor Module 0.1 — ESP32-S3 Pinout

Derived from `Resources/Board Schematic.pdf` (DesignFrictionLab, KiCad, Rev 0.1, 2026-05-27).
MCU: **ESP32-S3-WROOM-1** (U2).

## Buses

### I2C  — `SDA=GPIO1`, `SCL=GPIO2`
- On-board pull-ups (10k).
- **U3 — HUSB238 USB-PD sink controller** (I2C addr **0x08**), negotiates PD voltage and switches VBUS through a PMOS.
- **J3** external I2C header: `3V3 / SCL / SDA / GND`.

### SPI (FSPI) — micro-SD card (J7)
| Signal | GPIO | Notes |
|--------|------|-------|
| SCK  | GPIO12 | 22R series (R10) → SD CLK |
| MOSI | GPIO11 | 22R series (R12) → SD CMD |
| MISO | GPIO13 | direct → SD DAT0 |
| CS   | GPIO10 | 22R series (R13) → SD DAT3/CD |
| CARD_DETECT | GPIO14 | via R-divider → SD DET |
> SD cards need the SD init handshake; they won't answer a JEDEC-ID probe.

## Peripherals

### TMC2209 stepper driver (U4, motor power = +12V)
| Signal | GPIO | | Signal | GPIO |
|--------|------|-|--------|------|
| STEP  | GPIO15 | | EN (/ENN) | GPIO21 |
| DIR   | GPIO16 | | DIAG | GPIO38 |
| INDEX | GPIO39 | | UART TX | GPIO17 (U1TXD) → 1k (R14) → PDN |
|       |        | | UART RX | GPIO18 (U1RXD) → PDN (direct) |
- Single-wire UART: TX joins PDN through R14 1k, RX taps the same node. UART slave **address = 0** (MS1/MS2 → GND).
- Motor coils on **J2** (`MOT_A1/A2/B1/B2`). Sense resistors **0.11R** (R35/R36).

### Heater
- `HEATER_PWM = GPIO40` → 47R gate resistor → MOSFET, switching `HEATER-` on **J4** (+12V).
- `HEAT_ISNS` (current sense) → **GPIO5** (ADC1).

### Sensing / UI
| Function | GPIO |
|----------|------|
| **Liquid temp — DS18B20** (1-Wire, process value) | GPIO42 (IO42 on J6; needs 4.7k pull-up) |
| **Heater NTC `THERM_ADC`** — 10k/3950, *safety high-limit only* | GPIO6 (ADC1) |
| Heater current sense `HEAT_ISNS` | GPIO5 (ADC1) |
| Rotary encoder A / B / SW | GPIO8 / GPIO9 / GPIO41 |
| Button 1 / 2 / 3 | GPIO47 / GPIO48 / GPIO4 |
| **OLED — SH1107 128×128** (front panel) | SDA=GPIO43 / SCL=GPIO44 — **dedicated I2C (Wire1)** on J6 |

> Temperature control loop: the **DS18B20 in the liquid** is the PID process value; the
> **heater-mounted NTC** is read independently as an over-temp cutoff (heater forced
> off above `Thermal::kHeaterSafetyMaxC`), not for control.
>
> Front-panel OLED runs on its **own** I2C peripheral (`Wire1`, GPIO43/44), separate from
> the primary bus (GPIO1/2, HUSB238) — sharing the primary bus electrically disturbs the
> HUSB238. GPIO43/44 are the UART0 pins, free because the console is on USB-CDC.

### Programming / USB
- Native USB (USB-C J1): `D+ = GPIO20`, `D- = GPIO19`.
- UART0 / debug header **J6**: `U0TXD=GPIO43`, `U0RXD=GPIO44` (**repurposed as the OLED I2C bus** — console is on USB-CDC), `EN`, `BOOT (GPIO0)`, `IO42`.

### Not connected
- GPIO3, GPIO45, GPIO46 (NC). GPIO35/36/37 = WROOM-1 internal octal PSRAM/flash — do not use.

## Power
- USB-C → HUSB238 PD → **+12V** rail (motor + heater).
- **TPS563390** buck → **+3.3V** rail (logic).
