# HUB Companion Device — ESP32-S3 Pinout

Board: **Waveshare ESP32-S3-Touch-AMOLED-1.75** (HUB companion device).
Constants derived from `include/app_config.hpp` (`AppConfig::PinoutHub`, `AppConfig::Hub`).

## Buses

### I2C — `SDA=GPIO15`, `SCL=GPIO14` (400 kHz)

Seven ICs on a single shared bus:

| IC | Function | I2C Address |
|----|----------|-------------|
| AXP2101 | PMIC — power authority + battery telemetry | **0x34** |
| QMI8658 | IMU (accel + gyro) + wake-on-motion | **0x6B** (alt 0x6A) |
| PCF85063 | RTC | **0x51** |
| CST9217 | Touch controller | **0x5A** |
| TCA9554 | I/O expander (8-bit) | **0x20** |
| ES8311 | Audio DAC/codec | **0x18** |
| ES7210 | Audio ADC | **0x40** |

### QSPI — CO5300 AMOLED display

| Signal | GPIO |
|--------|------|
| CS | GPIO12 |
| SCLK | GPIO38 |
| DATA0 (D0) | GPIO4 |
| DATA1 (D1) | GPIO5 |
| DATA2 (D2) | GPIO6 |
| DATA3 (D3) | GPIO7 |
| RESET | GPIO39 |

Panel resolution: **466 × 466 px**.

## Peripherals

### Touch — CST9217

| Signal | GPIO | Notes |
|--------|------|-------|
| RESET | GPIO40 | active-low |
| INT | GPIO11 | active-low; also EXT1 wake source |

### Audio I2S — ES8311 DAC + ES7210 ADC

| Signal | GPIO | Notes |
|--------|------|-------|
| MCLK | GPIO42 | master clock |
| BCLK | GPIO9 | bit clock |
| WS | GPIO45 | word select / L-R clock |
| DOUT (DAC out) | GPIO8 | data to ES8311 |
| DIN (ADC in) | GPIO10 | data from ES7210 |
| AMP_ENABLE | GPIO46 | amplifier enable (active-high) |

> Audio codecs are probed at boot then powered down (`HubFeatures::kEnableAudio = false`). Phase-2 will enable streaming.

### IMU — QMI8658

| Signal | GPIO | Notes |
|--------|------|-------|
| INT2 | GPIO21 | wake-on-motion interrupt; EXT1 wake source (with the touch INT) |

### SD card (SDIO 1-bit)

| Signal | GPIO |
|--------|------|
| CMD | GPIO1 |
| CLK | GPIO2 |
| DATA0 | GPIO3 |

### Boot button

| Signal | GPIO |
|--------|------|
| BOOT | GPIO0 |

> Held LOW at reset to enter download mode; also used as a general-purpose button at runtime.

## Power

- **AXP2101** PMIC manages battery charging, rail switching, and telemetry.
  - Charging: enabled, 1000 mA, 4200 mV (see `AppConfig::HubPower`).
  - AXP2101 is always initialized at boot (power authority); only telemetry reporting is
    gated by `HubFeatures::kEnablePmicTelemetry`.
- All other ICs are powered down at boot if their `HubFeatures::kEnable*` toggle is `false`.

## Flash partitions

New partition table: `partitions_hub_16mb.csv` (16 MB flash).
First flash of the HUB board requires a full flash sequence (erase + all partitions).
