#pragma once
#include <stdint.h>

namespace ui {

enum class UiEvent : uint8_t {
  None, RotateCW, RotateCCW, EncShortPush, EncLongPush, Btn1, Btn2, Btn3
};

enum class Screen : uint8_t { Dashboard, Menu, EditTarget, EditSpeed, Info };

enum MenuItem : int { kMenuStartStop = 0, kMenuTarget, kMenuSpeed, kMenuInfo, kMenuBack, kMenuCount };

struct ReactorSnapshot {
  bool  running = false;
  bool  motorPaused = false;
  bool  fullHold = false;
  bool  sensorFault = false;
  bool  safetyTripped = false;
  float liquidTempC = 0.0f;    // may be NaN on fault
  float setpointC = 0.0f;
  float rpm = 0.0f;            // actual disc rpm (0 when stopped/paused)
  float rpmSetpoint = 0.0f;    // configured speed
  float heaterDutyPct = 0.0f;
  uint32_t elapsedSec = 0;
  // info screen
  bool  wifiConnected = false;
  const char* wifiSsid = "";
  const char* ip = "";
  int   rssi = 0;
  bool  sdMounted = false;
  const char* firmware = "";
};

struct ReactorControl {
  virtual ~ReactorControl() {}
  virtual void startRun(float targetC, float rpm) = 0;
  virtual void stopRun() = 0;
  virtual void setTargetC(float c) = 0;
  virtual void setRpm(float rpm) = 0;
  virtual void setMotorPaused(bool on) = 0;
  virtual void setFullHold(bool on) = 0;
};

}  // namespace ui
