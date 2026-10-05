#pragma once
#include <stdint.h>

// ============================================================================
// CalStamp -- what a saved calibration was taken ON.
//
// Every saved file records two revisions and load() refuses the file when
// either differs from the running firmware's (Config.hpp):
//
//   CAL_HW_REV    the hardware the duty -> resistance relationship depends on
//                 (base-bleed resistors, emitter resistors, PWM filter, PNP/NPN
//                 type, LED series resistors, ...). Playing through a LUT taken
//                 on different hardware puts the cells at the wrong brightness
//                 -- potentially tens of dB too loud at the quiet end.
//   CAL_ALGO_REV  the meaning of the saved numbers on the firmware side (curve
//                 conventions, solve/trim semantics). Bump it by hand when a
//                 firmware change makes old files misleading.
//
// A refused file is the existing "no valid saved calibration" path: the board
// runs the first-boot calibration with the audio path disconnected and the amp
// muted. Pure function, tested on the host.
// ============================================================================
namespace CalStamp {

enum class Verdict : uint8_t { OK, HW_MISMATCH, ALGO_MISMATCH };

inline Verdict check(uint16_t fileHw, uint16_t fileAlgo, uint16_t curHw, uint16_t curAlgo) {
  if (fileHw != curHw) return Verdict::HW_MISMATCH;       // hardware first: the dangerous one
  if (fileAlgo != curAlgo) return Verdict::ALGO_MISMATCH;
  return Verdict::OK;
}

} // namespace CalStamp