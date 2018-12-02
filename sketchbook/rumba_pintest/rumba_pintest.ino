#include "pins_rumba.h"
int test_pins[] = {
  ORIG_X_STEP_PIN,
  ORIG_Y_STEP_PIN,
  ORIG_Z_STEP_PIN
  

    
};
int len = 0;

void setup() {
  len = sizeof(test_pins)/sizeof(int);
  // put your setup code here, to run once:
  for (int a=0;a<len;a++) {
    pinMode(test_pins[a], OUTPUT);
    digitalWrite(test_pins[a],LOW);
  }
  
  Serial.begin(115200);
  Serial.print("Num test pins=");
  Serial.print(len);
}

int a=0;
void loop() {
  
  // put your main code here, to run repeatedly:
  
  Serial.print("PIN=>");
  
  Serial.println(test_pins[a]);

  while(Serial.available() < 1) {}
  char c = Serial.read();
  
  analogWrite(test_pins[a], 255);
  delay(5000);
  digitalWrite(test_pins[a], 127);
  delay(5000);
  a++;
  if (a>=len) a = 0;
  
  
}
