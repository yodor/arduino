#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "SegmentDisplay.h"

SegmentDisplay disp;

int number = 0;
unsigned long last_update = 0;
unsigned long interval_update = 1000;

void setup() {  
  
  
  
  Serial.begin(115200);
  Serial.println("Start");
  
  disp.start();
  
  Serial.println("Start1");
  
  
}
bool ps=false;

void loop() {

  
  
//  if (millis() - last_update > interval_update)
//  {
//    last_update = millis();
//    
//    ps=!ps;
//    number+=1;
//    if (number>9)number = 0;
//    
//    Serial.println(number);
//    
//    digitalWrite(LED_BUILTIN, ps);
//    
//    
//  }

   disp.digitBits(1,7,false);
   
   disp.digitBits(2,1,false);
   
  
}
