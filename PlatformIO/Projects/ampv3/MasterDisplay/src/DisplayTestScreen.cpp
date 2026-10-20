#include "DisplayTestScreen.hpp"
#include "Renderer.hpp"
#include "PanelDriver.hpp"
#include <cstdio>
#include <cstring>

bool DisplayTestScreen::s_startRequested = false;

namespace {
const char* const kTestNames[] = {
    "1 PEAK WALK", "2 WALK, CS HELD", "3 SAME SPOT", "4 INVERTED", "5 DATA PATTERN", "6 ALIGNMENT", "7 THIN ROWS", "8 TINY WINDOWS"
};
const char* const kTestHints[] = {
    "trails = lost erases (CS toggles per fill)",
    "CS stays low all frame - only DC toggles",
    "no window commands - few DC switches",
    "black on white - color dependent?",
    "specks/wrong colors = bit errors",
    "which rows A-H leave trails?",
    "1px-tall markers walking up",
    "A 2x1 B 2x1 odd-y C 2x2 D 4x1: dots left?",
};

// Test 6 rows: start parity and size of every fill, per row.
//   x = horizontal start (even/odd/any), w = width, y = vertical start, h = height
struct AlignRow { char label; int8_t xParity; int16_t w; bool yOdd; int16_t h; };
//                       label xParity(0 even,1 odd,-1 any) w  yOdd   h
const AlignRow kAlignRows[] = {
    { 'A',  0, 2, false, 14 },  // fully even-aligned: even x, even width, even y, even height
    { 'B',  0, 2, true,  14 },  // odd y start
    { 'C',  1, 2, false, 14 },  // odd x start
    { 'D',  1, 2, true,  14 },  // odd x and odd y
    { 'E', -1, 1, false, 14 },  // 1px wide, moving 1px (like test 1), even y
    { 'F', -1, 1, true,  14 },  // same, odd y
    { 'G',  0, 2, false, 13 },  // aligned start, odd height
    { 'H', -1, 1, false, 13 },  // 1px wide, odd height
};
constexpr int16_t kAlignLeft = 16; // labels live left of this

} // namespace

void DisplayTestScreen::onEnter() {
    restart();
}

void DisplayTestScreen::nextTest() { m_test = static_cast<uint8_t>((m_test + 1) % TEST_COUNT); restart(); }
void DisplayTestScreen::prevTest() { m_test = static_cast<uint8_t>((m_test + TEST_COUNT - 1) % TEST_COUNT); restart(); }

void DisplayTestScreen::restart() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;
    gfx->fillScreen(COLOR_BLACK);
    if (m_test == INVERTED) {
        gfx->fillRect(0, kTop, SCREEN_WIDTH, SCREEN_HEIGHT - kTop, COLOR_WHITE);
    }
    // Markers start spread out so they don't all cross the same columns.
    for (uint8_t r = 0; r < kRows; ++r) {
        m_pos[r] = static_cast<int16_t>(SCREEN_WIDTH - 1 - (r * 53) % SCREEN_WIDTH);
    }
    m_frames = 0;
    drawHeader();

    if (m_test == TINY_WINDOWS) {
        gfx->setTextSize(1);
        gfx->setTextColor(COLOR_YELLOW, COLOR_BLACK);
        const char* labels[] = {"A", "B", "C", "D"};
        for (uint8_t a = 0; a < 4; ++a) {
            gfx->setCursor(a * 107 + 2, kTop);
            gfx->print(labels[a]);
        }
    }

    if (m_test == THIN_ROWS) {
        for (uint8_t r = 0; r < kRows; ++r) {
            m_pos[r] = static_cast<int16_t>(SCREEN_HEIGHT - 1 - (r * 13) % (SCREEN_HEIGHT - kTop));
        }
    }

    if (m_test == ALIGNMENT) {
        gfx->setTextSize(1);
        gfx->setTextColor(COLOR_YELLOW, COLOR_BLACK);
        for (uint8_t r = 0; r < kRows; ++r) {
            const AlignRow& row = kAlignRows[r];
            int16_t y = kTop + r * (kRowH + kRowGap);
            if ((y & 1) != (row.yOdd ? 1 : 0)) ++y;
            char lbl[2] = {row.label, '\0'};
            gfx->setCursor(4, y + 3);
            gfx->print(lbl);
            // Start position with the row's x parity.
            int16_t x = SCREEN_WIDTH - 2 - r * 37;
            if (row.xParity >= 0 && (x & 1) != row.xParity) --x;
            m_pos[r] = x;
        }
    }
}

void DisplayTestScreen::drawHeader() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_YELLOW, COLOR_BLACK);
    gfx->setCursor(2, 2);
    gfx->print(kTestNames[m_test]);
    gfx->setTextColor(0x8410, COLOR_BLACK);
    gfx->setCursor(2, 12);
    gfx->print(kTestHints[m_test]);
    gfx->setCursor(SCREEN_WIDTH - 6 * 27, 2);
    gfx->print("UP/DN test  R clear  L exit");
    drawFixState();
}

// "pair:on rows:off" -- the panel workarounds' switches (PanelDriver.hpp).
void DisplayTestScreen::drawFixState() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    char buf[32];
    snprintf(buf, sizeof(buf), "pair:%s rows:%s", NV3007Display::alignPairs ? "on " : "off",
             NV3007Display::minRows > 1 ? "4  " : "off");
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_YELLOW, COLOR_BLACK);
    gfx->setCursor(SCREEN_WIDTH - 6 * 18, 12);
    gfx->print(buf);
}

void DisplayTestScreen::toggleMinRows() {
    NV3007Display::minRows = (NV3007Display::minRows > 1) ? 0 : 4;
    restart();
}

void DisplayTestScreen::toggleColumnFix() {
    NV3007Display::alignPairs = !NV3007Display::alignPairs;
    restart();
}

void DisplayTestScreen::render() {
    switch (m_test) {
        case PEAK_WALK:    stepWalk(false, false); break;
        case WALK_CS_HELD: stepWalk(true,  false); break;
        case SAME_SPOT:    stepSameSpot();         break;
        case INVERTED:     stepWalk(false, true);  break;
        case DATA_PATTERN: stepDataPattern();      break;
        case ALIGNMENT:    stepAlignment();        break;
        case THIN_ROWS:    stepThinRows();         break;
        case TINY_WINDOWS: stepTinyWindows();      break;
        default: break;
    }
    ++m_frames;
}

// Each row: erase the marker, move it 1px left (wrapping), draw it again --
// the same erase/draw pair a decaying peak makes every frame.
void DisplayTestScreen::stepWalk(bool oneTransaction, bool inverted) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    uint16_t bg = inverted ? COLOR_WHITE : COLOR_BLACK;
    uint16_t fg = inverted ? COLOR_BLACK : COLOR_WHITE;

    if (oneTransaction) gfx->startWrite(); // CS low for the whole frame
    for (uint8_t r = 0; r < kRows; ++r) {
        int16_t y = kTop + r * (kRowH + kRowGap);
        int16_t newPos = (m_pos[r] > 0) ? m_pos[r] - 1 : SCREEN_WIDTH - 1;
        if (oneTransaction) {
            gfx->writeFillRect(m_pos[r], y, 1, kRowH, bg);
            gfx->writeFillRect(newPos,   y, 1, kRowH, fg);
        } else {
            gfx->fillRect(m_pos[r], y, 1, kRowH, bg); // each call: CS low ... CS high
            gfx->fillRect(newPos,   y, 1, kRowH, fg);
        }
        m_pos[r] = newPos;
    }
    if (oneTransaction) gfx->endWrite();
}

// Draw and erase a marker 20 times on the same spot, ending erased, then move
// on. After the first fill the driver's window cache skips CASET/RASET, so
// each fill is just RAMWR + pixel data: far fewer command/data (DC) switches
// and no window changes.
void DisplayTestScreen::stepSameSpot() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    m_rng = m_rng * 1103515245u + 12345u;
    int16_t x = static_cast<int16_t>((m_rng >> 8) % SCREEN_WIDTH);
    m_rng = m_rng * 1103515245u + 12345u;
    int16_t y = kTop + static_cast<int16_t>((m_rng >> 8) % (SCREEN_HEIGHT - kTop - kRowH));
    for (uint8_t i = 0; i < 10; ++i) {
        gfx->fillRect(x, y, 1, kRowH, COLOR_WHITE);
        gfx->fillRect(x, y, 1, kRowH, COLOR_BLACK);
    }
}

// Alternate two checkerboards of 0x5555 / 0xAAAA (every bit flips between
// neighbours) a band at a time. With an intact bus the image just flickers
// between two patterns of the same two colors; a corrupted bit shows as a
// differently colored speck or a shifted band.
void DisplayTestScreen::stepDataPattern() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    constexpr int16_t cell = 8;
    int16_t band = static_cast<int16_t>(m_frames % ((SCREEN_HEIGHT - kTop) / cell));
    int16_t y = kTop + band * cell;
    if (band == 0) m_phase = !m_phase;
    for (int16_t x = 0; x + cell <= SCREEN_WIDTH; x += cell) {
        bool odd = ((x / cell) + band + (m_phase ? 1 : 0)) & 1;
        gfx->fillRect(x, y, cell, cell, odd ? 0x5555 : 0xAAAA);
    }
}

// Like the peak walk, but each row with its own alignment (kAlignRows): 2px
// markers move 2px so their x parity never changes; 1px markers move 1px.
void DisplayTestScreen::stepAlignment() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    for (uint8_t r = 0; r < kRows; ++r) {
        const AlignRow& row = kAlignRows[r];
        int16_t y = kTop + r * (kRowH + kRowGap);
        if ((y & 1) != (row.yOdd ? 1 : 0)) ++y;

        int16_t step   = row.w;
        int16_t newPos = m_pos[r] - step;
        if (newPos < kAlignLeft) {
            newPos = SCREEN_WIDTH - row.w; // wrap to the right edge, keeping the parity
            if (row.xParity >= 0 && (newPos & 1) != row.xParity) --newPos;
        }
        gfx->fillRect(m_pos[r], y, row.w, row.h, COLOR_BLACK);
        gfx->fillRect(newPos,   y, row.w, row.h, COLOR_WHITE);
        m_pos[r] = newPos;
    }
}

// Eight 12-px-wide markers, each one pixel TALL, walking up 1 px per frame
// (wrapping to the bottom) -- the shape of the spectrum's peak lines.
void DisplayTestScreen::stepThinRows() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    for (uint8_t r = 0; r < kRows; ++r) {
        int16_t x      = 24 + r * 50;
        int16_t newPos = (m_pos[r] > kTop) ? m_pos[r] - 1 : SCREEN_HEIGHT - 1;
        gfx->fillRect(x, m_pos[r], 12, 1, COLOR_BLACK);
        gfx->fillRect(x, newPos,   12, 1, COLOR_WHITE);
        m_pos[r] = newPos;
    }
}

// Four areas, 8 tiny dots each per frame: draw them all, then erase them all
// (so draw and erase of a dot are separate windows, as in a real redraw).
// All x positions even and widths even, so the pair fix passes them through
// unchanged and the window shapes are exactly as labelled.
void DisplayTestScreen::stepTinyWindows() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    struct Shape { int16_t w, h; bool oddY; };
    static const Shape kShapes[4] = { {2, 1, false}, {2, 1, true}, {2, 2, false}, {4, 1, false} };
    int16_t xs[4][8], ys[4][8];

    for (uint8_t a = 0; a < 4; ++a) {
        for (uint8_t i = 0; i < 8; ++i) {
            m_rng = m_rng * 1103515245u + 12345u;
            int16_t x = static_cast<int16_t>(a * 107 + 12 + ((m_rng >> 8) % 88));
            x &= ~1; // even
            m_rng = m_rng * 1103515245u + 12345u;
            int16_t y = static_cast<int16_t>(kTop + 10 + ((m_rng >> 8) % (SCREEN_HEIGHT - kTop - 14)));
            if ((y & 1) != (kShapes[a].oddY ? 1 : 0)) ++y;
            xs[a][i] = x;
            ys[a][i] = y;
            gfx->fillRect(x, y, kShapes[a].w, kShapes[a].h, COLOR_WHITE);
        }
    }
    for (uint8_t a = 0; a < 4; ++a) {
        for (uint8_t i = 0; i < 8; ++i) {
            gfx->fillRect(xs[a][i], ys[a][i], kShapes[a].w, kShapes[a].h, COLOR_BLACK);
        }
    }
}
