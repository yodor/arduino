#include "DS3231RTC.h"
 
DS3231 RTC;
int tempC;
int tempF;
 
void setup() {
  RTC.begin();
}
 
void loop() {
  tempC = RTC.getTemperature();
  tempF = (tempC * 1.8) + 32.0; // Convert C to F
 
  lcd.print(tempF);
  lcd.print("F");
}

