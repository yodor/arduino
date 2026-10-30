#pragma once
#include <Arduino.h>

class Arduino_GFX;

// Proportional bitmap fonts for the menu's list view (MenuScreen): DejaVu
// Sans at 15 px (header), 38 px (item names) and 26 px (the line under them),
// generated from the TTF at 1 bit. Each glyph is stored as a few filled
// rectangles (rows of lit pixels merged downwards), so drawing a character is
// a handful of fillRect() calls -- vertical strokes are one rectangle each.
//
// DejaVu fonts: Bitstream Vera / DejaVu licence (free to embed and modify).
struct MenuFontRect  { int8_t x; uint8_t y, w, h; };        // x relative to the pen
struct MenuFontGlyph { uint16_t firstRect; uint8_t rectCount; uint8_t advance; };
struct MenuFont {
    uint8_t height;      // line box, px
    uint8_t baseline;    // from the top of the line box
    uint8_t first, last; // character range (ASCII)
    const MenuFontGlyph* glyphs;
    const MenuFontRect*  rects;
};

extern const MenuFont kMenuFontHeader;  // 15 px
extern const MenuFont kMenuFontLabel;   // 38 px
extern const MenuFont kMenuFontHint;    // 26 px

// Width of `text` in pixels.
int16_t menuTextWidth(const MenuFont& font, const char* text);
// Draws `text` with its line box's top-left at (x, y); returns the pen x after it.
// Only the glyphs' pixels are drawn (the background is left as it is).
int16_t menuDrawText(Arduino_GFX* gfx, const MenuFont& font, int16_t x, int16_t y, const char* text, uint16_t color);
