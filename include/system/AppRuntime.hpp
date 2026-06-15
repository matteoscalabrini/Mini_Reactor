/*
 * AppRuntime.hpp — top-level application runtime for the Bioreactor Module.
 *
 * Owns the long-lived device objects (HUSB238 PD sink, TMC2209 motor, heater
 * MOSFET, SD logger), builds their configs from AppConfig, and drives the
 * board: begin() runs the one-shot bring-up sequence, tick() runs the periodic
 * main-loop work. The Arduino entry points in main.cpp delegate entirely here.
 *
 * Implementation: src/system/AppRuntime.cpp
 */

#pragma once

namespace AppRuntime {

/* begin() — One-shot bring-up: serial, I2C + 12V PD, bus scan, SD card check,
 * heater, and motor. Called once from setup(). */
void begin();

/* tick() — One iteration of periodic work: toggle heater, spin motor, and log
 * a sensor row to the SD card. Called repeatedly from loop(). */
void tick();

}  // namespace AppRuntime
