#include "MSGEQ7.h"



#define PIN_MSGEQ7_STROBE   13
#define PIN_MSGEQ7_RESET    12
#define PIN_MSGEQ7_DATA     A2

int band_levels[] = {0,127,255,383,511,639,767,895};

MSGEQ7::MSGEQ7()
{

}
MSGEQ7::~MSGEQ7()
{

}
uint8_t* MSGEQ7::getLevels()
{
  return &levels[0];
}
void MSGEQ7::begin()
{
  pinMode(PIN_MSGEQ7_RESET, OUTPUT); // reset
  pinMode(PIN_MSGEQ7_STROBE, OUTPUT); // strobe
  digitalWrite(PIN_MSGEQ7_RESET,LOW); // reset low
  digitalWrite(PIN_MSGEQ7_STROBE,HIGH); //pin 5 is RESET on the shield
}
void MSGEQ7::process()
{
  digitalWrite(PIN_MSGEQ7_RESET, HIGH);
  digitalWrite(PIN_MSGEQ7_RESET, LOW);
  for( int band = 0; band < 7; band++ )
  {
    digitalWrite(PIN_MSGEQ7_STROBE,LOW); // strobe pin on the shield - kicks the IC up to the next band 
    delayMicroseconds(33); // 
    fft[band] = analogRead(PIN_MSGEQ7_DATA); // store left band reading
    digitalWrite(PIN_MSGEQ7_STROBE,HIGH); 
  }
  

  
  //prepare levels
  for( int band = 0; band < 7; band++ )
  {
     int level = 0; 
     for (int level_index=0;level_index<7;level_index++) {
         if (fft[band]>=band_levels[level_index]) {
           level = level_index;
         }
     }
     levels[band] = level;
  }
  
  
}


MSGEQ7 EQ7 = MSGEQ7();

