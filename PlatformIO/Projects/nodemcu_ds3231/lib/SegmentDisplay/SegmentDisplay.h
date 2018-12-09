#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H
#include <Arduino.h>
#include <Adafruit_MCP23017.h>


class SegmentDisplay
{



public:
    SegmentDisplay();
    ~SegmentDisplay();


    void showHour(int hr, int mn);
    void showTemperature(double value);
    void digitBits(uint8_t digit_cell, int value);

    void begin();
    void loop(uint8_t hour, uint8_t minute, double temperature);
    void startProgress();
    void stepProgress();
    void endProgress();

protected:
//    void segmentsOff();
//    void showDigit(int dgt, bool dot);
//    void enableSlot(uint8_t slot);

//    void clearSegment(uint16_t& value);
};


#endif //SEGMENT_DISPLAY_H
