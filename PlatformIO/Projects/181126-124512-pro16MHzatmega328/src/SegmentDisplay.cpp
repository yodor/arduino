#include "SegmentDisplay.h"
#include <Arduino.h>
#include <Wire.h>

#define DELAY_SEGMENT 6000

Adafruit_MCP23017 mcp;

//The MCP23017 has 16 pins - A0 thru A7 + B0 thru B7. 
//A0 is called 0 in the library, and A7 is called 7, 
//then B0 continues from there as is called 8 and finally B7 is pin 15
/**
pin   lib_pin      lib_pin       pin
1   -    8    | v |  7          - 28
2   -    9    |   |  6          - 27
3   -   10    |   |  5          - 26
4   -   11    |   |  4          - 25
5   -   12    |   |  3          - 24
6   -   13    |   |  2          - 23
7   -   14    |   |  1          - 22
8   -   15    |   |  0          - 21 
9   -   VCC   |   |  INT2       - 20
10  -   GND   |   |  INT1       - 19
11  -   NC    |   |  RST        - 18 to 5V with 10K
12  -   SCL   |   |  ADR2       - 17
13  -   SDA   |   |  ADR1       - 16
14  -   NC    |   |  ADR0       - 15
*/

//mcp23017 function / pin 
#define SEG_A      7
#define SEG_B      6
#define SEG_C      5
#define SEG_D      4
#define SEG_E      3
#define SEG_F      2
#define SEG_G      1
#define UNUSED_1   0

#define EN_D1      8
#define EN_D2      9
#define EN_D3      10
#define EN_D4      11

#define UNUSED_2   12
#define UNUSED_3   13
#define UNUSED_4   14
#define UNUSED_5   15

// A  B  C  D  E  F  G

const bool digits[13][8] = {
  {1, 1, 1, 1, 1, 1, 0}, //0
  {0, 0, 0, 1, 1, 0, 0}, //1
  {1, 0, 1, 1, 0, 1, 1}, //2
  {0, 0, 1, 1, 1, 1, 1}, //3
  {0, 1, 0, 1, 1, 0, 1}, //4
  {0, 1, 1, 0, 1, 1, 1}, //5
  {1, 0, 1, 1, 1, 1, 1}, //6
  {0, 0, 1, 1, 1, 0, 0}, //7
  {1, 1, 1, 1, 1, 1, 1}, //8
  {0, 1, 1, 1, 1, 1, 1}, //9
  {1, 0, 0, 0, 1, 1, 1}, //o
  {0, 1, 1, 1, 0, 0, 1}, //^o
  {0, 0, 0, 0, 0, 0, 0}, //off
};

const uint8_t segments[7] = {
  SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G
};
const uint8_t unused[5] = {
  UNUSED_1, UNUSED_2, UNUSED_3, UNUSED_4, UNUSED_4
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
  mcp.pinMode(EN_D4, OUTPUT);
  mcp.digitalWrite(EN_D4, LOW);

  //unused test pullup
  for (uint8_t a=0;a<5;a++) {
      mcp.pinMode(unused[a], OUTPUT);
      mcp.pullUp(unused[a], LOW);  // turn on a 100K pullup internally
  }
  
  
  for (uint8_t a=0;a<7;a++) {
    mcp.pinMode(segments[a], OUTPUT);
    mcp.digitalWrite(segments[a], LOW);
  }
  //mcp.pinMode(SEG_DP, OUTPUT);
}



void SegmentDisplay::digitBits(uint8_t seg_num, int dgt, bool dot)
{
  uint16_t value = 0x0000;
  
  if (seg_num==1) {
      bitSet(value, EN_D1);
      //bitClear(value, EN_D2);
     // bitClear(value, EN_D3);
      //bitClear(value, EN_D4);
  }
  else if (seg_num==2) {
     // bitClear(value, EN_D1);
      bitSet(value, EN_D2);
      //bitClear(value, EN_D3);
      //bitClear(value, EN_D4);
  }
  else if (seg_num==3) {
      //bitClear(value, EN_D1);
      //bitClear(value, EN_D2);
      bitSet(value, EN_D3);
      //bitClear(value, EN_D4);
  }
  else if (seg_num==4) {
      //bitClear(value, EN_D1);
      //bitClear(value, EN_D2);
      //bitClear(value, EN_D3);
      bitSet(value, EN_D4);
  }
  
  for (uint8_t a=0;a<7;a++) {
    bool seg_val =  digits[dgt][a];
    if (seg_val) {
      bitSet(value, segments[a]);  
    }
    else {
      bitClear(value, segments[a]);  
    }
  }


//already high from 0xFFFF
//  for (int a=0;a<5;a++) {
//    bitSet(value, unused[a]);  
//  }
  
  //if (dot) {
  //  bitClear(value, SEG_DP);
  //}

  mcp.writeGPIOAB(value);
  delayMicroseconds(DELAY_SEGMENT);
  //Serial.println(value,OCT);
  
  value = 0x0000;
  
  //bitClear(value, EN_D1);
  //bitClear(value, EN_D2);
  //bitClear(value, EN_D3);
  //bitClear(value, EN_D4);
  
  mcp.writeGPIOAB(value);
  //delayMicroseconds(DELAY_SEGMENT);
 //Serial.println(value,BIN);
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
void SegmentDisplay::showHour(int hr, int mn)
{
  
  
}
