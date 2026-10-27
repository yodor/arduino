#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// Diagnostics -> Display Test: drawing patterns that tell apart
// WHICH display line loses writes. Each test runs continuously; leftovers
// accumulate on screen, so even a rare loss becomes visible.
//
//   1 PEAK WALK        markers walking left 1px/frame, erase + draw each frame,
//                      every fill its own transaction (CS toggles per fill) --
//                      exactly what the meters do. Trails = lost erases.
//   2 WALK, CS HELD    the same, but the whole frame in ONE transaction: CS
//                      stays low, only DC toggles. Clean here but not in 1
//                      -> the CS line (its edges) is the culprit.
//   3 SAME SPOT        draw + erase repeatedly on the SAME spot: the driver
//                      skips the window commands (CASET/RASET), so far fewer
//                      DC command/data switches. Clean here but not in 1/2
//                      -> the DC line.
//   4 INVERTED         test 1 with black markers on white: do lost writes
//                      depend on the color (only black erases failing)?
//   5 DATA PATTERN     a 0x5555/0xAAAA checkerboard redrawn every frame --
//                      specks or wrong colors mean bit errors on SCK/MOSI.
//   6 ALIGNMENT        eight walking markers, labelled A-H, each with a
//                      different start-address parity / size (see the
//                      table in DisplayTestScreen.cpp). Which rows trail
//                      tells exactly which alignment the panel needs.
//                      Result: only 1-px-WIDE windows lose writes.
//   7 THIN ROWS        1-px-TALL markers walking upward (like the spectrum
//                      peak lines). Result: not affected.
//   8 TINY WINDOWS     the Waveform's diet: four areas A-D, each dotting 8
//                      tiny windows per frame and erasing them again --
//                      A 2x1 (even y), B 2x1 (odd y), C 2x2, D 4x1. Any dots
//                      left = tiny windows get lost. Result: clean here --
//                      yet the Waveform needed the minimum-rows fix.
//
// The panel workarounds (PanelDriver.hpp) apply to all tests: MUTE on the
// remote switches the pixel-pair fix, the channel-mode button the minimum
// window height (off / 4 rows), so both can be compared live.
//
// Controls: Up/Down = previous/next test, Right = clear and restart the
// test, Left or the menu button = leave.
class DisplayTestScreen : public Screen {
public:
    void onEnter() override;   // starts at the current test, cleared
    void render() override;
    const char* name() const override { return "DISPLAY TEST"; }

    void nextTest();
    void prevTest();
    void restart();
    void toggleColumnFix(); // remote MUTE
    void toggleMinRows();   // remote channel-mode button

    // Menu action -> main.cpp: the menu can only call a plain function, so
    // it raises this request and main.cpp switches over to the test.
    static void requestStart() { s_startRequested = true; }
    static bool consumeStartRequest() { bool r = s_startRequested; s_startRequested = false; return r; }

private:
    enum Test : uint8_t { PEAK_WALK, WALK_CS_HELD, SAME_SPOT, INVERTED, DATA_PATTERN, ALIGNMENT, THIN_ROWS, TINY_WINDOWS, TEST_COUNT };

    void drawHeader();
    void stepWalk(bool oneTransaction, bool inverted);
    void stepSameSpot();
    void stepDataPattern();
    void stepAlignment();
    void stepThinRows();
    void stepTinyWindows();
    void drawFixState();

    static constexpr uint8_t kRows   = 8;   // walking markers
    static constexpr int16_t kTop    = 22;  // test area starts under the header
    static constexpr int16_t kRowH   = 14;
    static constexpr int16_t kRowGap = 1;

    uint8_t  m_test = PEAK_WALK;
    int16_t  m_pos[kRows] = {};
    uint32_t m_frames = 0;
    uint32_t m_rng    = 12345;
    bool     m_phase  = false;

    static bool s_startRequested;
};
