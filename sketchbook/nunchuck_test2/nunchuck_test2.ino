#include "Wire.h"
//#include "WiiChuckClass.h" //most likely its WiiChuck.h for the rest of us.
#include "WiiChuck.h"

WiiChuck chuck = WiiChuck();

void setup() {
  //nunchuck_init();
  Serial.begin(9600);

  Wire.begin();
  chuck.begin();
  chuck.update();
  chuck.calibrateJoy();
}

void loop() {
  
  chuck.update(); 

  Serial.print("X=>");
    Serial.print(chuck.readJoyX());
  Serial.print(", Y=>");  
    Serial.print(chuck.readJoyY());
  
  Serial.print(", AX=>");  
    Serial.print(chuck.readAccelX());
  Serial.print(", AY=>");  
    Serial.print(chuck.readAccelY());
  Serial.print(", AZ=>");  
    Serial.print(chuck.readAccelZ());
  
  Serial.print(", ");  
    
  if (chuck.buttonZ) {
     Serial.print("Z");
  } else  {
     Serial.print("-");
  }

    Serial.print(", ");  

//not a function//  if (chuck.buttonC()) {
  if (chuck.buttonC) {
     Serial.print("C");
  } else  {
     Serial.print("-");
  }

    Serial.println();

}
