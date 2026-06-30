#include "features/hub/display/Co5300Panel.hpp"
#include "features/hub/display/PanelGeometry.hpp"
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr spi_host_device_t kPanelSpiHost = SPI2_HOST;
constexpr int32_t kPanelClockHz = 80000000;
constexpr size_t kPanelPixelsPerTransfer = 4096;
constexpr uint8_t kQspiRegisterWriteCommand = 0x02;
constexpr uint8_t kQspiPixelWriteCommand = 0x32;
constexpr uint32_t kQspiPixelWriteAddress = 0x003C00;
constexpr uint8_t kCo5300SwReset = 0x01;
constexpr uint8_t kCo5300SleepIn = 0x10;
constexpr uint8_t kCo5300SleepOut = 0x11;
constexpr uint8_t kCo5300DispOff = 0x28;
constexpr uint8_t kCo5300DispOn = 0x29;
constexpr uint8_t kCo5300CaseSet = 0x2A;
constexpr uint8_t kCo5300PageSet = 0x2B;
constexpr uint8_t kCo5300RamWrite = 0x2C;
constexpr uint8_t kCo5300Madctl = 0x36;
constexpr uint8_t kCo5300PixFmt = 0x3A;
constexpr uint8_t kCo5300InversionOff = 0x20;
constexpr uint8_t kCo5300BrightnessNormal = 0x51;
constexpr uint8_t kCo5300CtrlDisplay1 = 0x53;
constexpr uint8_t kCo5300ContrastEnhancement = 0x58;
constexpr uint8_t kCo5300BrightnessHbm = 0x63;
constexpr uint8_t kCo5300SpiModeControl = 0xC4;
constexpr uint8_t kCo5300VendorPage = 0xFE;
constexpr uint16_t kResetDelayMs = 200;
constexpr uint16_t kSleepInDelayMs = 120;
constexpr uint16_t kSleepOutDelayMs = 120;

uint16_t swapBytes16(uint16_t value) {
  return static_cast<uint16_t>((value >> 8) | (value << 8));
}

}  // namespace

bool Co5300Panel::begin(String& lastError) {
  end();

  if (!initializeSpi(lastError)) {
    return false;
  }

  if (!initializeController(lastError)) {
    end();
    return false;
  }

  if (!setAddressWindow(0, 0, AppConfig::HubDisplay::kWidth,
                        AppConfig::HubDisplay::kHeight) ||
      !writeSolidColor(0x0000,
                       static_cast<size_t>(AppConfig::HubDisplay::kWidth) *
                           static_cast<size_t>(AppConfig::HubDisplay::kHeight))) {
    lastError = "display_clear_failed";
    end();
    return false;
  }

  ready_ = true;
  sleeping_ = false;
  lastError = "";
  return true;
}

void Co5300Panel::end() {
  ready_ = false;
  sleeping_ = false;

  if (txBuffer_ != nullptr) {
    heap_caps_free(txBuffer_);
    txBuffer_ = nullptr;
  }
  if (txBuffer2_ != nullptr) {
    heap_caps_free(txBuffer2_);
    txBuffer2_ = nullptr;
  }
  txBufferPixelCapacity_ = 0;

  destroySpi();
}

bool Co5300Panel::enterSleep() {
  if (!ready_) {
    return false;
  }
  if (sleeping_) {
    return true;
  }
  if (!writeCommand(kCo5300DispOff) || !writeCommand(kCo5300SleepIn)) {
    return false;
  }
  delay(kSleepInDelayMs);
  sleeping_ = true;
  return true;
}

bool Co5300Panel::exitSleep() {
  if (!ready_) {
    return false;
  }
  if (!sleeping_) {
    return true;
  }

  const uint8_t brightnessData[] = {AppConfig::HubDisplay::kBrightness};
  if (!writeCommand(kCo5300SleepOut)) {
    return false;
  }
  delay(kSleepOutDelayMs);
  if (!writeCommand(kCo5300DispOn) ||
      !writeCommandBytes(kCo5300BrightnessNormal, brightnessData,
                         sizeof(brightnessData))) {
    return false;
  }
  delay(10);
  sleeping_ = false;
  return true;
}

bool Co5300Panel::flush(const lv_area_t& area, const lv_color_t* colors) {
  if (!ready_ || sleeping_ || colors == nullptr) {
    return false;
  }

  const uint16_t width = static_cast<uint16_t>(area.x2 - area.x1 + 1);
  const uint16_t height = static_cast<uint16_t>(area.y2 - area.y1 + 1);
  if (width == 0 || height == 0) {
    return true;
  }

  if (!setAddressWindow(static_cast<uint16_t>(area.x1),
                        static_cast<uint16_t>(area.y1), width, height)) {
    return false;
  }

  csLow();
  static spi_transaction_ext_t s_trans[2];
  uint16_t* bufs[2] = {txBuffer_, txBuffer2_};
  size_t remaining = static_cast<size_t>(width) * static_cast<size_t>(height);
  bool firstChunk = true;
  int bufIdx = 0;
  int inflight = 0;
  bool ok = true;
  while (remaining > 0 && ok) {
    const size_t chunkPixels =
        remaining > txBufferPixelCapacity_ ? txBufferPixelCapacity_ : remaining;
    uint16_t* dst = bufs[bufIdx];

    if (inflight == 2) {
      spi_transaction_t* done = nullptr;
      if (spi_device_get_trans_result(spi_, &done, portMAX_DELAY) != ESP_OK) {
        ok = false;
        break;
      }
      --inflight;
    }

    const uint32_t* src32 = reinterpret_cast<const uint32_t*>(colors);
    uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);
    const size_t words = chunkPixels / 2;
    for (size_t i = 0; i < words; ++i) {
      const uint32_t v = src32[i];
      dst32[i] = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8);
    }
    if ((chunkPixels & 1u) != 0u) {
      dst[chunkPixels - 1] = swapBytes16(
          lv_color_to16(colors[chunkPixels - 1]));
    }

    spi_transaction_ext_t& t = s_trans[bufIdx];
    t = {};
    t.base.flags =
        SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
        SPI_TRANS_VARIABLE_DUMMY;
    t.base.tx_buffer = dst;
    t.base.length = chunkPixels * 16;
    t.command_bits = firstChunk ? 8 : 0;
    t.address_bits = firstChunk ? 24 : 0;
    t.base.cmd = firstChunk ? kQspiPixelWriteCommand : 0;
    t.base.addr = firstChunk ? kQspiPixelWriteAddress : 0;
    if (spi_device_queue_trans(spi_, reinterpret_cast<spi_transaction_t*>(&t),
                               portMAX_DELAY) != ESP_OK) {
      ok = false;
      break;
    }
    ++inflight;

    firstChunk = false;
    colors += chunkPixels;
    remaining -= chunkPixels;
    bufIdx ^= 1;
  }
  while (inflight > 0) {
    spi_transaction_t* done = nullptr;
    spi_device_get_trans_result(spi_, &done, portMAX_DELAY);
    --inflight;
  }
  csHigh();

  return ok;
}

bool Co5300Panel::initializeSpi(String& lastError) {
  if (AppConfig::PinoutHub::kHubDisplayCs == 0xFF ||
      AppConfig::PinoutHub::kHubDisplaySclk == 0xFF ||
      AppConfig::PinoutHub::kHubDisplayData0 == 0xFF ||
      AppConfig::PinoutHub::kHubDisplayData1 == 0xFF ||
      AppConfig::PinoutHub::kHubDisplayData2 == 0xFF ||
      AppConfig::PinoutHub::kHubDisplayData3 == 0xFF) {
    lastError = "display_pins_missing";
    return false;
  }

  pinMode(AppConfig::PinoutHub::kHubDisplayCs, OUTPUT);
  digitalWrite(AppConfig::PinoutHub::kHubDisplayCs, HIGH);
  if (AppConfig::PinoutHub::kHubDisplayReset != 0xFF) {
    pinMode(AppConfig::PinoutHub::kHubDisplayReset, OUTPUT);
    digitalWrite(AppConfig::PinoutHub::kHubDisplayReset, HIGH);
  }

  spi_bus_config_t busConfig = {};
  busConfig.mosi_io_num = AppConfig::PinoutHub::kHubDisplayData0;
  busConfig.miso_io_num = AppConfig::PinoutHub::kHubDisplayData1;
  busConfig.sclk_io_num = AppConfig::PinoutHub::kHubDisplaySclk;
  busConfig.quadwp_io_num = AppConfig::PinoutHub::kHubDisplayData2;
  busConfig.quadhd_io_num = AppConfig::PinoutHub::kHubDisplayData3;
  busConfig.max_transfer_sz =
      static_cast<int>((kPanelPixelsPerTransfer * sizeof(uint16_t)) + 8);
  busConfig.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS |
                    SPICOMMON_BUSFLAG_QUAD;

  const esp_err_t busErr =
      spi_bus_initialize(kPanelSpiHost, &busConfig, SPI_DMA_CH_AUTO);
  if (busErr != ESP_OK) {
    Serial.printf("[HUBDISPLAY] SPI bus init failed err=%d\n",
                  static_cast<int>(busErr));
    lastError = "display_spi_bus_init_failed";
    return false;
  }
  busInitialized_ = true;

  spi_device_interface_config_t deviceConfig = {};
  deviceConfig.mode = SPI_MODE0;
  deviceConfig.clock_speed_hz = kPanelClockHz;
  deviceConfig.spics_io_num = -1;
  deviceConfig.queue_size = 2;
  deviceConfig.flags = SPI_DEVICE_HALFDUPLEX;

  const esp_err_t devErr =
      spi_bus_add_device(kPanelSpiHost, &deviceConfig, &spi_);
  if (devErr != ESP_OK) {
    Serial.printf("[HUBDISPLAY] SPI add device failed err=%d\n",
                  static_cast<int>(devErr));
    lastError = "display_spi_device_add_failed";
    destroySpi();
    return false;
  }

  txBuffer_ = static_cast<uint16_t*>(
      heap_caps_malloc(kPanelPixelsPerTransfer * sizeof(uint16_t),
                       MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (txBuffer_ == nullptr) {
    lastError = "display_dma_buffer_alloc_failed";
    destroySpi();
    return false;
  }
  txBuffer2_ = static_cast<uint16_t*>(
      heap_caps_malloc(kPanelPixelsPerTransfer * sizeof(uint16_t),
                       MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (txBuffer2_ == nullptr) {
    lastError = "display_dma_buffer2_alloc_failed";
    destroySpi();
    return false;
  }
  txBufferPixelCapacity_ = kPanelPixelsPerTransfer;
  return true;
}

bool Co5300Panel::initializeController(String& lastError) {
  if (AppConfig::PinoutHub::kHubDisplayReset != 0xFF) {
    digitalWrite(AppConfig::PinoutHub::kHubDisplayReset, HIGH);
    delay(10);
    digitalWrite(AppConfig::PinoutHub::kHubDisplayReset, LOW);
    delay(kResetDelayMs);
    digitalWrite(AppConfig::PinoutHub::kHubDisplayReset, HIGH);
    delay(kResetDelayMs);
  } else {
    if (!writeCommand(kCo5300SwReset)) {
      lastError = "display_swreset_failed";
      return false;
    }
    delay(kResetDelayMs);
  }

  if (!writeCommand(kCo5300SleepOut)) {
    lastError = "display_sleepout_failed";
    return false;
  }
  delay(kSleepOutDelayMs);

  const uint8_t madctl = geom::rotationMadctl(AppConfig::HubDisplay::kRotation);
  const uint8_t vendorPageData[] = {0x00};
  const uint8_t spiModeData[] = {0x80};
  const uint8_t pixelFormatData[] = {0x55};
  const uint8_t ctrlDisplay1Data[] = {0x20};
  const uint8_t hbmBrightnessData[] = {0xFF};
  const uint8_t brightnessData[] = {AppConfig::HubDisplay::kBrightness};
  const uint8_t contrastData[] = {0x00};

  if (!writeCommandBytes(kCo5300VendorPage, vendorPageData,
                         sizeof(vendorPageData)) ||
      !writeCommandBytes(kCo5300SpiModeControl, spiModeData,
                         sizeof(spiModeData)) ||
      !writeCommandBytes(kCo5300Madctl, &madctl, 1) ||
      !writeCommandBytes(kCo5300PixFmt, pixelFormatData,
                         sizeof(pixelFormatData)) ||
      !writeCommandBytes(kCo5300CtrlDisplay1, ctrlDisplay1Data,
                         sizeof(ctrlDisplay1Data)) ||
      !writeCommandBytes(kCo5300BrightnessHbm, hbmBrightnessData,
                         sizeof(hbmBrightnessData)) ||
      !writeCommand(kCo5300InversionOff) ||
      !writeCommand(kCo5300DispOn) ||
      !writeCommandBytes(kCo5300BrightnessNormal, brightnessData,
                         sizeof(brightnessData)) ||
      !writeCommandBytes(kCo5300ContrastEnhancement, contrastData,
                         sizeof(contrastData))) {
    lastError = "display_controller_init_failed";
    return false;
  }

  delay(10);
  return true;
}

bool Co5300Panel::writeCommand(uint8_t command) {
  spi_transaction_ext_t transaction = {};
  transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                           SPI_TRANS_MULTILINE_CMD |
                           SPI_TRANS_MULTILINE_ADDR;
  transaction.base.cmd = kQspiRegisterWriteCommand;
  transaction.base.addr = static_cast<uint32_t>(command) << 8;
  transaction.command_bits = 8;
  transaction.address_bits = 24;

  csLow();
  const esp_err_t err = spi_device_polling_transmit(
      spi_, reinterpret_cast<spi_transaction_t*>(&transaction));
  csHigh();
  return err == ESP_OK;
}

bool Co5300Panel::writeCommandBytes(uint8_t command, const uint8_t* data,
                                    size_t length) {
  spi_transaction_ext_t transaction = {};
  transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                           SPI_TRANS_MULTILINE_CMD |
                           SPI_TRANS_MULTILINE_ADDR;
  transaction.base.cmd = kQspiRegisterWriteCommand;
  transaction.base.addr = static_cast<uint32_t>(command) << 8;
  transaction.base.length = length * 8;
  transaction.command_bits = 8;
  transaction.address_bits = 24;

  if (length <= sizeof(transaction.base.tx_data)) {
    transaction.base.flags |= SPI_TRANS_USE_TXDATA;
    std::memcpy(transaction.base.tx_data, data, length);
  } else {
    transaction.base.tx_buffer = data;
  }

  csLow();
  const esp_err_t err = spi_device_polling_transmit(
      spi_, reinterpret_cast<spi_transaction_t*>(&transaction));
  csHigh();
  return err == ESP_OK;
}

bool Co5300Panel::writeCommandRange(uint8_t command, uint16_t start,
                                    uint16_t end) {
  uint8_t payload[4] = {
      static_cast<uint8_t>((start >> 8) & 0xFF),
      static_cast<uint8_t>(start & 0xFF),
      static_cast<uint8_t>((end >> 8) & 0xFF),
      static_cast<uint8_t>(end & 0xFF),
  };
  return writeCommandBytes(command, payload, sizeof(payload));
}

bool Co5300Panel::writeSolidColor(uint16_t color565, size_t pixelCount) {
  const uint16_t wireColor = swapBytes16(color565);
  for (size_t index = 0; index < txBufferPixelCapacity_; ++index) {
    txBuffer_[index] = wireColor;
  }

  csLow();
  bool firstChunk = true;
  size_t remaining = pixelCount;
  while (remaining > 0) {
    const size_t chunkPixels =
        remaining > txBufferPixelCapacity_ ? txBufferPixelCapacity_ : remaining;
    if (!transmitPixels(txBuffer_, chunkPixels, firstChunk)) {
      csHigh();
      return false;
    }

    firstChunk = false;
    remaining -= chunkPixels;
  }
  csHigh();

  return true;
}

bool Co5300Panel::setAddressWindow(uint16_t x, uint16_t y, uint16_t width,
                                   uint16_t height) {
  const uint16_t xStart = static_cast<uint16_t>(x + geom::columnOffset(AppConfig::HubDisplay::kRotation));
  const uint16_t yStart = static_cast<uint16_t>(y + geom::rowOffset(AppConfig::HubDisplay::kRotation));
  const uint16_t xEnd = static_cast<uint16_t>(xStart + width - 1);
  const uint16_t yEnd = static_cast<uint16_t>(yStart + height - 1);

  return writeCommandRange(kCo5300CaseSet, xStart, xEnd) &&
         writeCommandRange(kCo5300PageSet, yStart, yEnd) &&
         writeCommand(kCo5300RamWrite);
}

bool Co5300Panel::transmitPixels(const uint16_t* pixels, size_t pixelCount,
                                 bool firstChunk) {
  spi_transaction_ext_t transaction = {};
  transaction.base.flags =
      SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
      SPI_TRANS_VARIABLE_DUMMY;
  transaction.base.tx_buffer = pixels;
  transaction.base.length = pixelCount * 16;
  transaction.command_bits = firstChunk ? 8 : 0;
  transaction.address_bits = firstChunk ? 24 : 0;
  transaction.base.cmd = firstChunk ? kQspiPixelWriteCommand : 0;
  transaction.base.addr = firstChunk ? kQspiPixelWriteAddress : 0;

  const esp_err_t err = spi_device_polling_transmit(
      spi_, reinterpret_cast<spi_transaction_t*>(&transaction));
  return err == ESP_OK;
}

void Co5300Panel::csLow() {
  digitalWrite(AppConfig::PinoutHub::kHubDisplayCs, LOW);
}

void Co5300Panel::csHigh() {
  digitalWrite(AppConfig::PinoutHub::kHubDisplayCs, HIGH);
}

void Co5300Panel::destroySpi() {
  if (spi_ != nullptr) {
    spi_bus_remove_device(spi_);
    spi_ = nullptr;
  }
  if (busInitialized_) {
    spi_bus_free(kPanelSpiHost);
    busInitialized_ = false;
  }
}
