/*
  Blink
  Turns on an LED on for one second, then off for one second, repeatedly.
 
  This example code is in the public domain.
 */
 
// Pin 13 has an LED connected on most Arduino boards.
// give it a name:
int PIN_LED = 9;
const int LED_PWM_PERIOD = 3500;

// the setup routine runs once when you press reset:
void setup() {                
  // initialize the digital pin as an output.
  pinMode(PIN_LED, OUTPUT);     
}

// the loop routine runs over and over again forever:
void loop() {
  int value = 128+127*cos(2*PI/LED_PWM_PERIOD*millis());
  analogWrite(PIN_LED, value);            // wait for a second
}
