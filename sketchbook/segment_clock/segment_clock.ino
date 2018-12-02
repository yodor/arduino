
#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "SegmentDisplay.h"

#include "RTClib.h"

SegmentDisplay disp;
unsigned long last_update = 0;
unsigned long temp_update = 0;
unsigned long led_update = 0;

#define INTERVAL_TEMP_UPDATE 30000
#define INTERVAL_TEMP_VISIBLE 5000
#define INTERVAL_LED_UPDATE 1000

DateTime compiled = DateTime(__DATE__, __TIME__);
//RTC_Millis rtc;
DS3231 rtc;

void setup() {  
  
  Serial.begin(115200);
  
  Wire.begin();
  //Wire.setClock(50000);

  //rtc.begin(compiled);
  rtc.begin();

  //rtc.adjust(DateTime(__DATE__, __TIME__));
  
  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }
  else {
    Serial.println("RTC is running");  
  }

  disp.start();
}
bool ps=false;
double current_temperature = 0;

void loop() {

  if (millis() - led_update > INTERVAL_LED_UPDATE) {

      led_update = millis();
      ps = !ps;
      digitalWrite(LED_BUILTIN, ps);
      compiled = rtc.now();
      
  }
  


  if (millis() - temp_update > INTERVAL_TEMP_VISIBLE) {
      temp_update = 0;
  }
  
  if (temp_update > 0 ) {
      disp.showTemperature(current_temperature);
      
  }
  else {

      if (millis() - last_update > INTERVAL_TEMP_UPDATE) {
    
        last_update = millis();
        current_temperature = rtc.getTemp();
    
        temp_update = millis();
    
      }
      disp.showHour(compiled.hour(),compiled.minute());
      
  }
  
  
  
   
//   disp.digitBits(1,number);
//   
//   disp.digitBits(2,number+1);
//
//   disp.digitBits(3,number+2);
//
//   disp.digitBits(4,number+3);
   
  
}
