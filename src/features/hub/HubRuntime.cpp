#include "features/hub/HubRuntime.hpp"
#include <Arduino.h>
#include "app_config.hpp"

namespace HubRuntime {

void begin() {
  Serial.begin(AppConfig::kSerialBaud);
  delay(AppConfig::kSerialStartupDelayMs);
  Serial.printf("[HUB] boot — firmware %s\n", AppConfig::kFirmwareVersion);
  Serial.printf("[HUB] free heap %u, PSRAM %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void tick() {
  delay(10);
}

}  // namespace HubRuntime
