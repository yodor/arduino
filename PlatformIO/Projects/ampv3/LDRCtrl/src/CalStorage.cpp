#include "CalStorage.hpp"
#include "LDRVolume.hpp" // full type needed here, not just the forward declaration
#include <LittleFS.h>
#include "DriverChannels.hpp" // DriverChannels::WRAP -- saved duties are only meaningful at the WRAP they were taken at
#include "Config.hpp"        // CAL_HW_REV / CAL_ALGO_REV
#include "CalStamp.hpp"
#include "Capabilities.hpp"

namespace CalStorage {

namespace {

constexpr uint32_t MAGIC = 0x4C445243; // 'LDRC'
// v2: added mode/rTotalOhms/rangeDb/autoRange/fixedSeriesDuty to the
// header so settings persist alongside the data that depends on them.
// A v1 file fails this version check and is correctly treated as "no
// valid saved calibration" rather than misread.
// Bumped 2 -> 3 alongside DriverChannels::WRAP going from 4095 to 1023:
// a v2 file's saved duties are scaled against the OLD 12-bit range and
// would silently misapply (clamped, wrong steps) if loaded as-is against
// the new 10-bit PWM. Bumping this makes calLoad() reject any old file
// automatically, falling through to LDRVolume::begin()'s existing
// fresh-calibration path -- no manual flash-clearing required.
// Bumped 3 -> 4 alongside DriverChannels::WRAP going from 1023 to 749
// (200kHz PWM): same reasoning as the earlier 2->3 bump -- a v3 file's
// saved duties are scaled against the old 1023-max range and would
// silently misapply (clamped, wrong steps) if loaded as-is. This makes
// calLoad() reject the old file automatically, falling through to a
// fresh characterization -- no manual flash-clearing required.
// Bumped 4 -> 5 alongside DriverChannels::WRAP going from 749 back to
// 4095 (full 12-bit resolution, for current-driver-stage bench testing).
// Same reasoning as every prior bump: a v4 file's saved duties are scaled
// against the 749-max range and would silently misapply if loaded as-is.
// Bumped 5 -> 6: the header now records the PWM WRAP the saved duties were
// taken at, and load rejects the file on any mismatch. Before this, every
// WRAP change meant remembering to bump this version by hand -- forgetting
// would silently misapply duties scaled to the wrong range. Now changing
// LED_PWM_WRAP in Config.hpp is enough: the old file is refused and the
// normal "no valid saved calibration" path runs a full characterization at boot.
// Bumped 6 -> 7: the header now carries the hardware revision and calibration
// revision the file was taken on (CAL_HW_REV / CAL_ALGO_REV, see CalStamp.hpp).
// A v6 file has no stamp, so it cannot be vouched for and is refused: the
// first boot after this change recalibrates once.
// Bumped 7 -> 8: the FIXED_SERIES attenuation mode was removed, so the header no
// longer carries mode / autoRange / fixedSeriesDuty. Older files are refused once
// and recalibrated (the layout differs, so they cannot be read safely).
// Bumped 8 -> 9: LUT duties are now FINE units (count x PWM_DITHER_LEVELS, uint32)
// for the dither engine, so the entry layout changed. Older files are refused once
// and recalibrated.
// Bumped 9 -> 10: the header also stores the Rtotal range the calibration measured and
// whether Rtotal was chosen automatically, so R=<ohms> can be checked after a reboot.
// Bumped 10 -> 11: the series cell's measured dark end (DarkProfile) follows the LUT.
// Bumped 11 -> 12: then the cells' bright-end fingerprint and the calibration history
// (CellCheck.hpp), so a single CAL can tell the same cells from changed ones.
constexpr uint16_t FORMAT_VERSION = 12;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint8_t seriesCount;
  uint8_t shuntCount;
  uint8_t numSteps;
  uint16_t pwmWrap;        // DriverChannels::WRAP at save time
  uint16_t hwRev;          // CAL_HW_REV at save time
  uint16_t calRev;         // CAL_ALGO_REV at save time
  float rTotalOhms;
  float rangeDb;  // informational (read back from the LUT); ignored on load
  float rMinOhms; // the Rtotal range the calibration measured (0/0 = unknown)
  float rMaxOhms;
  uint32_t rAuto; // 1 = Rtotal was chosen by the calibration (AUTO), 0 = by the master
  uint32_t crc32; // over rTotalOhms, rangeDb, then the series/shunt/entry arrays, in that order
};

// Standard bitwise CRC32 (no table -- data here is at most a few hundred
// bytes, so table-free is plenty fast and keeps this file self-contained).
uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

} // namespace

bool save(const char *path, const LDRVolume &vol) {
  const LdrCurve &series = vol.seriesCurve();
  const LdrCurve &shunt = vol.shuntCurve();
  const VolumeLut &lut = vol.lut();

  Header hdr;
  hdr.magic = MAGIC;
  hdr.version = FORMAT_VERSION;
  hdr.seriesCount = series.count();
  hdr.shuntCount = shunt.count();
  hdr.numSteps = lut.numSteps();
  hdr.pwmWrap = DriverChannels::WRAP;
  hdr.hwRev = CAL_HW_REV;
  hdr.calRev = CAL_ALGO_REV;
  hdr.rTotalOhms = vol.rTotalOhms();
  hdr.rangeDb = vol.rangeDb(); // informational (read back from the LUT); ignored on load
  hdr.rMinOhms = vol.rangeMinOhms();
  hdr.rMaxOhms = vol.rangeMaxOhms();
  hdr.rAuto = vol.rAuto() ? 1u : 0u;

  size_t sBytes = (size_t)hdr.seriesCount * sizeof(LdrCurve::Point);
  size_t shBytes = (size_t)hdr.shuntCount * sizeof(LdrCurve::Point);
  size_t eBytes = (size_t)hdr.numSteps * sizeof(VolumeLut::Entry);

  uint32_t crc = 0;
  crc = crc32Update(crc, (const uint8_t *)&hdr.rTotalOhms, sizeof(hdr.rTotalOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rangeDb, sizeof(hdr.rangeDb));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rMinOhms, sizeof(hdr.rMinOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rMaxOhms, sizeof(hdr.rMaxOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rAuto, sizeof(hdr.rAuto));
  crc = crc32Update(crc, (const uint8_t *)series.rawPoints(), sBytes);
  crc = crc32Update(crc, (const uint8_t *)shunt.rawPoints(), shBytes);
  crc = crc32Update(crc, (const uint8_t *)lut.rawEntries(), eBytes);
  const DarkProfile &prof = vol.darkProfile();
  crc = crc32Update(crc, (const uint8_t *)&prof, sizeof(DarkProfile));
  const CellFingerprint &fp = vol.fingerprint();
  const CalMeta &meta = vol.calMeta();
  crc = crc32Update(crc, (const uint8_t *)&fp, sizeof(CellFingerprint));
  crc = crc32Update(crc, (const uint8_t *)&meta, sizeof(CalMeta));
  hdr.crc32 = crc;

  File f = LittleFS.open(path, "w");
  if (!f) return false;

  size_t written = f.write((const uint8_t *)&hdr, sizeof(hdr));
  written += f.write((const uint8_t *)series.rawPoints(), sBytes);
  written += f.write((const uint8_t *)shunt.rawPoints(), shBytes);
  written += f.write((const uint8_t *)lut.rawEntries(), eBytes);
  written += f.write((const uint8_t *)&prof, sizeof(DarkProfile));
  written += f.write((const uint8_t *)&fp, sizeof(CellFingerprint));
  written += f.write((const uint8_t *)&meta, sizeof(CalMeta));
  f.close();

  size_t expected = sizeof(hdr) + sBytes + shBytes + eBytes + sizeof(DarkProfile) + sizeof(CellFingerprint) + sizeof(CalMeta);
  return written == expected;
}

bool load(const char *path, LDRVolume &vol) {
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  Header hdr;
  if (f.read((uint8_t *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) { f.close(); return false; }
  if (hdr.magic != MAGIC || hdr.version != FORMAT_VERSION) { f.close(); return false; }
  if (hdr.pwmWrap != DriverChannels::WRAP) {
    // Saved at a different PWM resolution -- duties would misapply.
    Serial.print(F("Saved calibration was taken at PWM WRAP="));
    Serial.print(hdr.pwmWrap);
    Serial.print(F(", firmware is now "));
    Serial.print(DriverChannels::WRAP);
    Serial.println(F(" -- discarding it, recalibration required."));
    f.close();
    return false;
  }
  switch (CalStamp::check(hdr.hwRev, hdr.calRev, CAL_HW_REV, CAL_ALGO_REV)) {
    case CalStamp::Verdict::HW_MISMATCH:
      Serial.print(F("Saved calibration "));
      Serial.print(path);
      Serial.print(F(" was taken on hardware rev "));
      Serial.print(hdr.hwRev);
      Serial.print(F(", this firmware is built for rev "));
      Serial.print(CAL_HW_REV);
      Serial.println(F(" -- REFUSED (a stale LUT would put the cells at the wrong brightness); recalibration required."));
      f.close();
      return false;
    case CalStamp::Verdict::ALGO_MISMATCH:
      Serial.print(F("Saved calibration "));
      Serial.print(path);
      Serial.print(F(" has calibration rev "));
      Serial.print(hdr.calRev);
      Serial.print(F(", this firmware uses rev "));
      Serial.print(CAL_ALGO_REV);
      Serial.println(F(" -- refused; recalibration required."));
      f.close();
      return false;
    case CalStamp::Verdict::OK:
      break;
  }
  if (hdr.seriesCount > LdrCurve::MAX_POINTS ||
      hdr.shuntCount > LdrCurve::MAX_POINTS ||
      hdr.numSteps > VolumeLut::MAX_STEPS) {
    f.close();
    return false;
  }

  static LdrCurve::Point seriesPts[LdrCurve::MAX_POINTS];
  static LdrCurve::Point shuntPts[LdrCurve::MAX_POINTS];
  static VolumeLut::Entry entries[VolumeLut::MAX_STEPS];

  size_t sBytes = (size_t)hdr.seriesCount * sizeof(LdrCurve::Point);
  size_t shBytes = (size_t)hdr.shuntCount * sizeof(LdrCurve::Point);
  size_t eBytes = (size_t)hdr.numSteps * sizeof(VolumeLut::Entry);

  if (f.read((uint8_t *)seriesPts, sBytes) != (int)sBytes) { f.close(); return false; }
  if (f.read((uint8_t *)shuntPts, shBytes) != (int)shBytes) { f.close(); return false; }
  if (f.read((uint8_t *)entries, eBytes) != (int)eBytes) { f.close(); return false; }
  DarkProfile prof;
  if (f.read((uint8_t *)&prof, sizeof(DarkProfile)) != (int)sizeof(DarkProfile)) { f.close(); return false; }
  if (prof.n > DarkProfile::MAX_POINTS) { f.close(); return false; }
  CellFingerprint fp;
  CalMeta meta;
  if (f.read((uint8_t *)&fp, sizeof(CellFingerprint)) != (int)sizeof(CellFingerprint)) { f.close(); return false; }
  if (f.read((uint8_t *)&meta, sizeof(CalMeta)) != (int)sizeof(CalMeta)) { f.close(); return false; }
  f.close();

  uint32_t crc = 0;
  crc = crc32Update(crc, (const uint8_t *)&hdr.rTotalOhms, sizeof(hdr.rTotalOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rangeDb, sizeof(hdr.rangeDb));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rMinOhms, sizeof(hdr.rMinOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rMaxOhms, sizeof(hdr.rMaxOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rAuto, sizeof(hdr.rAuto));
  crc = crc32Update(crc, (const uint8_t *)seriesPts, sBytes);
  crc = crc32Update(crc, (const uint8_t *)shuntPts, shBytes);
  crc = crc32Update(crc, (const uint8_t *)entries, eBytes);
  crc = crc32Update(crc, (const uint8_t *)&prof, sizeof(DarkProfile));
  crc = crc32Update(crc, (const uint8_t *)&fp, sizeof(CellFingerprint));
  crc = crc32Update(crc, (const uint8_t *)&meta, sizeof(CalMeta));
  if (crc != hdr.crc32) return false; // corrupt file -- reject, don't half-apply it


  // Settings first -- setRTotalOhms() invalidates the (still-empty, at this
  // point) LUT as a side effect, which is harmless here. hdr.rangeDb is
  // informational and deliberately ignored: the range is always computed, and
  // it is recoverable from the loaded LUT itself (step 0's target is -range).
  vol.setRTotalOhms(hdr.rTotalOhms, hdr.rAuto == 0);
  vol.setRange(hdr.rMinOhms, hdr.rMaxOhms);

  LdrCurve series, shunt;
  VolumeLut lut;
  series.rawLoad(seriesPts, hdr.seriesCount);
  shunt.rawLoad(shuntPts, hdr.shuntCount);
  lut.rawLoad(entries, hdr.numSteps);
  vol.calAdoptLoaded(series, shunt, lut); // sets state_=SOLVED, overriding the invalidations above
  vol.refreshTransparentTop();            // the top step is derived, not stored data
  vol.setDarkProfile(prof);
  vol.setFingerprint(fp);
  vol.setCalMeta(meta);
  // The Rtotal RANGE is recomputed from the loaded curves rather than trusted from the
  // file: the RMIN/RMAX rules can change between firmware builds, and the curves are what
  // the range is derived from. (R itself and the AUTO flag are kept as saved.)
  {
    using namespace Capabilities;
    const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
    ChannelCaps cc[1] = {measure(vol.seriesCurve(), vol.shuntCurve(), &vol.darkProfile())};
    const LdrCurve *ser[1] = {&vol.seriesCurve()};
    const RtotalChoice rc = chooseRtotal(cc, ser, 1, true, vol.rTotalOhms(), load,
                                         RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
    if (rc.rangeOk) vol.setRange(rc.rMin, rc.rMax);
  }
  Serial.print(F("Calibration stamp accepted: hardware rev "));
  Serial.print(hdr.hwRev);
  Serial.print(F(", calibration rev "));
  Serial.println(hdr.calRev);
  return true;
}

} // namespace CalStorage

bool CalStorage::erase(const char *path) {
  if (!LittleFS.exists(path)) return true;
  return LittleFS.remove(path);
}
