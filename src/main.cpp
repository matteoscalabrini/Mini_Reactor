/*
 * main.cpp — Arduino entry points for the Bioreactor Module firmware.
 *
 * Intentionally thin: all device assembly, configuration, and orchestration
 * live in AppRuntime (src/system/AppRuntime.cpp), built from the compile-time
 * constants in include/app_config.hpp. setup() runs the one-shot bring-up and
 * loop() runs the periodic work.
 */

#include "system/AppRuntime.hpp"

void setup() { AppRuntime::begin(); }

void loop() { AppRuntime::tick(); }
