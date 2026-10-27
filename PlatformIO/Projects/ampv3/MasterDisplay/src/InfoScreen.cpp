#include "InfoScreen.hpp"
#include "Renderer.hpp"
#include "ColorTheme.hpp"
#include "DaughterBoardLink.hpp"
#include "VolumeControl.hpp"
#include "DisplaySettings.hpp"
#include "BeatDetector.hpp"
#include "AudioEngine.hpp"
#include "AudioLevel.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
// Default GFX font: 6x8 px per character cell at size 1.
constexpr int16_t kCharW = 6;
constexpr int16_t kCharH = 8;

// Three areas: the header row, the middle (big dB value on the left; Rtotal
// above the step number on the right), the volume bar at the bottom.
constexpr int16_t  kMargin     = 7;    // header: (428 - 2*7) / 12 = 34 characters
constexpr uint8_t  kRowSize    = 2;    // header and Rtotal: 12x16 px characters
constexpr int16_t  kHeaderY    = 4;
constexpr int16_t  kValueY     = 32;
constexpr uint8_t  kValueSize  = 5;    // the big dB number
constexpr uint8_t  kUnitSize   = 3;
constexpr uint8_t  kStepSize   = 3;
constexpr uint8_t  kRightSize  = 3;                 // the middle-right readout (BPM)
constexpr int16_t  kRightY     = kValueY + 8;       // bottom-aligned with the big dB digits
constexpr int16_t  kBarY       = 88;
constexpr int16_t  kBarH       = 42;
constexpr int16_t  kBarGap     = 2;
constexpr int16_t  kMsgY       = 40;

constexpr uint16_t kColorUnlit    = 0x2104; // dark gray
constexpr uint16_t kColorMutedLit = 0x630C; // mid gray -- level shown, but muted
constexpr uint16_t kColorSubtle   = 0x8410; // gray
constexpr uint16_t kColorOff      = 0x4208; // dim gray -- a tag that's "off"
constexpr uint16_t kColorMatch    = 0x07FF; // cyan
constexpr uint16_t kColorRtotal   = 0xC618; // light gray

// Header slots: start column (characters from the margin) and width.
struct Slot { uint8_t col, width; };
constexpr Slot kSlot[5] = {
    { 0, 4 },   // COMP
    { 5, 7 },   // L/R match, e.g. "+0.05dB"
    { 13, 6 },  // block temperature, e.g. "24.3C"
    { 20, 6 },  // alert: MUTE / RESYNC (alternating each second if both)
    { 27, 7 },  // Rtotal value, right-aligned, e.g. "95K" / "1431K"
};

int16_t textWidth(const char* text, uint8_t size) {
    return static_cast<int16_t>(strlen(text)) * kCharW * size;
}

// "%.1f" without relying on printf float support.
void formatTenths(char* out, size_t outSize, int32_t tenths) {
    const char* sign = (tenths < 0) ? "-" : "";
    int32_t a = (tenths < 0) ? -tenths : tenths;
    snprintf(out, outSize, "%s%ld.%ld", sign, static_cast<long>(a / 10), static_cast<long>(a % 10));
}
} // namespace

// --- As the popup ---------------------------------------------------------------

void InfoScreen::poke() {
    m_active     = true;
    m_lastPokeMs = millis();
}

void InfoScreen::update() {
    const DaughterBoardLink& link = DaughterBoardLink::instance();

    if (link.statusCount() != m_lastStatusCount) {
        m_lastStatusCount = link.statusCount();
        const DaughterStatus& s = link.status();
        if (!s.busy) {
            // Pop up for changes made anywhere (the daughter's console, a
            // calibration landing on a new step...) -- but not for the very
            // first status after boot, which is just adoption.
            if (m_haveBaseline && (s.vol != m_baseVol || s.mute != m_baseMute)) poke();
            m_baseVol      = s.vol;
            m_baseMute     = s.mute;
            m_haveBaseline = true;
        }
    }

    if (m_active && (millis() - m_lastPokeMs > VOLUME_OVERLAY_TIMEOUT_MS)) {
        m_active = false;
    }
}

// --- Drawing --------------------------------------------------------------------

InfoScreen::View InfoScreen::currentView() const {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    if (!link.isStatusFresh()) return View::NO_LINK;
    const DaughterStatus& s = link.status();
    if (s.busy) {
        // The power-up cell check is a (short) calibration run too.
        const DaughterProgress& pr = link.progress();
        return (pr.valid && !strcmp(pr.phase, "CHECK")) ? View::CHECKING : View::CALIBRATING;
    }
    // Until CAL=OK the daughter keeps the volume muted and refuses VOL.
    if (s.cal != DaughterCalState::OK) return View::NOT_CALIBRATED;
    if (s.vol < 0 || s.volMin < 0 || s.volMax < s.volMin) return View::NOT_CALIBRATED;
    return View::VOLUME;
}

void InfoScreen::onEnter() {
    m_fullRedraw = true;
}

void InfoScreen::drawCentered(const char* text, uint8_t size, int16_t y, uint16_t color) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    int16_t x = (SCREEN_WIDTH - textWidth(text, size)) / 2;
    if (x < 0) x = 0;
    gfx->setTextSize(size);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor(x, y);
    gfx->print(text);
}

void InfoScreen::drawStaticForView(View v) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);
    const DaughterStatus& s = DaughterBoardLink::instance().status();

    switch (v) {
        case View::NO_LINK:
            drawCentered("NO LINK", 4, kMsgY, COLOR_RED);
            drawCentered("volume board not answering", 2, kMsgY + 44, kColorSubtle);
            break;
        case View::CHECKING:
            drawCentered("CHECKING CELLS", 4, kMsgY, COLOR_YELLOW);
            drawCentered("volume board starting up", 2, kMsgY + 44, kColorSubtle);
            break;
        case View::CALIBRATING:
            drawCentered("CALIBRATING", 4, kMsgY, COLOR_YELLOW);
            drawCentered("volume locked until it finishes", 2, kMsgY + 44, kColorSubtle);
            break;
        case View::NOT_CALIBRATED: {
            const char* title = (s.cal == DaughterCalState::NONE)    ? "NO CALIBRATION"
                              : (s.cal == DaughterCalState::CHANGED) ? "CELLS CHANGED"
                              : (s.cal == DaughterCalState::STALE)   ? "RE-SOLVE NEEDED"
                              : (s.cal == DaughterCalState::ERR)     ? "CALIBRATION ERROR"
                                                                     : "NOT CALIBRATED";
            drawCentered(title, 4, kMsgY, COLOR_RED);
            if (s.cal == DaughterCalState::ERR && s.hasErr) {
                gfx->setTextSize(1);
                gfx->setTextColor(kColorSubtle, COLOR_BLACK);
                gfx->setCursor(kMargin, kMsgY + 44);
                gfx->print(s.err);
            } else {
                drawCentered("Settings > Calibration", 2, kMsgY + 44, kColorSubtle);
            }
            break;
        }
        case View::VOLUME:
        case View::NONE:
            break; // value + bar drawn by render()
    }
}


// Middle, right: the view stepped with the Options key -- the detected tempo
// ("124 BPM", "--- BPM" with no usable beat), the small scope or the small
// spectrum.
void InfoScreen::drawRightInfo() {
    if (m_rightInfo == RightInfo::SCOPE || m_rightInfo == RightInfo::SPECTRUM) {
        const char* tag = (m_rightInfo == RightInfo::SCOPE) ? "\x01scope" : "\x01spectrum";
        if (strcmp(m_drawnRight, tag)) {
            // Coming from another view: clear the area once, then the
            // graphic keeps it up to date itself.
            strcpy(m_drawnRight, tag);
            constexpr int16_t kAreaW = 15 * kCharW * kRowSize;
            Renderer::instance().gfx()->fillRect(SCREEN_WIDTH - kMargin - kAreaW, kValueY, kAreaW, kCharH * kValueSize, COLOR_BLACK);
            m_scopeDrawn = false;
            m_miniDrawn  = false;
        }
        if (m_rightInfo == RightInfo::SCOPE) drawScope();
        else                                 drawMiniSpectrum();
        return;
    }

    char text[24];
    const uint8_t size = kRightSize;
    float bpm = BeatDetector::instance().bpm();
    if (bpm > 0.0f) snprintf(text, sizeof(text), "%d BPM", static_cast<int>(lroundf(bpm)));
    else            snprintf(text, sizeof(text), "--- BPM");
    if (!strcmp(text, m_drawnRight)) return;
    strcpy(m_drawnRight, text);

    // The readout's area: from the end of the widest dB text to the margin.
    Arduino_GFX* gfx = Renderer::instance().gfx();
    constexpr int16_t kAreaW = 15 * kCharW * kRowSize; // 180 px
    gfx->fillRect(SCREEN_WIDTH - kMargin - kAreaW, kValueY, kAreaW, kCharH * kValueSize, COLOR_BLACK);
    gfx->setTextSize(size);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(SCREEN_WIDTH - kMargin - textWidth(text, size), kValueY + kCharH * (kValueSize - size)); // bottom-aligned with the dB
    gfx->print(text);
}

// The small scope: a triggered mono (L+R) trace of ~9 ms -- the
// oscilloscope timescale whatever the Wave screen's window setting -- with
// automatic gain, in a fixed gray (kColorMutedLit). Each column is a
// vertical stroke from one point to the next; a column only erases the part
// of its old stroke the new one doesn't cover, so the trace never flickers.
void InfoScreen::drawScope() {
    uint32_t now = millis();
    if (m_scopeDrawn && now - m_scopeLastMs < 25) return; // ~40 fps
    m_scopeLastMs = now;

    constexpr int16_t kH  = kCharH * kValueSize;                // 40 px: the height of the big dB digits
    constexpr int16_t kX0 = SCREEN_WIDTH - kMargin - kScopeW;
    constexpr int16_t kY0 = kValueY;
    constexpr size_t  kSpanSamples = 384;                       // ~8.7 ms at 44.1 kHz
    Arduino_GFX* gfx = Renderer::instance().gfx();

    int16_t v[kScopeW + 1];
    AudioEngine::instance().getScopeMono(v, kScopeW + 1, kSpanSamples);

    // Auto-gain: follow the peak up at once, let it fall slowly; never
    // below 48 counts full scale, so idle noise stays a flat line.
    float peak = 0.0f;
    for (int16_t i = 0; i <= kScopeW; ++i) { float a = fabsf(static_cast<float>(v[i])); if (a > peak) peak = a; }
    m_scopePeak = (peak > m_scopePeak) ? peak : m_scopePeak * 0.97f;
    float fullScale = (m_scopePeak > 48.0f) ? m_scopePeak : 48.0f;
    float half = (kH - 2) / 2.0f;
    auto yOf = [&](int16_t s) {
        int16_t y = static_cast<int16_t>(lroundf(kY0 + kH / 2.0f - s * half / fullScale));
        return y < kY0 ? kY0 : (y > kY0 + kH - 1 ? static_cast<int16_t>(kY0 + kH - 1) : y);
    };

    bool full = !m_scopeDrawn;
    if (full) gfx->fillRect(kX0, kY0, kScopeW, kH, COLOR_BLACK);

    int16_t yPrev = yOf(v[0]);
    for (int16_t x = 0; x < kScopeW; ++x) {
        int16_t yNext = yOf(v[x + 1]);
        int16_t lo = yPrev < yNext ? yPrev : yNext;
        int16_t hi = yPrev < yNext ? yNext : yPrev;
        if (!full) {
            int16_t oLo = m_scopeLo[x], oHi = m_scopeHi[x];
            if (oLo < lo) gfx->fillRect(kX0 + x, oLo, 1, (oHi < lo ? oHi + 1 : lo) - oLo, COLOR_BLACK);
            if (oHi > hi) { int16_t from = (oLo > hi ? oLo : hi + 1); gfx->fillRect(kX0 + x, from, 1, oHi - from + 1, COLOR_BLACK); }
        }
        gfx->fillRect(kX0 + x, lo, 1, hi - lo + 1, kColorMutedLit);
        m_scopeLo[x] = lo;
        m_scopeHi[x] = hi;
        yPrev = yNext;
    }
    m_scopeDrawn = true;
}

// The small spectrum: mono doubling bands (10 bars, low to high), the same
// level mapping as the Spectrum screen, a gentle fall, fixed gray. Each bar
// only grows (gray) or shrinks (black) by the rows that changed.
void InfoScreen::drawMiniSpectrum() {
    uint32_t now = millis();
    if (m_miniDrawn && now - m_miniLastMs < 30) return; // ~33 fps, about the FFT rate
    float dt = m_miniDrawn ? (now - m_miniLastMs) / 1000.0f : 0.0f;
    m_miniLastMs = now;

    constexpr int16_t kH   = kCharH * kValueSize;              // 40 px
    constexpr int16_t kX0  = SCREEN_WIDTH - kMargin - kScopeW;
    constexpr int16_t kY0  = kValueY;
    constexpr int16_t kGap = 2;
    constexpr int16_t kBarW = (kScopeW - (kMiniBars - 1) * kGap) / kMiniBars; // 15 px
    constexpr float   kFallPerSec = 1.6f;                      // full height in ~0.6 s
    Arduino_GFX* gfx = Renderer::instance().gfx();

    float bars[16] = {};
    size_t n = AudioEngine::instance().getMiniBars(bars);
    if (n > kMiniBars) n = kMiniBars;

    if (!m_miniDrawn) {
        gfx->fillRect(kX0, kY0, kScopeW, kH, COLOR_BLACK);
        for (int i = 0; i < kMiniBars; ++i) { m_miniDrawnH[i] = 0; m_miniLevel[i] = 0.0f; }
    }
    for (int i = 0; i < kMiniBars; ++i) {
        float target = (static_cast<size_t>(i) < n) ? AudioLevel::dbNormalize(bars[i]) : 0.0f;
        float fallen = m_miniLevel[i] - kFallPerSec * dt;
        m_miniLevel[i] = (target > fallen) ? target : (fallen > 0.0f ? fallen : 0.0f);
        int16_t h = static_cast<int16_t>(lroundf(m_miniLevel[i] * kH));
        int16_t old = m_miniDrawnH[i];
        int16_t x = kX0 + i * (kBarW + kGap);
        if (h > old)      gfx->fillRect(x, kY0 + kH - h, kBarW, h - old, kColorMutedLit);
        else if (h < old) gfx->fillRect(x, kY0 + kH - old, kBarW, old - h, COLOR_BLACK);
        m_miniDrawnH[i] = h;
    }
    m_miniDrawn = true;
}

void InfoScreen::drawSlot(uint8_t slot, int16_t x, uint8_t widthChars, const char* text, uint16_t color) {
    if (!strcmp(text, m_drawnSlot[slot]) && color == m_drawnSlotColor[slot]) return;
    snprintf(m_drawnSlot[slot], sizeof(m_drawnSlot[slot]), "%s", text);
    m_drawnSlotColor[slot] = color;
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillRect(x, kHeaderY, widthChars * kCharW * kRowSize, kCharH * kRowSize, COLOR_BLACK);
    if (!text[0]) return;
    gfx->setTextSize(kRowSize);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor(x, kHeaderY);
    gfx->print(text);
}

// The header row: the status tags and the time, each in its own slot.
void InfoScreen::drawTags() {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterEnv&       e    = link.env();
    bool fresh = link.isStatusFresh();
    uint32_t now = millis();

    auto slotX = [](uint8_t i) { return static_cast<int16_t>(kMargin + kSlot[i].col * kCharW * kRowSize); };

    int8_t comp = fresh ? VolumeControl::instance().compensationState() : -1;
    drawSlot(0, slotX(0), kSlot[0].width, comp >= 0 ? "COMP" : "", comp == 1 ? COLOR_GREEN : kColorOff);

    char match[12] = "";
    if (fresh && e.valid && strcmp(e.match, "--")) snprintf(match, sizeof(match), "%sdB", e.match);
    drawSlot(1, slotX(1), kSlot[1].width, match, kColorMatch);

    char temp[12] = "";
    uint16_t tempColor = COLOR_WHITE;
    if (fresh && e.valid) {
        if (!e.ntcOk())                 { snprintf(temp, sizeof(temp), "NTC!"); tempColor = COLOR_RED; }
        else if (strcmp(e.block, "--")) {
            // One decimal: "24.31" -> "24.3C".
            float c = static_cast<float>(atof(e.block));
            char t[10];
            formatTenths(t, sizeof(t), static_cast<int32_t>(lroundf(c * 10.0f)));
            snprintf(temp, sizeof(temp), "%sC", t);
        }
    }
    drawSlot(2, slotX(2), kSlot[2].width, temp, tempColor);

    // Alert slot: MUTE (red) and RESYNC (yellow) share it, alternating each
    // second when both apply.
    bool muted  = VolumeControl::instance().isMuted();
    bool resync = fresh && e.valid && e.resync == 1;
    bool showMute = muted && (!resync || (now / 1000) % 2 == 0);
    if (showMute)    drawSlot(3, slotX(3), kSlot[3].width, "MUTE",   COLOR_RED);
    else if (resync) drawSlot(3, slotX(3), kSlot[3].width, "RESYNC", COLOR_YELLOW);
    else             drawSlot(3, slotX(3), kSlot[3].width, "",       COLOR_BLACK);

    // Rtotal value, right-aligned in the last slot: "95K" (auto/manual and
    // the "R=" form are in the middle-right readout, one Options press away).
    const DaughterStatus& st = link.status();
    char rv[12] = "";
    if (fresh && st.rOhms > 0) snprintf(rv, sizeof(rv), "%luK", static_cast<unsigned long>((st.rOhms + 500) / 1000));
    char padded[12];
    snprintf(padded, sizeof(padded), "%*s", kSlot[4].width, rv);
    drawSlot(4, slotX(4), kSlot[4].width, padded, kColorRtotal);
}

// The big value on the left: "-23.5 dB", or -- Settings -> Volume Display =
// Steps -- "14/31": the step (0-based, as the daughter numbers it) in white,
// the last step in gray, like the dB unit.
void InfoScreen::drawValue(int16_t vol, int16_t volMax, int32_t dbTenths) {
    Arduino_GFX* gfx = Renderer::instance().gfx();

    // Clear the value part of the row before reprinting -- the new text can be
    // shorter than the old. (The right part is the Options view.)
    gfx->fillRect(0, kValueY, SCREEN_WIDTH - kMargin - 15 * kCharW * kRowSize, kCharH * kValueSize, COLOR_BLACK);

    char big[12], unit[12];
    if (DisplaySettings::instance().getVolumeDisplay() == VolumeDisplay::STEPS) {
        snprintf(big,  sizeof(big),  "%d", static_cast<int>(vol));
        snprintf(unit, sizeof(unit), "/%d", static_cast<int>(volMax));
    } else {
        formatTenths(big, sizeof(big), dbTenths);
        snprintf(unit, sizeof(unit), "dB");
    }
    gfx->setTextSize(kValueSize);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(kMargin, kValueY);
    gfx->print(big);

    gfx->setTextSize(kUnitSize);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(kMargin + textWidth(big, kValueSize) + 8,
                   kValueY + kCharH * (kValueSize - kUnitSize)); // bottom-aligned with the big digits
    gfx->print(unit);
}

// One block per step, VOLMIN..VOLMAX, lit up to the current step in the
// active color theme's gradient (gray while muted). Only blocks whose state
// changed are redrawn, unless the layout or mute state changed.
void InfoScreen::drawBar(int16_t vol, int16_t volMin, int16_t volMax, bool muted) {
    Arduino_GFX* gfx = Renderer::instance().gfx();

    int16_t count = volMax - volMin + 1;
    if (count <= 0) return;
    // The bar spans the width between the margins.
    int16_t areaW  = SCREEN_WIDTH - 2 * kMargin;
    int16_t blockW = (areaW - (count - 1) * kBarGap) / count;
    if (blockW < 1) blockW = 1;
    int16_t totalW = count * blockW + (count - 1) * kBarGap;
    int16_t x0     = kMargin + (areaW - totalW) / 2;

    int16_t lit = vol - volMin + 1; // the quietest step still lights one block
    if (lit < 0) lit = 0;
    if (lit > count) lit = count;

    bool layoutChanged = (volMin != m_drawnVolMin) || (volMax != m_drawnVolMax) ||
                         (muted != m_drawnBarMuted) || (m_drawnLit < 0);
    int16_t from = 0, to = count;
    if (!layoutChanged) {
        from = (lit < m_drawnLit) ? lit : m_drawnLit;
        to   = (lit < m_drawnLit) ? m_drawnLit : lit;
    }

    ColorTheme& theme = ColorTheme::instance();
    for (int16_t i = from; i < to; ++i) {
        uint16_t color;
        if (i >= lit)   color = kColorUnlit;
        else if (muted) color = kColorMutedLit;
        else            color = theme.interpolatedColor(count > 1 ? static_cast<float>(i) / (count - 1) : 1.0f);
        gfx->fillRect(x0 + i * (blockW + kBarGap), kBarY, blockW, kBarH, color);
    }

    m_drawnLit      = lit;
    m_drawnVolMin   = volMin;
    m_drawnVolMax   = volMax;
    m_drawnBarMuted = muted;
}

void InfoScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    View v     = currentView();
    bool muted = VolumeControl::instance().isMuted();

    if (m_fullRedraw || v != m_drawnView) {
        drawStaticForView(v);
        m_drawnView     = v;
        m_fullRedraw    = false;
        m_drawnDbTenths = INT32_MIN;
        m_drawnVol      = -2;
        m_drawnVolMin   = -2;
        m_drawnVolMax   = -2;
        m_drawnLit      = -1;
        m_drawnRight[0] = '\0';
        m_scopeDrawn    = false;
        m_miniDrawn     = false;
        for (uint8_t i = 0; i < kSlots; ++i) { m_drawnSlot[i][0] = '\0'; m_drawnSlotColor[i] = 0; }
    }

    drawTags();

    if (v != View::VOLUME) return;

    const DaughterStatus& s = DaughterBoardLink::instance().status();
    int32_t dbTenths = static_cast<int32_t>(lroundf(s.db * 10.0f));

    VolumeDisplay mode = DisplaySettings::instance().getVolumeDisplay();
    if (dbTenths != m_drawnDbTenths || s.vol != m_drawnVol || s.volMax != m_drawnVolMax || mode != m_drawnVolDisplay) {
        drawValue(s.vol, s.volMax, dbTenths);
        m_drawnDbTenths    = dbTenths;
        m_drawnVol         = s.vol;
        m_drawnVolDisplay  = mode;
    }

    drawRightInfo();

    // drawBar() is itself a delta draw, so it's safe to call every frame.
    drawBar(s.vol, s.volMin, s.volMax, muted);
}
