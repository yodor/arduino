#include "RtcClock.hpp"
#include "Config.hpp"
#include <Wire.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint8_t kDs3231Addr = 0x68;

// DS3231 registers
constexpr uint8_t kRegSeconds = 0x00; // 0x00..0x06: sec, min, hour, day-of-week, date, month(+century), year
constexpr uint8_t kRegControl = 0x0E;
constexpr uint8_t kRegStatus  = 0x0F;

constexpr uint8_t kControlEOSC = 0x80; // 1 = oscillator STOPS when running on the battery
constexpr uint8_t kStatusOSF   = 0x80; // 1 = oscillator stopped at some point -> time not valid

uint8_t bcdToBin(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t binToBcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

// Day of week 1-7 (1 = Sunday) for the DS3231's day register -- the chip
// only counts it, it doesn't derive it from the date. Sakamoto's method.
uint8_t dayOfWeek(uint16_t y, uint8_t m, uint8_t d) {
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return static_cast<uint8_t>((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 1);
}

} // namespace

uint8_t RtcClock::weekday(const RtcDateTime& dt) {
    return static_cast<uint8_t>(dayOfWeek(dt.year, dt.month, dt.day) - 1);
}

RtcClock& RtcClock::instance() {
    static RtcClock inst;
    return inst;
}

bool RtcClock::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire1.beginTransmission(kDs3231Addr);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false; // repeated start
    if (Wire1.requestFrom(kDs3231Addr, len) != len) return false;
    for (size_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(Wire1.read());
    return true;
}

bool RtcClock::writeRegs(uint8_t reg, const uint8_t* buf, size_t len) {
    Wire1.beginTransmission(kDs3231Addr);
    Wire1.write(reg);
    Wire1.write(buf, len);
    return Wire1.endTransmission() == 0;
}

void RtcClock::format(const RtcDateTime& dt, char* out, size_t outSize) {
    snprintf(out, outSize, "%04u-%02u-%02u %02u:%02u:%02u",
             static_cast<unsigned>(dt.year), static_cast<unsigned>(dt.month), static_cast<unsigned>(dt.day),
             static_cast<unsigned>(dt.hour), static_cast<unsigned>(dt.minute), static_cast<unsigned>(dt.second));
}

bool RtcClock::read(RtcDateTime& out) {
    if (!m_present) return false;
    uint8_t r[7];
    if (!readRegs(kRegSeconds, r, sizeof(r))) return false;

    out.second = bcdToBin(r[0] & 0x7F);
    out.minute = bcdToBin(r[1] & 0x7F);
    if (r[2] & 0x40) {
        // 12-hour mode (only if something else ever set it): bit 5 = PM
        uint8_t h12 = bcdToBin(r[2] & 0x1F);
        bool pm = r[2] & 0x20;
        out.hour = static_cast<uint8_t>((h12 % 12) + (pm ? 12 : 0));
    } else {
        out.hour = bcdToBin(r[2] & 0x3F);
    }
    out.day   = bcdToBin(r[4] & 0x3F);
    out.month = bcdToBin(r[5] & 0x1F); // bit 7 is the century flag -- ignored, years are 2000-2099
    out.year  = static_cast<uint16_t>(2000 + bcdToBin(r[6]));
    return true;
}

bool RtcClock::set(const RtcDateTime& dt) {
    if (!m_present) return false;
    uint8_t r[7] = {
        binToBcd(dt.second),
        binToBcd(dt.minute),
        binToBcd(dt.hour),                  // bit 6 clear = 24-hour mode
        dayOfWeek(dt.year, dt.month, dt.day),
        binToBcd(dt.day),
        binToBcd(dt.month),                 // century bit clear (2000-2099)
        binToBcd(static_cast<uint8_t>(dt.year % 100)),
    };
    if (!writeRegs(kRegSeconds, r, sizeof(r))) return false;

    // Time is valid again: clear the Oscillator Stop Flag, keep the other bits.
    uint8_t status = 0;
    if (!readRegs(kRegStatus, &status, 1)) return false;
    status &= static_cast<uint8_t>(~kStatusOSF);
    return writeRegs(kRegStatus, &status, 1);
}

bool RtcClock::begin() {
    Wire1.setSDA(SDA_PIN);
    Wire1.setSCL(SCL_PIN);
    Wire1.begin();
    Wire1.setClock(RTC_I2C_CLOCK_HZ);

    Wire1.beginTransmission(kDs3231Addr);
    m_present = (Wire1.endTransmission() == 0);
    if (!m_present) {
        Serial.println("[RTC] No DS3231 answering on i2c1 -- clock disabled");
        return false;
    }

    // Make sure the oscillator keeps running on the backup battery (EOSC=1
    // would stop it whenever main power is off).
    uint8_t control = 0;
    if (readRegs(kRegControl, &control, 1) && (control & kControlEOSC)) {
        control &= static_cast<uint8_t>(~kControlEOSC);
        writeRegs(kRegControl, &control, 1);
        Serial.println("[RTC] Battery oscillator was disabled -- enabled it");
    }

    uint8_t status = 0;
    bool oscStopped = readRegs(kRegStatus, &status, 1) && (status & kStatusOSF);

    // The clock is never overwritten automatically -- it keeps whatever it
    // has. If the oscillator had stopped (never set, or both main power and
    // the battery were lost), the time can't be trusted: say so, and leave
    // setting it to the user (Settings -> Clock, or the console's TIME).
    RtcDateTime now;
    char buf[20];
    if (read(now)) {
        RtcClock::format(now, buf, sizeof(buf));
        Serial.print("[RTC] DS3231 found, time ");
        Serial.print(buf);
        Serial.println(oscStopped ? " -- NOT VALID (clock had stopped): set it in Settings > Clock" : "");
    } else {
        Serial.println("[RTC] DS3231 found, but reading the time failed");
    }
    return true;
}
