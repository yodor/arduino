#pragma once

// Common interface for all display visualizations (spectrum bars,
// waveform, analog VU, and future screens). Renderer owns only the
// low-level hardware primitives (SPI/panel setup, the raw Arduino_GFX
// handle); each Screen owns its own visualization-specific state and
// drawing logic entirely, so adding a new screen never means touching
// another screen's code or state.
class Screen {
public:
    virtual ~Screen() = default;

    // Called once when switching TO this screen: clear the display and
    // reset whatever per-screen delta-tracking state exists, so stale
    // pixel/state assumptions from a previous visit to this screen don't
    // leak into the fresh one.
    virtual void onEnter() = 0;

    // Called every frame while this screen is the active one.
    virtual void render() = 0;

    // For diagnostics/logging.
    virtual const char* name() const = 0;
};