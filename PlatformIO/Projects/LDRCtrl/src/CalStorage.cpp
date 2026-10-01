#include "CalStorage.hpp"
#include "LDRVolume.hpp" // full type needed here, not just the forward declaration
#include <LittleFS.h>

namespace CalStorage {

namespace {

constexpr uint32_t MAGIC = 0x4C445243; // 'LDRC'
// v2: added mode/rTotalOhms/rangeDb/autoRange/fixedSeriesDuty to the
// header so settings persist alongside the data that depends on them.
// A v1 file fails this version check and is correctly treated as "no
// valid saved calibration" rather than misread.
constexpr uint16_t FORMAT_VERSION = 2;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint8_t seriesCount;
  uint8_t shuntCount;
  uint8_t numSteps;
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
  hdr.mode = (uint8_t)vol.mode();
  hdr.autoRange = vol.autoRange() ? 1 : 0;
  hdr.fixedSeriesDuty = vol.fixedSeriesDuty();
  hdr.rTotalOhms = vol.rTotalOhms();
  hdr.rangeDb = vol.rangeDb();

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

  // Settings first -- setRTotalOhms/setMode/setFixedSeriesDuty/setRangeDb
  // each invalidate the (still-empty, at this point) LUT as a side
  // effect, which is harmless here. setRangeDb also forces autoRange
  // off, so setAutoRange is applied last to correctly restore it if the
  // saved state had it on.
  vol.setRTotalOhms(hdr.rTotalOhms);
  vol.setMode(hdr.mode == 1 ? LDRVolume::AttenuationMode::FIXED_SERIES
                            : LDRVolume::AttenuationMode::CONSTANT_RTOTAL);
  vol.setFixedSeriesDuty(hdr.fixedSeriesDuty);
  vol.setRangeDb(hdr.rangeDb);
  vol.setAutoRange(hdr.autoRange != 0);

  LdrCurve series, shunt;
  VolumeLut lut;
  series.rawLoad(seriesPts, hdr.seriesCount);
  shunt.rawLoad(shuntPts, hdr.shuntCount);
  lut.rawLoad(entries, hdr.numSteps);
  vol.calAdoptLoaded(series, shunt, lut); // sets state_=SOLVED, overriding the invalidations above
  return true;
}

} // namespace CalStorage