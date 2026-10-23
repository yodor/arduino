#include "VolumeOverlay.hpp"
#include "Renderer.hpp"
#include "ColorTheme.hpp"
#include "DaughterBoardLink.hpp"
#include "VolumeControl.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
// Default GFX font: 6x8 px per character cell at size 1.
constexpr int16_t kCharW = 6;
constexpr int16_t kCharH = 8;

constexpr int16_t  kMargin       = 12;
constexpr int16_t  kHeaderY      = 8;
constexpr uint8_t  kHeaderSize   = 2;
constexpr int16_t  kValueY       = 38;
constexpr uint8_t  kValueSize    = 5;   // the big dB number
constexpr uint8_t  kUnitSize     = 3;
constexpr uint8_t  kStepSize     = 3;
constexpr int16_t  kBarY         = 96;
constexpr int16_t  kBarH         = 30;
constexpr int16_t  kBarGap       = 2;
constexpr int16_t  kMsgY         = 50;

constexpr uint16_t kColorUnlit      = 0x2104; // dark gray
constexpr uint16_t kColorMutedLit   = 0x630C; // mid gray -- level shown, but muted
constexpr uint16_t kColorHeader     = COLOR_WHITE;
constexpr uint16_t kColorSubtle     = 0x8410; // gray

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

void VolumeOverlay::poke() {
    m_active     = true;
    m_lastPokeMs = millis();
}

void VolumeOverlay::update() {
    const DaughterBoardLink& link = DaughterBoardLink::instance();

    if (link.statusCount() != m_lastStatusCount) {
        m_lastStatusCount = link.statusCount();
        const DaughterStatus& s = link.status();
        if (!s.busy) {
            // Show the overlay for changes made anywhere (the daughter's own
            // console, a calibration landing on a new step...) -- but not
            // for the very first status after boot, which is just adoption.
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

VolumeOverlay::View VolumeOverlay::currentView() const {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    if (!link.isStatusFresh()) return View::NO_LINK;
    const DaughterStatus& s = link.status();
    if (s.busy) {
        // The power-up cell check is a (short) calibration run too.
        const DaughterProgress& pr = link.progress();
        return (pr.valid && !strcmp(pr.phase, "CHECK")) ? View::CHECKING : View::CALIBRATING;
    }
    // Revision 10: until CAL=OK the daughter keeps the volume muted and
    // refuses VOL, whatever VOL= says.
    if (s.cal != DaughterCalState::OK) return View::NOT_CALIBRATED;
    if (s.vol < 0 || s.volMin < 0 || s.volMax < s.volMin) return View::NOT_CALIBRATED;
    return View::VOLUME;
}

void VolumeOverlay::onEnter() {
    m_fullRedraw = true;
}

void VolumeOverlay::drawCentered(const char* text, uint8_t size, int16_t y, uint16_t color) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    int16_t x = (SCREEN_WIDTH - textWidth(text, size)) / 2;
    if (x < 0) x = 0;
    gfx->setTextSize(size);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor(x, y);
    gfx->print(text);
}

void VolumeOverlay::drawStaticForView(View v) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);

    gfx->setTextSize(kHeaderSize);
    gfx->setTextColor(kColorHeader, COLOR_BLACK);
    gfx->setCursor(kMargin, kHeaderY);
    gfx->print("VOLUME");

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
            // Revision 10 says why: nothing stored, other cells, or a
            // re-solve after a settings change (quick).
            const char* title = (s.cal == DaughterCalState::NONE)    ? "NO CALIBRATION"
                              : (s.cal == DaughterCalState::CHANGED) ? "CELLS CHANGED"
                              : (s.cal == DaughterCalState::STALE)   ? "RE-SOLVE NEEDED"
                              : (s.cal == DaughterCalState::ERR)     ? "CALIBRATION ERROR"
                                                                     : "NOT CALIBRATED";
            drawCentered(title, 4, kMsgY, COLOR_RED);
            if (s.cal == DaughterCalState::ERR && s.hasErr) {
                // Free text from the daughter -- size 1, wraps if long.
                gfx->setTextSize(1);
                gfx->setTextColor(kColorSubtle, COLOR_BLACK);
                gfx->setCursor(kMargin, kMsgY + 44);
                gfx->print(s.err);
            } else {
                drawCentered("Settings > Calibration > Start", 2, kMsgY + 44, kColorSubtle);
            }
            break;
        }
        case View::VOLUME:
        case View::NONE:
            break; // value + bar drawn by render()
    }
}

// "MUTED" right after the "VOLUME" title (the top-right corner shows Rtotal).
void VolumeOverlay::drawMuteTag(bool muted) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    const char* tag = "MUTED";
    int16_t w = textWidth(tag, kHeaderSize);
    int16_t x = kMargin + textWidth("VOLUME", kHeaderSize) + 16;
    gfx->fillRect(x, kHeaderY, w, kCharH * kHeaderSize, COLOR_BLACK);
    if (muted) {
        gfx->setTextSize(kHeaderSize);
        gfx->setTextColor(COLOR_RED, COLOR_BLACK);
        gfx->setCursor(x, kHeaderY);
        gfx->print(tag);
    }
}

// Top-right: the Rtotal the volume steps are solved for, and whether the
// daughter picked it (auto) or it was set (manual) -- e.g. "R=650K (auto)".
// Kept as last shown while a calibration runs (the field is transient then).
void VolumeOverlay::drawRTag() {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();
    if (s.busy) return;
    uint32_t ohms = link.isStatusFresh() ? s.rOhms : 0;
    if (ohms == m_drawnROhms && static_cast<int8_t>(s.rAuto) == m_drawnRAuto) return;

    char text[24];
    if (ohms == 0) snprintf(text, sizeof(text), "R=--");
    else snprintf(text, sizeof(text), "R=%luK (%s)", static_cast<unsigned long>((ohms + 500) / 1000), s.rAuto ? "auto" : "manual");

    Arduino_GFX* gfx = Renderer::instance().gfx();
    constexpr int16_t kSlotChars = 16;
    int16_t slotW = kSlotChars * kCharW * kHeaderSize;
    gfx->fillRect(SCREEN_WIDTH - kMargin - slotW, kHeaderY, slotW, kCharH * kHeaderSize, COLOR_BLACK);
    gfx->setTextSize(kHeaderSize);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(SCREEN_WIDTH - kMargin - textWidth(text, kHeaderSize), kHeaderY);
    gfx->print(text);
    m_drawnROhms = ohms;
    m_drawnRAuto = static_cast<int8_t>(s.rAuto);
}

void VolumeOverlay::drawValue(int16_t vol, int16_t volMax, int32_t dbTenths) {
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

    // Step number, 0-based exactly as the daughter numbers it.
    char step[12];
    snprintf(step, sizeof(step), "%d/%d", static_cast<int>(vol), static_cast<int>(volMax));
    gfx->setTextSize(kStepSize);
    gfx->setTextColor(kColorSubtle, COLOR_BLACK);
    gfx->setCursor(SCREEN_WIDTH - kMargin - textWidth(step, kStepSize),
                   kValueY + kCharH * (kValueSize - kStepSize));
    gfx->print(step);
}

// One block per step, VOLMIN..VOLMAX, lit up to the current step in the
// active color theme's gradient (gray while muted). Only blocks whose state
// changed are redrawn, unless the layout or mute state changed.
void VolumeOverlay::drawBar(int16_t vol, int16_t volMin, int16_t volMax, bool muted) {
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

void VolumeOverlay::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    View v     = currentView();
    bool muted = VolumeControl::instance().isMuted();

    if (m_fullRedraw || v != m_drawnView) {
        drawStaticForView(v);
        m_drawnView     = v;
        m_fullRedraw    = false;
        m_drawnMuted    = -1;
        m_drawnDbTenths = INT32_MIN;
        m_drawnVol      = -2;
        m_drawnVolMin   = -2;
        m_drawnVolMax   = -2;
        m_drawnLit      = -1;
        m_drawnROhms    = 0xFFFFFFFF;
        m_drawnRAuto    = -1;
    }

    if (static_cast<int8_t>(muted) != m_drawnMuted) {
        drawMuteTag(muted);
        m_drawnMuted = static_cast<int8_t>(muted);
    }
    drawRTag();

    if (v != View::VOLUME) return;

    const DaughterStatus& s = DaughterBoardLink::instance().status();
    int32_t dbTenths = static_cast<int32_t>(lroundf(s.db * 10.0f));

    if (dbTenths != m_drawnDbTenths || s.vol != m_drawnVol || s.volMax != m_drawnVolMax) {
        drawValue(s.vol, s.volMax, dbTenths);
        m_drawnDbTenths = dbTenths;
        m_drawnVol      = s.vol;
    }

    // drawBar() is itself a delta draw (nothing touched when the lit count,
    // range and mute state are unchanged), so it's safe to call every frame.
    drawBar(s.vol, s.volMin, s.volMax, muted);
}
