/*
  Blink
  Turns on an LED on for one second, then off for one second, repeatedly.

  Most Arduinos have an on-board LED you can control. On the Uno and
  Leonardo, it is attached to digital pin 13. If you're unsure what
  pin the on-board LED is connected to on your Arduino model, check
  the documentation at http://arduino.cc

  This example code is in the public domain.

  modified 8 May 2014
  by Scott Fitzgerald
 */

const int pin_pwm = 9;
const int pin_a = 7;
const int pin_b = 8;

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin 13 as an output.
  Serial.begin(9600);
  pinMode(pin_a, OUTPUT);
  digitalWrite(pin_a, LOW);
  pinMode(pin_b, OUTPUT);
  digitalWrite(pin_b, LOW);
  pinMode(pin_pwm, OUTPUT);
  digitalWrite(pin_pwm, LOW);
  Serial.println("SETUP");
  
}
String inData = "";
bool have_line = false;
int pwmVal = 0;

// the loop function runs over and over again forever
void loop() {

  if (Serial.available()) {
      int inChar = Serial.read();
      if (inChar == 'h') {
          digitalWrite(pin_pwm, 0);
          
          digitalWrite(pin_a, HIGH);
          digitalWrite(pin_b, LOW);
          
          analogWrite(pin_pwm, pwmVal);
          
          Serial.print("A=HIGH B=LOW");
          Serial.print(" PWM=");
          Serial.println(pwmVal);
      }
      else if (inChar == 'l') {
          digitalWrite(pin_pwm, 0);
          
          digitalWrite(pin_a, LOW);
          digitalWrite(pin_b, HIGH);
          
          analogWrite(pin_pwm, pwmVal);
          Serial.print("A=LOW B=HIGH");
          Serial.print(" PWM=");
          Serial.println(pwmVal);
      }
      else if (isDigit(inChar)) {
          inData += (char)inChar; 
      }
      else if (inChar == '\n' || inChar == '\r')
      {
//        int inVal = atoi((const char*)&inChar);
//        int pwmVal = map(inVal, 0, 9, 0, 255); 
//        analogWrite(pin_name, pwmVal);
//        Serial.print(inVal);
//        Serial.print("=>");
//        Serial.println(pwmVal);
            have_line = true;
      }
  }
  

  if (have_line) {
      pwmVal = inData.toInt();
      analogWrite(pin_pwm, pwmVal);
      Serial.print("PWM=>");
      Serial.println(pwmVal, DEC);
      have_line = false;
      inData = "";
  }
  
  
//  delay(500);
//  analogWrite(pin_name, 255);
//  delay(2000);
//  analogWrite(pin_name, 128);
//  delay(2000);
//  analogWrite(pin_name, 0);
//  delay(2000);
//  digitalWrite(pin_name, LOW);
//  delay(500);
}
