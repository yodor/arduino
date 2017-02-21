#include <OneWire.h>

#include "DS18B20.h"
// DS18S20 Temperature chip i/o
//OneWire ds(10);  // on pin 10
DS18B20 sensor;
void setup(void) {
  // initialize inputs/outputs
  // start serial port
  Serial.begin(9600);
  Serial.println("SETUP");
  
  if (sensor.haveDevice()) {
      uint8_t sensorType = sensor.deviceType();
      if (sensorType == 0x10) {
        Serial.println("DS18S20");
      }
      else if (sensorType = 0x28) {
        Serial.println("DS18B20");      
      }
   }
   else {
        Serial.println("No device found");         
   }
   Serial.println("SETUP DONE");
}


void loop(void) 
{
   if (sensor.haveDevice()) {
     sensor.prepareRead();
     
     float temp;
      if (sensor.readTemp(temp)) {
         Serial.println(temp); 
      }
     
   }
}


