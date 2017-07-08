/*
  Blink
  Turns on an LED on for one second, then off for one second, repeatedly.

  Most Arduinos have an on-board LED you can control. On the Uno and
  Leonardo, it is attached to digital pin 13. If you're unsure what
  pin the on-board LED is connected to on your Arduino model, check
  the documentation at http://www.arduino.cc

  This example code is in the public domain.

  modified 8 May 2014
  by Scott Fitzgerald
 */
#include <SoftwareSerial.h>
#include <Wire.h>
const int pin_blink = 13;

// the setup function runs once when you press reset or power the board
void setup() {
  
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  
  // initialize digital pin 13 as an output.
  pinMode(pin_blink, OUTPUT);
}
int a=0;

// the loop function runs over and over again forever
void loop() {
  digitalWrite(pin_blink, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);              // wait for a second
  digitalWrite(pin_blink, LOW);    // turn the LED off by making the voltage LOW
  delay(1000); // wait for a second
  Serial.println(++a);
  
}
