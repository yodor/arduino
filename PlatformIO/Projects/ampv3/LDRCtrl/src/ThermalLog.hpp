#pragma once
// Stage 1 of the temperature compensation: RECORD. After every calibration the environment
// it was made in (driver block, both cells, 3.3 V rail) is saved; after every RESYNC one line
// is logged: each channel's knee shift next to how much each of those changed since the
// previous calibration. LEARN prints the log and the fit (ThermalFit.hpp). Kept apart from
// the calibration files: nothing here changes how the attenuator plays.
#include <Arduino.h>

namespace ThermalLog {
struct Env { float block; float cell[2]; float rail; };
Env snapshot();                                          // reads all sensors (~0.8 s)
// After a calibration. resync: shift[] = knee shift (counts, L/R; NAN if not measured).
void onCalibrated(bool resync, const float *shift, const Env &now, Stream &out);
void printSince(Stream &out);                            // the change since the last calibration
void printLog(Stream &out);                              // LEARN
void clear();                                            // LEARN CLEAR
} // namespace ThermalLog
