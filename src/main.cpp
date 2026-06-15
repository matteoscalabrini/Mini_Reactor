/*
 * ESP32-S3 — SPI + I2C bus scanner
 *
 * Scans the I2C bus for responding device addresses and probes a set of SPI
 * chip-select pins by reading the JEDEC ID (0x9F), which most SPI flash /
 * sensor parts answer. Results are printed over Serial at 115200 baud.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// ---------------------------------------------------------------------------
// Pin configuration — adjust to match your wiring.
// Defaults below are typical for the ESP32-S3-DevKitC-1.
// ---------------------------------------------------------------------------

// I2C bus
static const int I2C_SDA = 8;
static const int I2C_SCL = 9;
static const uint32_t I2C_FREQ = 100000;  // 100 kHz standard mode

// SPI bus (FSPI). MISO/MOSI/SCK are shared; CS is per-device.
static const int SPI_SCK  = 12;
static const int SPI_MISO = 13;
static const int SPI_MOSI = 11;

// Chip-select pins to probe on the SPI bus.
static const int SPI_CS_PINS[] = {10};
static const size_t SPI_CS_COUNT = sizeof(SPI_CS_PINS) / sizeof(SPI_CS_PINS[0]);

// SPI clock used while probing. Conservative so most devices respond.
static const uint32_t SPI_PROBE_HZ = 1000000;  // 1 MHz

// ---------------------------------------------------------------------------

static void scanI2C() {
  Serial.println(F("\n[I2C] Scanning 0x01 - 0x7E ..."));
  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
      Serial.printf("  device found at 0x%02X\n", addr);
      found++;
    } else if (err == 4) {
      Serial.printf("  unknown error at 0x%02X\n", addr);
    }
  }

  if (found == 0) {
    Serial.println(F("  no I2C devices found"));
  } else {
    Serial.printf("  %u device(s) found\n", found);
  }
}

static void probeSPI() {
  Serial.println(F("\n[SPI] Reading JEDEC ID (0x9F) on each CS pin ..."));

  for (size_t i = 0; i < SPI_CS_COUNT; i++) {
    const int cs = SPI_CS_PINS[i];

    SPI.beginTransaction(SPISettings(SPI_PROBE_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    SPI.transfer(0x9F);
    uint8_t manuf = SPI.transfer(0x00);
    uint8_t memType = SPI.transfer(0x00);
    uint8_t capacity = SPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    SPI.endTransaction();

    bool responded = !(manuf == 0x00 && memType == 0x00 && capacity == 0x00) &&
                     !(manuf == 0xFF && memType == 0xFF && capacity == 0xFF);

    Serial.printf("  CS=GPIO%-2d  JEDEC: %02X %02X %02X  -> %s\n",
                  cs, manuf, memType, capacity,
                  responded ? "device responded" : "no/idle response");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { /* wait briefly for USB CDC */ }

  Serial.println(F("\n=== ESP32-S3 SPI + I2C Scanner ==="));

  // Bring up I2C.
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

  // Bring up SPI and set each CS pin high (deselected).
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  for (size_t i = 0; i < SPI_CS_COUNT; i++) {
    pinMode(SPI_CS_PINS[i], OUTPUT);
    digitalWrite(SPI_CS_PINS[i], HIGH);
  }
}

void loop() {
  scanI2C();
  probeSPI();
  Serial.println(F("\n--- waiting 5s before next scan ---"));
  delay(5000);
}
