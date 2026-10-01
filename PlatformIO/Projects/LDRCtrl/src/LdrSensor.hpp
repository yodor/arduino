#pragma once
#include "Ads1115.hpp"

// ============================================================================
// LdrSensor
//
// One LDR board's resistance measurement, via its ADS1115 and known Rref.
// Wiring per the finalized A0/A1 swap: A0=ADS_VDD_RREF (before Rref),
// A1=TOP_JUNCTION (after Rref, top of Rseries), A3=MIDDLE_JUNCTION (between
// Rseries and Rshunt).
//
// The I2C bus is NOT started automatically -- call activate() before use
// and deactivate() when done. This is deliberate: TOP_JUNCTION/
// MIDDLE_JUNCTION are only actually connected to the LDR network while
// that board's relay is energized (calibration mode), and keeping the I2C
// peripheral fully de-initialized (not just idle) the rest of the time
// means one less digital signal toggling near the LDR sense nodes during
// normal playback. LDRVolume ties activate()/deactivate() directly to its
// relay energize/de-energize, so callers don't normally call these
// directly.
// ============================================================================
class LdrSensor {
public:
  LdrSensor(TwoWire &wire, uint8_t sdaPin, uint8_t sclPin,
            uint8_t addr = 0x48, float rRefOhms = 10000.0f)
      : wire_(wire), sdaPin_(sdaPin), sclPin_(sclPin), rRefOhms_(rRefOhms) {
    ads_.begin(wire_, addr);
  }

  // Configures SDA/SCL and starts the bus.
  void activate();

  // Fully de-inits the I2C peripheral (Wire::end(), i.e. pico-sdk
  // i2c_deinit under the hood) rather than leaving it idle-but-configured.
  void deactivate();

  // Briefly activates the bus, checks whether this sensor's address ACKs,
  // deactivates again. Use before an automatic sweep to distinguish "no
  // board attached" from "board attached but both LDRs are dark."
  bool isPresent() {
    activate();
    bool present = ads_.isPresent();
    deactivate();
    return present;
  }

  // Scans the bus for any ACKing address, activating/deactivating around
  // it so it doesn't leave the bus running afterwards. Independent of
  // relay state -- a scan doesn't need valid sense-node connections.
  void scanBus();

  // Three auto-ranged reads (Vref, Vs, Vsh) and the derived Rs/Rsh. Only
  // meaningful while active AND the board's relay is energized -- callers
  // (LDRVolume) are responsible for checking relay state before calling
  // this. Returns false (outputs undefined) if Vref <= 0.
  bool read(float &vRefOut, float &rsOut, float &rshOut);

  // Overrides the Rref value used to convert voltages into absolute
  // resistance -- e.g. once you've measured a specific board's actual
  // R27 rather than trusting its nominal 10k 1%. Takes effect for reads
  // from this point forward only; any already-characterized curve keeps
  // whatever Rref was in effect when it was measured -- re-run CALAUTO
  // after changing this if you want the correction reflected in a saved
  // calibration.
  void setRRefOhms(float ohms) { rRefOhms_ = ohms; }
  float rRefOhms() const { return rRefOhms_; }

private:
  TwoWire &wire_;
  uint8_t sdaPin_, sclPin_;
  Ads1115 ads_;
  float rRefOhms_;
};