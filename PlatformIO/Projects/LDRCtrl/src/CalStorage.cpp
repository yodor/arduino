#include "CalStorage.hpp"
#include "LDRVolume.hpp" // full type needed here, not just the forward declaration
#include <LittleFS.h>
#include "DriverChannels.hpp" // DriverChannels::WRAP -- saved duties are only meaningful at the WRAP they were taken at

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
// normal "no valid saved calibration" path runs a fresh CALAUTO at boot.
constexpr uint16_t FORMAT_VERSION = 6;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint8_t seriesCount;
  uint8_t shuntCount;
  uint8_t numSteps;
  uint16_t pwmWrap;        // DriverChannels::WRAP at save time
  uint8_t mode;            // LDRVolume::AttenuationMode
  uint8_t autoRange;       // 0/1
  uint16_t fixedSeriesDuty;
  float rTotalOhms;
  float rangeDb;
  uint32_t crc32; // over mode..rangeDb fields, then the series/shunt/entry arrays, in that order
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
  hdr.mode = (uint8_t)vol.mode();
  hdr.autoRange = 1; // legacy field, kept so the file layout (and every existing file) stays valid:
                     // the range is always computed now, so this is always 1 and ignored on load
  hdr.fixedSeriesDuty = vol.fixedSeriesDuty();
  hdr.rTotalOhms = vol.rTotalOhms();
  hdr.rangeDb = vol.rangeDb(); // informational (read back from the LUT); ignored on load

  size_t sBytes = (size_t)hdr.seriesCount * sizeof(LdrCurve::Point);
  size_t shBytes = (size_t)hdr.shuntCount * sizeof(LdrCurve::Point);
  size_t eBytes = (size_t)hdr.numSteps * sizeof(VolumeLut::Entry);

  uint32_t crc = 0;
  crc = crc32Update(crc, (const uint8_t *)&hdr.mode, sizeof(hdr.mode));
  crc = crc32Update(crc, (const uint8_t *)&hdr.autoRange, sizeof(hdr.autoRange));
  crc = crc32Update(crc, (const uint8_t *)&hdr.fixedSeriesDuty, sizeof(hdr.fixedSeriesDuty));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rTotalOhms, sizeof(hdr.rTotalOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rangeDb, sizeof(hdr.rangeDb));
  crc = crc32Update(crc, (const uint8_t *)series.rawPoints(), sBytes);
  crc = crc32Update(crc, (const uint8_t *)shunt.rawPoints(), shBytes);
  crc = crc32Update(crc, (const uint8_t *)lut.rawEntries(), eBytes);
  hdr.crc32 = crc;

  File f = LittleFS.open(path, "w");
  if (!f) return false;

  size_t written = f.write((const uint8_t *)&hdr, sizeof(hdr));
  written += f.write((const uint8_t *)series.rawPoints(), sBytes);
  written += f.write((const uint8_t *)shunt.rawPoints(), shBytes);
  written += f.write((const uint8_t *)lut.rawEntries(), eBytes);
  f.close();

  size_t expected = sizeof(hdr) + sBytes + shBytes + eBytes;
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
  f.close();

  uint32_t crc = 0;
  crc = crc32Update(crc, (const uint8_t *)&hdr.mode, sizeof(hdr.mode));
  crc = crc32Update(crc, (const uint8_t *)&hdr.autoRange, sizeof(hdr.autoRange));
  crc = crc32Update(crc, (const uint8_t *)&hdr.fixedSeriesDuty, sizeof(hdr.fixedSeriesDuty));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rTotalOhms, sizeof(hdr.rTotalOhms));
  crc = crc32Update(crc, (const uint8_t *)&hdr.rangeDb, sizeof(hdr.rangeDb));
  crc = crc32Update(crc, (const uint8_t *)seriesPts, sBytes);
  crc = crc32Update(crc, (const uint8_t *)shuntPts, shBytes);
  crc = crc32Update(crc, (const uint8_t *)entries, eBytes);
  if (crc != hdr.crc32) return false; // corrupt file -- reject, don't half-apply it

  // Settings first -- setRTotalOhms/setMode/setFixedSeriesDuty each
  // invalidate the (still-empty, at this point) LUT as a side effect, which
  // is harmless here. hdr.autoRange / hdr.rangeDb are legacy/informational
  // and deliberately ignored: the range is always computed, and it is
  // recoverable from the loaded LUT itself (step 0's target is -range).
  vol.setRTotalOhms(hdr.rTotalOhms);
  vol.setMode(hdr.mode == 1 ? LDRVolume::AttenuationMode::FIXED_SERIES
                            : LDRVolume::AttenuationMode::CONSTANT_RTOTAL);
  vol.setFixedSeriesDuty(hdr.fixedSeriesDuty);

  LdrCurve series, shunt;
  VolumeLut lut;
  series.rawLoad(seriesPts, hdr.seriesCount);
  shunt.rawLoad(shuntPts, hdr.shuntCount);
  lut.rawLoad(entries, hdr.numSteps);
  vol.calAdoptLoaded(series, shunt, lut); // sets state_=SOLVED, overriding the invalidations above
  return true;
}

} // namespace CalStorage