#pragma once
// Stage 1 of the temperature compensation: RECORD. After every calibration the environment
// it was made in (driver block, 3.3 V rail) is saved; after every RESYNC one line
// is logged: each channel's knee shift next to how much each of those changed since the
// previous calibration. LEARN prints the log and the fit (ThermalFit.hpp). Kept apart from
// the calibration files: nothing here changes how the attenuator plays.
#include <Arduino.h>

namespace ThermalLog {
struct Env { float block; float rail; };
Env snapshot();                                          // block (NTC) and rail (TL431)
// After a calibration. resync: shift[] = knee shift (counts, L/R; NAN if not measured).
void onCalibrated(bool resync, const float *shift, const Env &now, Stream &out);
void printSince(Stream &out);                            // the change since the last calibration
void printLog(Stream &out);                              // LEARN
void clear();                                            // FORGET COMP
bool savedBlock(float &t);                               // block temperature at the last calibration
// The learned slopes vs the BLOCK (counts per C, L/R) and their R2; false where the fit is
// not usable yet (n, spread).
void blockSlopes(float k[2], float r2[2], bool usable[2]);
// For the master's LEARN query: resyncs in the log and the RMS block-temperature spread.
void fitCounts(uint8_t &n, float &spread);
void forgetEnvironment();                                // FORGET CAL: the calibration it described is gone
} // namespace ThermalLog
