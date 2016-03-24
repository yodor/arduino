#include "SegmentDisplay.h"
#include <Arduino.h>
#include <Wire.h>

Adafruit_MCP23017 mcp;

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
#define SEG_B     10
#define UNUSED_1  11
#define UNUSED_2  12
#define SEG_F     13
#define SEG_A     14
#define UNUSED_3  15



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

#define DELAY_SEGMENT 5500

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
  delayMicroseconds(DELAY_SEGMENT);

  value = 0xFFFF;
  
  bitClear(value, EN_D1);
  bitClear(value, EN_D2);
  bitClear(value, EN_D3);

  mcp.writeGPIOAB(value);
  //delayMicroseconds(dlc);
  
}


void SegmentDisplay::showFloat(float number)
{
  int tens = (int(number) % 100) / 10;
  int units = (int(number) % 10);
  int dot_tens = (int(number * 10) % 10);

  
  
  //if (tens>0) {
    
    digitBits(1,  tens, false);
   
 // }
  
  digitBits(2, units, true);
  digitBits(3, dot_tens, false);

}



