#include <TimerOne.h>



void setup() {
  // put your setup code here, to run once:
  pinMode(PWM_PIN,OUTPUT);
  pinMode(A2, OUTPUT);
  //int thisPitch = map(sensorReading, 400, 1000, 120, 1500);

  //int sensorReading = analogRead(A0);
  // print the sensor reading so you know its range
  //Serial.println(sensorReading);
  // map the analog input range (in this case, 400 - 1000 from the photoresistor)
  // to the output pitch range (120 - 1500Hz)
  // change the minimum and maximum input numbers below
  // depending on the range your sensor's giving:
  //int thisPitch = map(sensorReading, 400, 1000, 120, 1500);

  // play the pitch:
  tone(PWM_PIN, 5000);
  //analogWrite(PWM_PIN, 128);
}

void loop() {
  for (uint8_t a=0; a< 256; a++) {
    analogWrite(A2, a);  
    delay(1000);
  }
  //delay(1);        // delay in between reads for stability
}
