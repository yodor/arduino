#include "Commands.hpp"
#include "LDRVolume.hpp"
#include "Board.hpp"
#include "CalArgs.hpp"
#include "DualCalibration.hpp"
#include "AudioModel.hpp"
#include "VerifyAnalysis.hpp"
#include <math.h>
#include <stdio.h>

namespace Commands {

const char *validateCal(const char *args, LDRVolume &left, LDRVolume &right, CalRequest &req) {
  static char why[96];
  bool hasR = false, rAuto = false, full = false, verify = false;
  long ohms = 0;
  if (!CalArgs::parseCal(args, hasR, rAuto, ohms, full, verify)) return "CAL args: nothing, R=<ohms> within RMIN..RMAX, R=AUTO, FULL, or VERIFY";
  if (!left.adsPresent() && !right.adsPresent()) return "no ADS1115 detected on either channel";
  if (hasR && !(left.rangeKnown() && right.rangeKnown())) return "R range unknown -- send CAL first";
  const float mn = left.rangeMinOhms() > right.rangeMinOhms() ? left.rangeMinOhms() : right.rangeMinOhms();
  const float mx = left.rangeMaxOhms() < right.rangeMaxOhms() ? left.rangeMaxOhms() : right.rangeMaxOhms();
  if (hasR && ((float)ohms < mn || (float)ohms > mx)) {
    snprintf(why, sizeof why, "R=%ld outside the achievable range %ld..%ld", ohms, (long)mn, (long)mx);
    return why;
  }
  if (verify && !(left.hasUsableLut() && right.hasUsableLut())) return "VERIFY needs a calibration -- send CAL first";
  req.full = full; req.verify = verify; req.hasR = hasR; req.rAuto = rAuto; req.ohms = ohms;
  return nullptr;
}

void ampMute(Board &board, bool muted) {
  board.setAmpMute(muted);   // during a calibration's hold an unmute waits for its end
  Board::noteAmpCommand(muted);
}

bool setModel(bool source, float ohms, LDRVolume &left, LDRVolume &right) {
  if (!(source ? AudioModel::setSource(ohms) : AudioModel::setLoad(ohms))) return false;
  DualCalibration::onModelChanged(left, right);
  return true;
}

float setAutoPref(AutoPref::Pref pref, LDRVolume &left, LDRVolume &right) {
  AutoPref::set(pref);
  return DualCalibration::onAutoPrefChanged(left, right);
}

namespace {
DarkProfile effective(const DarkProfile &p, float off) {
  DarkProfile e = p;
  const long o = lroundf(isnan(off) ? 0.0f : off);
  for (uint8_t i = 0; i < e.n; i++) e.p[i].duty = (uint16_t)((long)e.p[i].duty + o);
  return e;
}
bool sameProfile(const DarkProfile &a, const DarkProfile &b) {
  if (a.n != b.n) return false;
  for (uint8_t i = 0; i < a.n; i++)
    if (a.p[i].duty != b.p[i].duty || a.p[i].ohms != b.p[i].ohms || a.p[i].ok != b.p[i].ok) return false;
  return true;
}
} // namespace

Adopt adoptWalk(const VerifyHistory::Record &r, LDRVolume &left, LDRVolume &right, float &newPick) {
  newPick = NAN;
  if (r.calFull != left.calMeta().fullCount || r.calResync != left.calMeta().resyncCount || r.rTotal != left.rTotalOhms())
    return Adopt::CAL_CHANGED;
  if (VerifyHistory::summarize(r).verdict == (uint8_t)VerifyAnalysis::Verdict::CHECK) return Adopt::CHECK;
  if (r.walk[0].okCount() < 2 || r.walk[1].okCount() < 2) return Adopt::TOO_FEW_POINTS;
  LDRVolume *ch[2] = {&left, &right};
  DarkProfile old[2], neu[2];
  for (uint8_t c = 0; c < 2; c++) { old[c] = ch[c]->darkProfile(); neu[c] = effective(r.walk[c], r.compOff[c]); }
  for (uint8_t c = 0; c < 2; c++) ch[c]->setDarkProfile(neu[c]);          // try it ...
  bool ok = true;
  Adopt why = Adopt::ADOPTED;
  if (left.rAuto() && right.rAuto()) {
    newPick = DualCalibration::autoChoiceOhms(left, right);
    if (!(newPick > 0.0f) || fabsf(newPick - left.rTotalOhms()) > 0.005f * left.rTotalOhms()) { ok = false; why = Adopt::AUTO_WOULD_CHANGE; }
  } else if (DualCalibration::walkReachOhms(left, right) < left.rTotalOhms()) {
    ok = false; why = Adopt::NOT_REACHED;
  }
  if (!ok) { for (uint8_t c = 0; c < 2; c++) ch[c]->setDarkProfile(old[c]); return why; }   // ... or put it back
  for (uint8_t c = 0; c < 2; c++) ch[c]->calSave();
  return Adopt::ADOPTED;
}

const char *adoptText(Adopt a) {
  switch (a) {
    case Adopt::ADOPTED: return "the walk was adopted as the stored walk";
    case Adopt::CHECK: return "walk NOT adopted: the verdict is CHECK -- have a look first";
    case Adopt::AUTO_WOULD_CHANGE: return "walk NOT adopted: AUTO would then choose another Rtotal (CAL FULL measures afresh)";
    case Adopt::NOT_REACHED: return "walk NOT adopted: it does not reach the manual Rtotal";
    case Adopt::TOO_FEW_POINTS: return "walk NOT adopted: too few settled points";
    default: return "walk NOT adopted: the calibration changed since that run";
  }
}

bool walkAdopted(const VerifyHistory::Record &r, const LDRVolume &left, const LDRVolume &right) {
  return sameProfile(left.darkProfile(), effective(r.walk[0], r.compOff[0])) &&
         sameProfile(right.darkProfile(), effective(r.walk[1], r.compOff[1]));
}

} // namespace Commands
