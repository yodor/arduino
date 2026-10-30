#include "SpectrumScreen.hpp"
#include "Console.hpp"
#include "DebugFlags.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "AudioLevel.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "BarPhysics.hpp"
#include <cmath>

namespace {
// Computes bar width for `totalBars` bars (with `totalBars+1` gaps total --
// one leading, one trailing, one between each pair) fit into `screenWidth`
// pixels, never returning less than 1. Uses signed arithmetic internally
// so a pathologically large totalBars/gap combination clamps safely
// instead of silently underflowing the unsigned subtraction this used to
// be computed with directly (an underflow there wraps to a huge positive
// number rather than 0, corrupting the whole render rather than just
// looking cramped).
uint16_t computeBarWidth(uint16_t screenWidth, uint16_t totalBars, uint16_t gap) {
    if (totalBars == 0) return 1;
    int32_t available = static_cast<int32_t>(screenWidth) - static_cast<int32_t>(totalBars + 1) * gap;
    int32_t width     = available / static_cast<int32_t>(totalBars);
    return (width < 1) ? 1 : static_cast<uint16_t>(width);
}
} // namespace

void SpectrumScreen::resetPrevHeights(BarPhysicsState* state) {
    for (size_t i = 0; i < MAX_BAND_COUNT; ++i) {
        state[i].prevUnits     = 0;
        state[i].prevPeakUnits = 0;
    }
}

void SpectrumScreen::onEnter() {
    Renderer::instance().clear();

    // Reset only the "what's currently drawn" tracking to match the now-
    // blank screen -- deliberately NOT resetting decay/peak/peakTimer, so
    // the audio-reactive smoothing state carries over continuously across
    // screen switches and channel-mode/layout toggles, rather than
    // jumping back to zero every time.
    resetPrevHeights(m_left);
    resetPrevHeights(m_right);
    resetPrevHeights(m_mono);
}

void SpectrumScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    // One consistent snapshot: the bars and the band count of the same FFT.
    float leftBars[MAX_BAND_COUNT];
    float rightBars[MAX_BAND_COUNT];
    size_t totalBars = AudioEngine::instance().getSpectrumBars(leftBars, rightBars);
    if (totalBars == 0 || totalBars > MAX_BAND_COUNT) return; // defensive -- never index past our arrays

    const DisplaySettings& ds = DisplaySettings::instance();
    bool stereo     = ds.getChannelMode() == ChannelMode::STEREO;
    bool sideBySide = ds.getStereoLayout() == StereoLayout::SIDE_BY_SIDE;
    bool mirrorLeft = ds.getMirrorLeftBars();
    // Full mode's whole point is maximum density -- zero gap between bars,
    // unlike Doubling/Fixed Bands which keep a visible 1-2px separation.
    bool isFullMode = (AudioEngine::instance().getSpectrumBandLayoutMode() == SpectrumPipeline::BandLayoutMode::FULL);

    // Geometry changed since the last frame (band count landing from core1
    // after a Band Layout change, or any layout setting)? Start clean: the
    // old bars sit at positions/widths the new layout never touches, so
    // they'd stay on the panel forever. Peaks are stored in pixels of the
    // layout they were measured in (side-by-side is twice as tall as
    // top-bottom), so they restart too; the smoothed levels are 0..1 and
    // carry over.
    LayoutKey key;
    key.bands    = static_cast<uint16_t>(totalBars);
    key.channel  = static_cast<uint8_t>(ds.getChannelMode());
    key.stereo   = static_cast<uint8_t>(ds.getStereoLayout());
    key.mirror   = mirrorLeft;
    key.fullMode = isFullMode;
    if (key != m_layout) {
        m_layout = key;
        Renderer::instance().clear();
        resetPrevHeights(m_left);
        resetPrevHeights(m_right);
        resetPrevHeights(m_mono);
        for (size_t i = 0; i < MAX_BAND_COUNT; ++i) {
            m_left[i].peak = m_right[i].peak = m_mono[i].peak = 0.0f;
        }
    }

    uint32_t now = millis();

    // --- Diagnostic: which bin is currently loudest, once/sec ---
    static uint32_t s_lastDebugMs    = 0;
    static float    s_debugMaxMag    = 0.0f;
    static size_t   s_debugMaxBarIdx = 0;
    static bool     s_debugMaxIsLeft = true;
    for (size_t i = 0; i < totalBars; ++i) {
        if (leftBars[i] > s_debugMaxMag)  { s_debugMaxMag = leftBars[i];  s_debugMaxBarIdx = i; s_debugMaxIsLeft = true; }
        if (rightBars[i] > s_debugMaxMag) { s_debugMaxMag = rightBars[i]; s_debugMaxBarIdx = i; s_debugMaxIsLeft = false; }
    }

    // Geometry for this frame.
    uint16_t gap, barWidth, maxBarHeight;
    size_t   columns; // physical columns drawn: 2 per band in stereo, 1 in mono
    if (stereo && sideBySide) {
        gap          = isFullMode ? 0 : 2;
        barWidth     = computeBarWidth(SCREEN_WIDTH, static_cast<uint16_t>(totalBars * 2), gap);
        maxBarHeight = SCREEN_HEIGHT - 10;
        columns      = totalBars * 2;
    } else if (stereo) { // top-bottom
        gap          = isFullMode ? 0 : 1;
        barWidth     = computeBarWidth(SCREEN_WIDTH, static_cast<uint16_t>(totalBars), gap);
        maxBarHeight = SCREEN_HEIGHT / 2 - 5;
        columns      = totalBars * 2;
    } else { // mono
        gap          = isFullMode ? 0 : 1;
        barWidth     = computeBarWidth(SCREEN_WIDTH, static_cast<uint16_t>(totalBars), gap);
        maxBarHeight = SCREEN_HEIGHT - 10;
        columns      = totalBars;
    }


    if (stereo && sideBySide) {
        // L and R groups side by side, both growing up from the bottom.
        constexpr uint16_t baseline = SCREEN_HEIGHT;
        for (size_t i = 0; i < totalBars; ++i) {
            size_t leftDrawIndex = mirrorLeft ? (totalBars - 1 - i) : i;
            uint16_t xL = gap + static_cast<uint16_t>(leftDrawIndex) * (barWidth + gap);
            uint16_t xR = gap + static_cast<uint16_t>(i + totalBars) * (barWidth + gap);
            processAndDrawBar(m_left[i],  i, totalBars, AudioLevel::dbNormalize(leftBars[i]),  xL, barWidth, maxBarHeight, baseline, true, now);
            processAndDrawBar(m_right[i], i, totalBars, AudioLevel::dbNormalize(rightBars[i]), xR, barWidth, maxBarHeight, baseline, true, now);
        }
    } else if (stereo) {
        // L grows up, R grows down, meeting at the centerline.
        constexpr uint16_t baseline = SCREEN_HEIGHT / 2;
        for (size_t i = 0; i < totalBars; ++i) {
            uint16_t x = gap + static_cast<uint16_t>(i) * (barWidth + gap);
            processAndDrawBar(m_left[i],  i, totalBars, AudioLevel::dbNormalize(leftBars[i]),  x, barWidth, maxBarHeight, baseline, true, now);
            processAndDrawBar(m_right[i], i, totalBars, AudioLevel::dbNormalize(rightBars[i]), x, barWidth, maxBarHeight, baseline, false, now);
        }
    } else {
        // Mono: same shape regardless of the Stereo Layout setting. Average,
        // not sum -- a mono-duplicated source reads like either channel alone.
        constexpr uint16_t baseline = SCREEN_HEIGHT;
        for (size_t i = 0; i < totalBars; ++i) {
            float monoMag = (leftBars[i] + rightBars[i]) * 0.5f;
            uint16_t x = gap + static_cast<uint16_t>(i) * (barWidth + gap);
            processAndDrawBar(m_mono[i], i, totalBars, AudioLevel::dbNormalize(monoMag), x, barWidth, maxBarHeight, baseline, true, now);
        }
    }

    uint32_t nowMs = millis();
    if (DebugFlags::audio && nowMs - s_lastDebugMs >= 1000) {
        s_lastDebugMs = nowMs;
        float db = (s_debugMaxMag > 0.0001f) ? 20.0f * log10f(s_debugMaxMag) : -100.0f;
        Console::log("Spectrum", "loudest bin -> band %d (%s) | raw mag=%s | dBFS=%s",
                     static_cast<int>(s_debugMaxBarIdx), s_debugMaxIsLeft ? "L" : "R",
                     Console::num(s_debugMaxMag, 4).s, Console::num(db, 2).s);
        s_debugMaxMag = 0.0f;
    }
}

// Coordinates: a bar of height h covers height indices 0..h-1 -- rows
// baseline-h .. baseline-1 when growing up, baseline .. baseline+h-1 when
// growing down. The peak marker sits at height index peakH, i.e. directly
// BEYOND the bar's last row (never on it) -- the same convention the
// Digital VU meter uses. Since the peak is never below the bar (BarPhysics),
// the bar's growth fill and shrink erase can then never touch a marker that
// didn't move, which is what lets the marker be redrawn only when it moves.
// (It used to sit ON the bar's top row when growing up: whenever the bar
// rose to exactly the held peak, or fell away from it, the bar update
// painted over or erased the marker and nothing redrew it.)
void SpectrumScreen::processAndDrawBar(BarPhysicsState& state, size_t i, size_t totalBars, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint16_t baseline, bool growUp, uint32_t now) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    BarPhysics::instance().updateState(state, val, maxBarHeight, now);

    uint16_t h     = static_cast<uint16_t>(state.decay * maxBarHeight);
    // A bar never goes fully dark: at silence each one keeps a 1 px floor in
    // its colour, so a quiet input reads as a dashed line of the theme's
    // colours rather than an empty screen. (The peak marker sits beyond the
    // bar's last row, so it's never drawn over the floor.)
    if (h < 1) h = 1;
    uint16_t peakH = static_cast<uint16_t>(state.peak);
    if (peakH > maxBarHeight) peakH = maxBarHeight; // defensive: stay inside the column

    if (growUp) {
        updateBarColumnUp(x, barWidth, state.prevUnits, h, maxBarHeight, baseline, i, totalBars);
    } else {
        updateBarColumnDown(x, barWidth, state.prevUnits, h, maxBarHeight, baseline, i, totalBars);
    }

    auto markerRow = [&](uint16_t index) -> int16_t {
        return growUp ? static_cast<int16_t>(baseline - index - 1) : static_cast<int16_t>(baseline + index);
    };

    if (DisplaySettings::instance().getPeaksEnabled() && peakH != state.prevPeakUnits) {
        uint16_t oldPeak = state.prevPeakUnits;
        // Erase the old marker unless the bar has grown over it (then its
        // fill already painted that row).
        if (oldPeak >= h && oldPeak > 0) {
            gfx->fillRect(x, markerRow(oldPeak), barWidth, 1, COLOR_BLACK);
        }
        if (peakH > 0) {
            gfx->fillRect(x, markerRow(peakH), barWidth, 1, COLOR_WHITE);
        }
    }

    state.prevUnits     = h;
    // Tracked even while peaks are disabled: re-enabling them goes through
    // the menu, and closing the menu runs onEnter(), which clears the panel
    // and resets prevPeakUnits -- so the marker is simply redrawn fresh.
    state.prevPeakUnits = peakH;

}

void SpectrumScreen::fillGradientRangeUp(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    ColorTheme& theme = ColorTheme::instance();
    GradientStyle style = theme.getStyle();

    if (style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t color = theme.colorForBarIndex(barIndex, totalBars);
        gfx->fillRect(x, baseline - hiHeight, barWidth, hiHeight - loHeight, color);
        return;
    }

    if (style == GradientStyle::SMOOTH) {
        uint16_t bandHeight = max(static_cast<uint16_t>(1), static_cast<uint16_t>(maxBarHeight / GRADIENT_BAND_COUNT));
        uint16_t bandStart = (loHeight / bandHeight) * bandHeight;
        for (uint16_t bandLo = bandStart; bandLo < hiHeight; bandLo += bandHeight) {
            uint16_t bandHi = min(static_cast<uint16_t>(bandLo + bandHeight), hiHeight);
            if (bandHi <= loHeight) continue;
            uint16_t clippedLo = max(bandLo, loHeight);
            float t = static_cast<float>(bandLo) / static_cast<float>(maxBarHeight);
            uint16_t color = theme.interpolatedColor(t);
            gfx->fillRect(x, baseline - bandHi, barWidth, bandHi - clippedLo, color);
        }
        return;
    }

    // FLAT_ZONES
    uint16_t yellowStart = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_YELLOW_THRESHOLD);
    uint16_t redStart    = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_RED_THRESHOLD);

    uint16_t greenLo = loHeight;
    uint16_t greenHi = min(hiHeight, yellowStart);
    if (greenHi > greenLo) gfx->fillRect(x, baseline - greenHi, barWidth, greenHi - greenLo, theme.lowColor());

    uint16_t yellowLo = max(loHeight, yellowStart);
    uint16_t yellowHi = min(hiHeight, redStart);
    if (yellowHi > yellowLo) gfx->fillRect(x, baseline - yellowHi, barWidth, yellowHi - yellowLo, theme.midColor());

    uint16_t redLo = max(loHeight, redStart);
    if (hiHeight > redLo) gfx->fillRect(x, baseline - hiHeight, barWidth, hiHeight - redLo, theme.highColor());
}

void SpectrumScreen::fillGradientRangeDown(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    ColorTheme& theme = ColorTheme::instance();
    GradientStyle style = theme.getStyle();

    if (style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t color = theme.colorForBarIndex(barIndex, totalBars);
        gfx->fillRect(x, baseline + loHeight, barWidth, hiHeight - loHeight, color);
        return;
    }

    if (style == GradientStyle::SMOOTH) {
        uint16_t bandHeight = max(static_cast<uint16_t>(1), static_cast<uint16_t>(maxBarHeight / GRADIENT_BAND_COUNT));
        uint16_t bandStart = (loHeight / bandHeight) * bandHeight;
        for (uint16_t bandLo = bandStart; bandLo < hiHeight; bandLo += bandHeight) {
            uint16_t bandHi = min(static_cast<uint16_t>(bandLo + bandHeight), hiHeight);
            if (bandHi <= loHeight) continue;
            uint16_t clippedLo = max(bandLo, loHeight);
            float t = static_cast<float>(bandLo) / static_cast<float>(maxBarHeight);
            uint16_t color = theme.interpolatedColor(t);
            gfx->fillRect(x, baseline + clippedLo, barWidth, bandHi - clippedLo, color);
        }
        return;
    }

    // FLAT_ZONES
    uint16_t yellowStart = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_YELLOW_THRESHOLD);
    uint16_t redStart    = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_RED_THRESHOLD);

    uint16_t greenLo = loHeight;
    uint16_t greenHi = min(hiHeight, yellowStart);
    if (greenHi > greenLo) gfx->fillRect(x, baseline + greenLo, barWidth, greenHi - greenLo, theme.lowColor());

    uint16_t yellowLo = max(loHeight, yellowStart);
    uint16_t yellowHi = min(hiHeight, redStart);
    if (yellowHi > yellowLo) gfx->fillRect(x, baseline + yellowLo, barWidth, yellowHi - yellowLo, theme.midColor());

    uint16_t redLo = max(loHeight, redStart);
    if (hiHeight > redLo) gfx->fillRect(x, baseline + redLo, barWidth, hiHeight - redLo, theme.highColor());
}

void SpectrumScreen::updateBarColumnUp(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRangeUp(x, barWidth, repaintFloor, newHeight, maxBarHeight, baseline, barIndex, totalBars);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, baseline - oldHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}

void SpectrumScreen::updateBarColumnDown(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRangeDown(x, barWidth, repaintFloor, newHeight, maxBarHeight, baseline, barIndex, totalBars);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, baseline + newHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}
