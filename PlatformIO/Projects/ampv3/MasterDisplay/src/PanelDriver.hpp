#pragma once
#include <Arduino_GFX_Library.h>
#include "Config.hpp"

// NV3007 panel driver with a workaround for how this panel stores pixels.
//
// Found with Settings -> Diagnostics -> Display Test: the panel drives pixel
// columns in PAIRS (x = 0-1, 2-3, 4-5, ...). Windows that cover whole pairs
// are always stored correctly; windows that split a pair are not:
//  - a 1-px-wide window disturbs its pair partner (walking 1-px markers
//    left trails on every other column, in either color);
//  - a 2-px window starting on an odd column shows up as two separated
//    lines (rows C/D of the alignment test).
// 1-px-TALL windows are fine in the tests; the bus itself is fine (data
// pattern clean, holding CS low changed nothing). But the Waveform -- which
// sends thousands of tiny windows per frame -- still left occasional stray
// dots with only the pair fix, and is clean once very short windows are grown
// to a few rows (minRows, DISPLAY_MIN_WINDOW_ROWS = 4), so this driver does
// that too, again from the shadow.
//
// So this driver widens every window to whole pairs -- start rounded down to
// even, end rounded up to odd -- and sends the extra columns with their
// CURRENT color, which it knows from a shadow copy of the screen kept up to
// date by every write (all drawing -- fills, lines, text, single pixels --
// funnels through writeFillRectPreclipped()/writePixelPreclipped()).
// Windows already pair-aligned go out unchanged, as a plain color fill.
//
// alignPairs can be switched at runtime (the Display Test toggles it with the
// remote's mute button) to compare.
class NV3007Display : public Arduino_NV3007 {
public:
    using Arduino_NV3007::Arduino_NV3007;

    static bool    alignPairs;
    static uint8_t minRows; // windows shorter than this are grown (from the shadow); 0/1 = off

    void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
        writeFillRectPreclipped(x, y, 1, 1, color);
    }

    void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        shadowFill(x, y, w, h, color);
        if (!alignPairs || w <= 0 || h <= 0) {
            Arduino_NV3007::writeFillRectPreclipped(x, y, w, h, color);
            return;
        }
        int16_t ax0 = x & ~1;                         // first column of the first pair
        int16_t ax1 = static_cast<int16_t>((x + w - 1) | 1); // last column of the last pair
        if (ax0 < 0) ax0 = 0;
        if (ax1 > kW - 1) ax1 = kW - 1;
        // Optionally grow short windows to minRows rows (downward, or upward
        // at the bottom edge) -- the extra rows also come from the shadow.
        int16_t ay = y, ah = h;
        if (ah < minRows) {
            ah = minRows;
            if (ay + ah > kH) ay = static_cast<int16_t>(kH - ah);
            if (ay < 0) { ay = 0; ah = kH; }
        }
        if (ax0 == x && ax1 == x + w - 1 && ay == y && ah == h) {
            Arduino_NV3007::writeFillRectPreclipped(x, y, w, h, color); // already whole pairs
        } else {
            writeFromShadow(ax0, ay, ax1 - ax0 + 1, ah);
        }
    }

private:
    static constexpr int16_t kW = SCREEN_WIDTH;
    static constexpr int16_t kH = SCREEN_HEIGHT;
    static_assert((kW & 1) == 0, "pair alignment assumes an even screen width");
    static uint16_t s_shadow[kH][kW]; // what the panel shows, logical (rotated) coordinates

    static void shadowFill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        int16_t x1 = x + w, y1 = y + h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x1 > kW) x1 = kW;
        if (y1 > kH) y1 = kH;
        for (int16_t r = y; r < y1; ++r) {
            uint16_t* row = s_shadow[r];
            for (int16_t c = x; c < x1; ++c) row[c] = color;
        }
    }

    // Sends a window's pixels straight from the shadow (row by row, the
    // order the panel fills a window in).
    void writeFromShadow(int16_t x, int16_t y, int16_t w, int16_t h) {
        if (y < 0) { h += y; y = 0; }
        if (y + h > kH) h = kH - y;
        if (h <= 0 || w <= 0) return;

        writeAddrWindow(x, y, w, h);
        uint16_t buf[64];
        size_t   n = 0;
        for (int16_t r = 0; r < h; ++r) {
            const uint16_t* row = &s_shadow[y + r][x];
            for (int16_t c = 0; c < w; ++c) {
                buf[n++] = row[c];
                if (n == sizeof(buf) / sizeof(buf[0])) {
                    _bus->writePixels(buf, n);
                    n = 0;
                }
            }
        }
        if (n) _bus->writePixels(buf, n);
    }
};
