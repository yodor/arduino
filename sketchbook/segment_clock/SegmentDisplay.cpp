#include "SegmentDisplay.h"
#include <Arduino.h>
#include <Wire.h>

#define DELAY_SEGMENT_ON 3000
#define DELAY_SEGMENT_OFF 100
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

#define EN_D1      11
#define EN_D2      10
#define EN_D3      9
#define EN_D4      8

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
  {1, 1, 1, 0, 1, 1, 1}, //6
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



void SegmentDisplay::digitBits(uint8_t digit_cell, int value)
{
  uint16_t gpio_value = 0x0000;
  
  if (digit_cell==1) {
      bitSet(gpio_value, EN_D1);
      //bitClear(value, EN_D2);
     // bitClear(value, EN_D3);
      //bitClear(value, EN_D4);
  }
  else if (digit_cell==2) {
     // bitClear(value, EN_D1);
      bitSet(gpio_value, EN_D2);
      //bitClear(value, EN_D3);
      //bitClear(value, EN_D4);
  }
  else if (digit_cell==3) {
      //bitClear(value, EN_D1);
      //bitClear(value, EN_D2);
      bitSet(gpio_value, EN_D3);
      //bitClear(value, EN_D4);
  }
  else if (digit_cell==4) {
      //bitClear(value, EN_D1);
      //bitClear(value, EN_D2);
      //bitClear(value, EN_D3);
      bitSet(gpio_value, EN_D4);
  }

  if (value>11) value = 12;
  
  if (value>-1) {
    for (uint8_t a=0;a<7;a++) {
      bool segment_enabled =  digits[value][a];
      if (segment_enabled) {
        bitSet(gpio_value, segments[a]);  
      }
    }
  }

  mcp.writeGPIOAB(gpio_value);
  delayMicroseconds(DELAY_SEGMENT_ON);

  gpio_value = 0x0000;

  mcp.writeGPIOAB(gpio_value);
  delayMicroseconds(DELAY_SEGMENT_OFF);

 
}


void SegmentDisplay::showHour(int hr, int mn)
{
  int digit1 = hr>9 ? hr/10 : -1;
  int digit2 = digit1>0 ? hr-(digit1*10) : hr;
  int digit3 = mn>9 ? mn/10 : 0;
  int digit4 = digit3>0 ? mn-(digit3*10) : mn;

  digitBits(1, digit1);
  digitBits(2, digit2);
  digitBits(3, digit3);
  digitBits(4, digit4);

//  Serial.print(digit1);
//  Serial.print(" ");
//  Serial.print(digit2);
//  Serial.print(" ");
//  Serial.print(digit3);
//  Serial.print(" ");
//  Serial.print(digit4);
//  Serial.print(" ");
//  Serial.println();
  
}

void SegmentDisplay::showTemperature(double value)
{
    int whole = (int)value;
    int dec = (int)((value - whole) * 10);

  int digit1 = whole>9 ? whole/10 : -1;
  int digit2 = digit1>0 ? whole-(digit1*10) : whole;
  int digit3 = 11;
  int digit4 = dec;

  digitBits(1, digit1);
  digitBits(2, digit2);
  digitBits(3, digit3);
  digitBits(4, digit4);

//
//  
//  Serial.print(digit1,DEC);
//  Serial.print(" ");
//  Serial.print(digit2,DEC);
//  Serial.print("-");
//  Serial.print(digit3,DEC);
//  Serial.print("-");
//  Serial.print(digit4,DEC);
//  Serial.print(" ");
//  Serial.println();
  
  
}
