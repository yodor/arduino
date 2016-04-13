#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H
#include <Arduino.h>
#include "Adafruit_MCP23017.h"

class SegmentDisplay
{
  
      
    
public:
    SegmentDisplay();
    ~SegmentDisplay();
    
    void showFloat(float number);
//    void showFloat1(float number);
    void start();
    void displayOff();
    void updateSegments();
    void clearSegment();
protected:
//    void segmentsOff();
//    void showDigit(int dgt, bool dot);
//    void enableSlot(uint8_t slot);
    void digitBits(uint8_t seg_num, int dgt, bool dot);
//    void clearSegment(uint16_t& value);
int tens;
int units;
int dot_tens;
uint8_t current_digit;
};


#endif //SEGMENT_DISPLAY_H
