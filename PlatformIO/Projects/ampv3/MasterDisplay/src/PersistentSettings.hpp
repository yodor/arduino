#pragma once
#include <Arduino.h>

// Settings, saved as a plain text file on LittleFS: /settings.txt, one
// "key=value" per line ('#' starts a comment). Version-independent by design:
// unknown keys are ignored, missing ones keep their defaults, and enums are
// stored by NAME -- so firmware updates never need a migration, and the file
// can be read, edited, or flashed from a data/settings.txt with the
// Arduino-Pico filesystem uploader.
//
// Needs a Flash Size option with an FS partition (Tools menu, e.g.
// "4MB (Sketch: 3MB, FS: 1MB)"); without one, LittleFS can't mount and
// settings simply aren't saved (logged at boot).
//
// Flash writes are safe with core 1 running: Arduino-Pico's LittleFS pauses
// the other core itself (rp2040.idleOtherCore()) during erases and writes.
class PersistentSettings {
public:
    static PersistentSettings& instance();

    // Mounts LittleFS and applies /settings.txt. If there's no file yet but
    // the old binary settings (layout v13, last flash sector) are there,
    // they're imported once and written out as the file.
    void loadAndApply();

    void markDirty() { m_dirty = true; m_lastDirtyMs = millis(); }
    bool isDirty() const { return m_dirty; }
    // For deferred saves: how long since the last change.
    uint32_t msSinceDirty() const { return millis() - m_lastDirtyMs; }
    void saveIfDirty();

    // USB console: print the file / delete it (defaults from the next boot).
    void printFile();
    bool resetFile();

private:
    PersistentSettings() = default;
    void save();
    bool importLegacy();
    void applyLine(char* line, int lineNo);

    uint32_t m_lastDirtyMs = 0;
    bool     m_dirty       = false;
    bool     m_mounted     = false;
};
