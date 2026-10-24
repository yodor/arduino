#pragma once
#include <Arduino.h>

// Calendar date/time as kept by the RTC -- local time, 24h, years 2000-2099.
struct RtcDateTime {
    uint16_t year   = 2000;
    uint8_t  month  = 1;   // 1-12
    uint8_t  day    = 1;   // 1-31
    uint8_t  hour   = 0;   // 0-23
    uint8_t  minute = 0;
    uint8_t  second = 0;
};

// DS3231 real-time clock on the i2c1 hardware bus (Wire1, SDA_PIN/SCL_PIN in
// Pins.hpp). Small self-contained driver -- the DS3231 is just a handful of
// BCD registers, not worth a library dependency.
//
// The time is never set automatically: begin() only makes sure the
// oscillator runs on the battery and reports the time (flagging it if the
// oscillator had stopped). Setting it is up to the user -- Settings -> Clock
// (Set Time / Set Date) or the USB console's TIME command.
class RtcClock {
public:
    static RtcClock& instance();

    // Starts Wire1, probes for the DS3231, applies the sync policy above and
    // logs the outcome. Returns false if no DS3231 answered.
    bool begin();

    bool isPresent() const { return m_present; }

    // Reads the current time; false if the clock is absent or the I2C read failed.
    bool read(RtcDateTime& out);

    // Sets the clock and clears the Oscillator Stop Flag.
    bool set(const RtcDateTime& dt);

    // "YYYY-MM-DD HH:MM:SS" (needs a 20-byte buffer).
    static void format(const RtcDateTime& dt, char* out, size_t outSize);

    // 0 = Sunday ... 6 = Saturday.
    static uint8_t weekday(const RtcDateTime& dt);

private:
    RtcClock() = default;

    bool readRegs(uint8_t reg, uint8_t* buf, size_t len);
    bool writeRegs(uint8_t reg, const uint8_t* buf, size_t len);

    bool m_present = false;
};
