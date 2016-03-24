#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "SegmentDisplay.h"

SegmentDisplay disp;


float number = 0;
unsigned long last_update = 0;
unsigned long interval_update = 500;

void setup() {  
  
  disp.start();

}

void loop() {


  if (millis() - last_update > interval_update)
  {
    number+=0.1;
    if (number>99)number = 0;
    last_update = millis();
   
  }

   disp.showFloat(number);
  
}
