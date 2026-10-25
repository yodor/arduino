#include "InfoScreen.hpp"
#include "Renderer.hpp"
#include "ColorTheme.hpp"
#include "DaughterBoardLink.hpp"
#include "VolumeControl.hpp"
#include "DisplaySettings.hpp"
#include "RtcClock.hpp"
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
constexpr int16_t  kRY         = kValueY;           // Rtotal, right column, top...
constexpr int16_t  kStepY      = kValueY + 18;      // ...the step number under it (bottoms aligned with the dB)
constexpr int16_t  kBarY       = 88;
constexpr int16_t  kBarH       = 42;
constexpr int16_t  kBarGap     = 2;
constexpr int16_t  kMsgY       = 40;

constexpr uint16_t kColorUnlit    = 0x2104; // dark gray
constexpr uint16_t kColorMutedLit = 0x630C; // mid gray -- level shown, but muted
constexpr uint16_t kColorSubtle   = 0x8410; // gray
constexpr uint16_t kColorOff      = 0x4208; // dim gray -- a tag that's "off"
constexpr uint16_t kColorMatch    = 0x07FF; // cyan

// Header slots: start column (characters from the margin) and width.
struct Slot { uint8_t col, width; };
constexpr Slot kSlot[5] = {
    { 0, 4 },   // COMP
    { 5, 7 },   // L/R match, e.g. "+0.05dB"
    { 13, 6 },  // block temperature, e.g. "24.3C"
    { 20, 6 },  // alert: MUTE / RESYNC (alternating each second if both)
    { 27, 7 },  // time, right-aligned, e.g. "21:47" / "9:47PM"
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


// Middle, right column, top: the Rtotal the volume steps are solved for --
// "R=95K (auto)" -- above the step number.
void InfoScreen::drawRTag() {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();

    char text[24];
    uint32_t ohms = link.isStatusFresh() ? s.rOhms : 0;
    if (ohms == 0) snprintf(text, sizeof(text), "R=--");
    else snprintf(text, sizeof(text), "R=%luK (%s)", static_cast<unsigned long>((ohms + 500) / 1000), s.rAuto ? "auto" : "manual");
    if (!strcmp(text, m_drawnR)) return;
    strcpy(m_drawnR, text);

    Arduino_GFX* gfx = Renderer::instance().gfx();
    constexpr int16_t kSlotChars = 15;
    int16_t slotW = kSlotChars * kCharW * kRowSize;
    gfx->fillRect(SCREEN_WIDTH - kMargin - slotW, kRY, slotW, kCharH * kRowSize, COLOR_BLACK);
    gfx->setTextSize(kRowSize);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(SCREEN_WIDTH - kMargin - textWidth(text, kRowSize), kRY);
    gfx->print(text);
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

    // Time, right-aligned in the last slot (read from the module once a second).
    if (now - m_lastTimeReadMs >= 1000 || !m_drawnSlot[4][0]) {
        m_lastTimeReadMs = now;
        char text[10] = "--:--";
        RtcDateTime t;
        if (RtcClock::instance().read(t)) {
            if (DisplaySettings::instance().getClock24h()) {
                snprintf(text, sizeof(text), "%02u:%02u", t.hour, t.minute);
            } else {
                unsigned h = t.hour % 12 ? t.hour % 12 : 12;
                snprintf(text, sizeof(text), "%u:%02u%s", h, t.minute, t.hour >= 12 ? "PM" : "AM");
            }
        }
        char padded[12];
        snprintf(padded, sizeof(padded), "%*s", kSlot[4].width, text);
        drawSlot(4, slotX(4), kSlot[4].width, padded, COLOR_WHITE);
    }
}

void InfoScreen::drawValue(int16_t vol, int16_t volMax, int32_t dbTenths) {
    Arduino_GFX* gfx = Renderer::instance().gfx();

    // Clear the whole value row (left: dB, right: step) before reprinting --
    // the new text can be shorter than the old.
    gfx->fillRect(0, kValueY, SCREEN_WIDTH, kCharH * kValueSize, COLOR_BLACK);

    char db[12];
    formatTenths(db, sizeof(db), dbTenths);
    gfx->setTextSize(kValueSize);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(kMargin, kValueY);
    gfx->print(db);

    gfx->setTextSize(kUnitSize);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(kMargin + textWidth(db, kValueSize) + 8,
                   kValueY + kCharH * (kValueSize - kUnitSize)); // baseline-aligned with the big digits
    gfx->print("dB");

    // Step number, 0-based exactly as the daughter numbers it, under Rtotal.
    char step[12];
    snprintf(step, sizeof(step), "%d/%d", static_cast<int>(vol), static_cast<int>(volMax));
    gfx->setTextSize(kStepSize);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(SCREEN_WIDTH - kMargin - textWidth(step, kStepSize), kStepY);
    gfx->print(step);
    m_drawnR[0] = '\0'; // the row was cleared: Rtotal is redrawn after this
}

// One block per step, VOLMIN..VOLMAX, lit up to the current step in the
// active color theme's gradient (gray while muted). Only blocks whose state
// changed are redrawn, unless the layout or mute state changed.
void InfoScreen::drawBar(int16_t vol, int16_t volMin, int16_t volMax, bool muted) {
    Arduino_GFX* gfx = Renderer::instance().gfx();

    int16_t count = volMax - volMin + 1;
    if (count <= 0) return;
    int16_t blockW = (SCREEN_WIDTH - 2 * kMargin - (count - 1) * kBarGap) / count;
    if (blockW < 1) blockW = 1;
    int16_t totalW = count * blockW + (count - 1) * kBarGap;
    int16_t x0     = (SCREEN_WIDTH - totalW) / 2;

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
        m_drawnR[0]     = '\0';
        for (uint8_t i = 0; i < kSlots; ++i) { m_drawnSlot[i][0] = '\0'; m_drawnSlotColor[i] = 0; }
    }

    drawTags();

    if (v != View::VOLUME) return;

    const DaughterStatus& s = DaughterBoardLink::instance().status();
    int32_t dbTenths = static_cast<int32_t>(lroundf(s.db * 10.0f));

    if (dbTenths != m_drawnDbTenths || s.vol != m_drawnVol || s.volMax != m_drawnVolMax) {
        drawValue(s.vol, s.volMax, dbTenths);
        m_drawnDbTenths = dbTenths;
        m_drawnVol      = s.vol;
    }

    drawRTag();

    // drawBar() is itself a delta draw, so it's safe to call every frame.
    drawBar(s.vol, s.volMin, s.volMax, muted);
}
