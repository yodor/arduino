#include "SegmentDisplay.h"
#include <Arduino.h>
#include <Wire.h>

//adjust for long loops -  off time between segment updates
#define DELAY_SEGMENT 1000

Adafruit_MCP23017 mcp;

//The MCP23017 has 16 pins - A0 thru A7 + B0 thru B7. 
//A0 is called 0 in the library, and A7 is called 7, 
//then B0 continues from there as is called 8 and finally B7 is pin 15

//mcp23017 function / pin 
#define SEG_E      0
#define SEG_D      1
#define SEG_DP     2
#define SEG_C      3
#define SEG_G      4
#define UNUSED_4   5
#define UNUSED_5   6
#define EN_D3      7

#define EN_D2      8
#define EN_D1      9
#define SEG_B      10
#define UNUSED_1   11
#define UNUSED_2   12
#define SEG_F      13
#define SEG_A      14
#define UNUSED_3   15



const bool digits[11][7] = {
  {1, 1, 1, 1, 1, 1, 0}, //0
  {0, 1, 1, 0, 0, 0, 0}, //1
  {1, 1, 0, 1, 1, 0, 1}, //2
  {1, 1, 1, 1, 0, 0, 1}, //3
  {0, 1, 1, 0, 0, 1, 1}, //4
  {1, 0, 1, 1, 0, 1, 1}, //5
  {1, 0, 1, 1, 1, 1, 1}, //6
  {1, 1, 1, 0, 0, 0, 0}, //7
  {1, 1, 1, 1, 1, 1, 1}, //8
  {1, 1, 1, 1, 0, 1, 1}, //9
  {0, 0, 0, 0, 0, 0, 0}, //off
};

const uint8_t segments[7] = {
  SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G
};
const uint8_t unused[5] = {
  UNUSED_1, UNUSED_2, UNUSED_3, UNUSED_4, UNUSED_5
};

SegmentDisplay::SegmentDisplay()
{
    current_digit = 0;
    tens = 0;
    units = 0;
    dot_tens = 0;
    
}
SegmentDisplay::~SegmentDisplay()
{
}
void SegmentDisplay::start()
{
  
  
  mcp.begin(0);      // use default address 0

  mcp.pinMode(EN_D1, OUTPUT);
  mcp.digitalWrite(EN_D1, LOW);
  mcp.pinMode(EN_D2, OUTPUT);
  mcp.digitalWrite(EN_D2, LOW);
  mcp.pinMode(EN_D3, OUTPUT);
  mcp.digitalWrite(EN_D3, LOW);

  //unused test pullup
  for (uint8_t a=0;a<5;a++) {
      mcp.pinMode(unused[a], INPUT);
      mcp.pullUp(unused[a], HIGH);  // turn on a 100K pullup internally
  }
  
  
  for (uint8_t a=0;a<7;a++) {
    mcp.pinMode(segments[a], OUTPUT);
  }
  mcp.pinMode(SEG_DP, OUTPUT);
}



void SegmentDisplay::digitBits(uint8_t seg_num, int dgt, bool dot)
{
  uint16_t value = 0xFFFF;
  
  if (seg_num==1) {
      bitSet(value, EN_D1);
      bitClear(value, EN_D2);
      bitClear(value, EN_D3);
  }
  else if (seg_num==2) {
      bitClear(value, EN_D1);
      bitSet(value, EN_D2);
      bitClear(value, EN_D3);
  }
  else if (seg_num==3) {
      bitClear(value, EN_D1);
      bitClear(value, EN_D2);
      bitSet(value, EN_D3);
  }
  
  for (uint8_t a=0;a<7;a++) {
    bool seg_val =  digits[dgt][a];
    if (seg_val) {
      bitClear(value, segments[a]);  
    }
  }

//already high from 0xFFFF
//  for (int a=0;a<5;a++) {
//    bitSet(value, unused[a]);  
//  }
  
  if (dot) {
    bitClear(value, SEG_DP);
  }

  mcp.writeGPIOAB(value);
  

  //value = 0xFFFF;
  
  //bitClear(value, EN_D1);
  //bitClear(value, EN_D2);
  //bitClear(value, EN_D3);

  //mcp.writeGPIOAB(value);
  
  //delayMicroseconds(dlc);
  
}

void SegmentDisplay::displayOff()
{
    mcp.digitalWrite(EN_D1, LOW);
    mcp.digitalWrite(EN_D2, LOW);
    mcp.digitalWrite(EN_D3, LOW);
}
void SegmentDisplay::showFloat(float number)
{
  tens = (int(number) % 100) / 10;
  units = (int(number) % 10);
  dot_tens = (int(number * 10) % 10);

}
void SegmentDisplay::clearSegment()
{
    uint16_t value = 0xFFFF;

    bitClear(value, EN_D1);
    bitClear(value, EN_D2);
    bitClear(value, EN_D3);
  
    mcp.writeGPIOAB(value);  
}
void SegmentDisplay::updateSegments()
{

  //if (current_digit>3) {
   //   current_digit=1;
  //}
  
  //if (tens>0) {
  
   //if (current_digit==1) { 
    digitBits(1,  tens, false);
    delayMicroseconds(DELAY_SEGMENT);
    clearSegment();
    
   //}
   //else if (current_digit==2) {
      digitBits(2, units, true);
      delayMicroseconds(DELAY_SEGMENT);
      clearSegment();
   //}
   // else if (current_digit==3) {
        digitBits(3, dot_tens, false);
        delayMicroseconds(DELAY_SEGMENT);
        clearSegment();
   // }
    

      
 // }

  //current_digit++;
  
  
  

}



