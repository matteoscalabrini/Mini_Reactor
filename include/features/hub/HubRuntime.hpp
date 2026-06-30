/*
 * HubRuntime.hpp — top-level runtime for the HUB companion device.
 * Owns the shared I2C bus and every onboard-IC driver; begin() runs the
 * ordered power-up/probe/power-down bring-up, tick() runs periodic polling
 * and the LVGL pump. main_hub.cpp delegates entirely here (mirrors AppRuntime).
 * Implementation: src/features/hub/HubRuntime.cpp
 */
#pragma once
namespace HubRuntime {
void begin();   // one-shot bring-up; called once from setup()
void tick();    // one iteration of periodic work; called from loop()
}  // namespace HubRuntime
