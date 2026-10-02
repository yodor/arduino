#pragma once
#include <Arduino.h>
#include "LDRVolume.hpp"

// ============================================================================
// DualCalibration
//
// Characterizes BOTH channels at once, interleaved, instead of running
// each channel's full sweep sequentially (LDRVolume::runAutoCalibration()
// still does that, unchanged, for single-channel bench use). At every
// duty point, both channels are driven simultaneously and polled for
// convergence in alternation -- since the dominant cost is each LDR's own
// physical settling time (I/O-bound, not CPU-bound) and the two channels'
// I2C buses (i2c0/i2c1) don't contend with each other, this roughly
// halves total CAL FULL/FAST time versus doing the two channels back to
// back.
//
// Built entirely on LDRVolume's existing public API (calBegin/
// calDriveSeries/calDriveShunt/adcRead/calFeedSeriesPoint/
// calFeedShuntPoint/calSolve/calSave/relayEnergize) -- no changes to
// LDRVolume's own characterization logic.
// ============================================================================
namespace DualCalibration {

// Falls back to running just one channel's own (sequential) path if only
// one is actually present (ADS1115 ACKs); does nothing and prints a
// message if neither is present.
void runBoth(LDRVolume &left, LDRVolume &right, LDRVolume::CalMode mode, Stream &out);

} // namespace DualCalibration