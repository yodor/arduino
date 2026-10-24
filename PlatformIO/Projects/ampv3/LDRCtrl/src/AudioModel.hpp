#pragma once
// The audio model the step tables are solved for: the source's output impedance (the
// splitter) and the load after the attenuator (the amp's input, or a buffer's bias resistor).
// Defaults from Config.hpp; SET SRC / SET LOAD (console and master protocol) change them at
// runtime and store them in flash ("/audio_model.txt"), so no reflash is needed when the
// splitter or the load changes. If the stored calibration was solved for other values, the
// attenuator reports CAL=STALE and a plain CAL re-solves (DualCalibration::onModelChanged).
#include <Arduino.h>
#include "DividerMath.hpp"

namespace AudioModel {

constexpr float SRC_MIN = 0.0f,    SRC_MAX = 100000.0f;   // ohms
constexpr float LOAD_MIN = 1000.0f, LOAD_MAX = 100.0e6f;  // ohms; or exactly 0 = unloaded

void begin();                       // after LittleFS is mounted, before any calibration loads
float sourceOhms();
float loadOhms();
inline DividerMath::Load load() { return DividerMath::Load{sourceOhms(), loadOhms()}; }
// Validates and stores; false (nothing changed) if out of range.
bool setSource(float ohms);
bool setLoad(float ohms);

} // namespace AudioModel
