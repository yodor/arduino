#include "MenuFont.hpp"
#include "Renderer.hpp"   // (brings in Arduino_GFX)

#include "MenuFontData.inc"   // generated: the three fonts' glyphs and rectangles

namespace {
const MenuFontGlyph& glyphFor(const MenuFont& f, char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u < f.first || u > f.last) u = '?';
    return f.glyphs[u - f.first];
}
} // namespace

int16_t menuTextWidth(const MenuFont& font, const char* text) {
    int16_t w = 0;
    for (const char* p = text; *p; ++p) w += glyphFor(font, *p).advance;
    return w;
}

int16_t menuDrawText(Arduino_GFX* gfx, const MenuFont& font, int16_t x, int16_t y, const char* text, uint16_t color) {
    for (const char* p = text; *p; ++p) {
        const MenuFontGlyph& g = glyphFor(font, *p);
        for (uint8_t i = 0; i < g.rectCount; ++i) {
            const MenuFontRect& r = font.rects[g.firstRect + i];
            gfx->fillRect(x + r.x, y + r.y, r.w, r.h, color);
        }
        x += g.advance;
    }
    return x;
}
