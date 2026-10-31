#pragma once
#include <Arduino.h>

class LDRVolume; // forward declaration only -- avoids a circular include
                  // with LDRVolume.hpp, which includes this header to call
                  // CalStorage::save/load from its own inline methods.

// ============================================================================
// CalStorage
//
// Saves/loads one channel's full calibration state to/from LittleFS:
// characterization curves, solved LUT, AND the settings that determine
// what a future solve would produce (rTotalOhms, rangeDb) -- so a power cycle can never silently revert to
// Config.hpp's defaults while a LUT solved under different settings is
// still loaded and in use. File format: a header (magic, format version,
// point/step counts, the settings fields, CRC32 of everything but itself)
// then the raw Point/Entry arrays. The CRC + magic + version guard means a
// corrupted or format-mismatched file is detected and rejected on load --
// load() returning false means "no valid saved calibration," full stop;
// callers should fall back to characterizing fresh, not attempt to use
// partial data.
//
// LittleFS.begin() must have already been called (Board::begin() does
// this) before either function here is used.
// ============================================================================
namespace CalStorage {

bool save(const char *path, const LDRVolume &vol);
bool load(const char *path, LDRVolume &vol);
bool erase(const char *path); // delete the stored calibration (true if nothing is left)

} // namespace CalStorage
