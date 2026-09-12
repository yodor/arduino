#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "Config.hpp"

// Thin hardware layer: owns SPI/panel setup and the underlying Arduino_GFX
// handle. Deliberately has NO per-visualization state or drawing logic --
// that all lives in individual Screen subclasses (BarSpectrumScreen,
// WaveformScreen, etc), which call gfx() to get the raw drawing primitives
// they need.
class Renderer {
public:
    static Renderer& instance();

    void init();

    // Blanks the panel to black. Screens call this from their own
    // onEnter() alongside resetting their own state -- this alone does
    // NOT reset any screen's tracking state, since Renderer no longer
    // knows about any of that.
    void clear();

    // Raw Arduino_GFX handle for screens to draw with directly (fillRect,
    // drawLine, drawFastHLine, setCursor/print for text, etc) rather than
    // Renderer re-exposing every primitive as its own wrapper.
    Arduino_GFX* gfx() { return m_tft; }

private:
    Renderer() = default;

    Arduino_DataBus* m_bus = nullptr;
    Arduino_NV3007*  m_tft = nullptr;
};