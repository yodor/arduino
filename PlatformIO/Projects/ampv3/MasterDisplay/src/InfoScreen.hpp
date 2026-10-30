#pragma once
#include "Screen.hpp"
#include "Config.hpp"
#include "DisplaySettings.hpp"

// The info screen: volume plus the unit's state at a glance. It's the first
// screen in the cycle, AND the popup shown over the other screens for a
// moment when the volume or mute changes (main.cpp; the old volume overlay).
//
//   header:  COMP  +0.05dB  24.3C  MUTE           95K
//   middle:  -23.5 dB (or 14/31)  124 BPM | small scope | small spectrum
//   bottom:  the volume bar, one block per step, lit up to the current one
//
// Header tags are short colored words in fixed slots (each appears only when
// it has something to say): COMP green when the thermal compensation is on,
// gray when off; the last calibration's L/R match; the driver-block
// temperature (red "NTC!" on a sensor fault); an alert slot shared by MUTE
// (red) and RESYNC (yellow, the attenuator recommends one) -- alternating each
// second when both apply; the Rtotal value on the right (auto/manual and the
// full form are one Options-key press away, in the middle-right readout). Text rather than icons: at 16 px with the
// panel's bitmap font, words read instantly where icons would be blobs.
//
// When the attenuator is calibrating, uncalibrated or not answering, the big
// area says so instead of showing a stale number. Everything is redrawn only
// when it changes.
class InfoScreen : public Screen {
public:
    void onEnter() override;  // full redraw
    void render() override;   // redraws only what changed
    const char* name() const override { return "INFO"; }

    // --- As the popup over other screens ---
    void poke();                    // show / extend (volume or mute touched)
    void update();                  // call every loop: change detection + expiry
    bool isActive() const { return m_active; }

    // The first volume press is passive: if the info isn't on the panel yet
    // (neither this screen nor its popup), it only brings the popup up --
    // the volume changes from the next press, while the info is visible. A
    // press still held (or an IR key still repeating) stays passive until
    // it's released. Clears up/down while passive; returns whether there was
    // any volume input at all (the popup is poked for it).
    bool filterFirstVolumePress(bool& up, bool& down, bool onThisScreen);
    void dismiss() { m_active = false; }

    // The middle-right readout, stepped with the remote's Options key on this
    // screen: the detected tempo (default), a small oscilloscope (mono L+R,
    // triggered), or a small spectrum (mono, doubling bands) -- both in a
    // fixed gray. A list of views, so more can be added here later.
    enum class RightInfo : uint8_t { BPM, SCOPE, SPECTRUM, COUNT };
    void toggleRightInfo() {
        m_rightInfo = static_cast<RightInfo>((static_cast<uint8_t>(m_rightInfo) + 1) % static_cast<uint8_t>(RightInfo::COUNT));
        m_drawnRight[0] = '\0';
    }
    bool showsBpm() const { return m_rightInfo == RightInfo::BPM; }

private:
    enum class View : uint8_t { NONE, NO_LINK, CHECKING, CALIBRATING, NOT_CALIBRATED, VOLUME };
    View currentView() const;

    void drawStaticForView(View v);
    void drawRightInfo();
    void drawScope();
    void drawMiniSpectrum();
    void drawTags();
    void drawValue(int16_t vol, int16_t volMax, int32_t dbTenths);
    void drawBar(int16_t vol, int16_t volMin, int16_t volMax, bool muted);
    void drawCentered(const char* text, uint8_t size, int16_t y, uint16_t color);
    void drawSlot(uint8_t slot, int16_t x, uint8_t widthChars, const char* text, uint16_t color);

    bool     m_active     = false;
    bool     m_volPassive = false;    // filterFirstVolumePress()
    RightInfo m_rightInfo = RightInfo::BPM;
    uint32_t m_lastPokeMs = 0;

    // change detection in update()
    uint32_t m_lastStatusCount = 0;
    bool     m_haveBaseline    = false;
    int16_t  m_baseVol         = -2;
    bool     m_baseMute        = false;

    // what's on the panel
    bool     m_fullRedraw    = true;
    View     m_drawnView     = View::NONE;
    int32_t  m_drawnDbTenths = INT32_MIN;
    int16_t  m_drawnVol      = -2;
    int16_t  m_drawnVolMin   = -2;
    int16_t  m_drawnVolMax   = -2;
    int16_t  m_drawnLit      = -1;
    bool     m_drawnBarMuted = false;
    char     m_drawnRight[24] = "";
    VolumeDisplay m_drawnVolDisplay = VolumeDisplay::DB;

    // The small scope (RightInfo::SCOPE): what each column shows now.
    static constexpr int16_t kScopeW = 176;
    int16_t  m_scopeLo[kScopeW] = {};
    int16_t  m_scopeHi[kScopeW] = {};
    bool     m_scopeDrawn  = false;
    float    m_scopePeak   = 0.0f;     // auto-gain
    uint32_t m_scopeLastMs = 0;

    // The small spectrum (RightInfo::SPECTRUM): each bar's shown level and
    // drawn height.
    static constexpr int kMiniBars = 10;
    float    m_miniLevel[kMiniBars]  = {};
    int16_t  m_miniDrawnH[kMiniBars] = {};
    bool     m_miniDrawn  = false;
    uint32_t m_miniLastMs = 0;
    static constexpr uint8_t kSlots = 5;
    char     m_drawnSlot[kSlots][12] = {};
    uint16_t m_drawnSlotColor[kSlots] = {};
};
