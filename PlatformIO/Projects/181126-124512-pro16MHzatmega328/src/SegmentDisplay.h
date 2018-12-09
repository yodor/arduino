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
    void showHour(int hr, int mn);

    void digitBits(uint8_t seg_num, int dgt, bool dot);
    
    void start();
    
protected:
//    void segmentsOff();
//    void showDigit(int dgt, bool dot);
//    void enableSlot(uint8_t slot);
    
//    void clearSegment(uint16_t& value);
};


#endif //SEGMENT_DISPLAY_H
