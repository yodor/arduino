#pragma once
// Actions shared by both modes of a serial link (the strict protocol and the console): the
// work and its checks live here once; each mode only formats its own reply.
#include <Arduino.h>
#include "AutoPref.hpp"
#include "VerifyHistory.hpp"

class LDRVolume;
class Board;
struct CalRequest;

namespace Commands {

// CAL's arguments ("", "R=<ohms>", "R=AUTO"; upper-case) checked against the hardware: an
// ADS1115 present, R within the range the last calibration measured. nullptr = OK (req
// filled), otherwise the reason (static text, valid until the next call).
const char *validateCal(const char *args, LDRVolume &left, LDRVolume &right, CalRequest &req);

// AMP_MUTE: the amp pin, and the commanded state the resync hint's warm-up logic follows.
void ampMute(Board &board, bool muted);

// SET SRC / SET LOAD: store the value; a calibration solved for other values turns STALE.
// false = out of range (nothing changed).
bool setModel(bool source, float ohms, LDRVolume &left, LDRVolume &right);

// SET AUTO: store the preference; in AUTO mode a changed pick turns the calibration STALE.
// Returns AUTO's new pick (0 if not in AUTO mode or nothing qualifies).
float setAutoPref(AutoPref::Pref pref, LDRVolume &left, LDRVolume &right);

// After CAL VERIFY: make the run's walk the stored walk (AUTO, depth reach, deep-step refinement,
// comparisons use it) -- automatically, but only when that is safe:
//   not when the verdict is CHECK (L and R differ, or a shape change: maybe a fault -- left for a person),
//   not when AUTO would then choose another Rtotal (that would make the calibration STALE and refuse
//   the volume just because of a check -- reported instead), nor, with a manual Rtotal, when the new
//   walk does not reach it.
// Stored in effective drive (commanded duty + that run's compensation offset).
enum class Adopt : uint8_t { ADOPTED, CHECK, AUTO_WOULD_CHANGE, NOT_REACHED, TOO_FEW_POINTS, CAL_CHANGED };
Adopt adoptWalk(const VerifyHistory::Record &r, LDRVolume &left, LDRVolume &right, float &newPick);
const char *adoptText(Adopt a);
// Whether the stored walk is this run's (adopted), for VERIFY RESULT.
bool walkAdopted(const VerifyHistory::Record &r, const LDRVolume &left, const LDRVolume &right);

} // namespace Commands
