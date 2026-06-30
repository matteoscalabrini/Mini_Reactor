#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <driver/spi_master.h>

class Co5300Panel {
 public:
  bool begin(String& lastError);
  void end();
  bool flush(const lv_area_t& area, const lv_color_t* colors);
  bool enterSleep();
  bool exitSleep();
  bool isReady() const { return ready_; }

 private:
  bool initializeSpi(String& lastError);
  bool initializeController(String& lastError);
  bool setAddressWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
  bool writeSolidColor(uint16_t color565, size_t pixelCount);
  bool writeCommand(uint8_t command);
  bool writeCommandBytes(uint8_t command, const uint8_t* data, size_t length);
  bool writeCommandRange(uint8_t command, uint16_t start, uint16_t end);
  bool transmitPixels(const uint16_t* pixels, size_t pixelCount, bool firstChunk);
  void destroySpi();
  void csLow();
  void csHigh();

  spi_device_handle_t spi_ = nullptr;
  bool busInitialized_ = false;
  uint16_t* txBuffer_ = nullptr;
  uint16_t* txBuffer2_ = nullptr;
  size_t txBufferPixelCapacity_ = 0;
  bool ready_ = false;
  bool sleeping_ = false;
};
