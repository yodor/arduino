#include <TimerOne.h>
#include <Adafruit_NeoPixel.h>

#define LIGHTS_PIN A1

Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, LIGHTS_PIN, NEO_GRB + NEO_KHZ800);

// This example creates a PWM signal with 25 kHz carrier.
//
// Arduino's analogWrite() gives you PWM output, but no control over the
// carrier frequency.  The default frequency is low, typically 490 or
// 3920 Hz.  Sometimes you may need a faster carrier frequency.
//
// The specification for 4-wire PWM fans recommends a 25 kHz frequency
// and allows 21 to 28 kHz.  The default from analogWrite() might work
// with some fans, but to follow the specification we need 25 kHz.
//
// http://www.formfactors.org/developer/specs/REV1_2_Public.pdf
//
// Connect the PWM pin to the fan's control wire (usually blue).  The
// board's ground must be connected to the fan's ground, and the fan
// needs +12 volt power from the computer or a separate power supply.

#include "MotorCtrl.h"



MotorCtrl left_motor(9, 6, 5); //inversion should be applied to dir pins for the left motor
MotorCtrl right_motor(10, 8, 7);

String cmd = "";
int pos = 0;
bool debug_enabled = false;

unsigned long int last_cmd = 0;
unsigned long auto_stop_timeout = 2000;
unsigned long last_ping = 0;

void setup(void)
{
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }


  // 400 us = 2.5 kHz
  // 800 us = 1.25 kHz
  // 200 us = 5.0 kHz
  Timer1.initialize(200);

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  
  Serial.println(F("Setup Finished"));
}


void loop(void)
{

  if (Serial.available() > 0) {
    char a = Serial.read();

    if (cmd == "m") {
      processMove(a);

    }
    else if (cmd == "l") {
      processLights(a);

    }
    else if (cmd == "d") {
      processDebug(a);
    }
    else if (cmd == "") {
      //start character of a recognized command?
      if (a == 'm') {
        cmd = a;
      }
      else if (a == 'l') {
        cmd = a;
      }
      else if (a == 'd') {
        cmd = a;
      }
      if (cmd == "") {
        processSimple(a);
      }

    }
  } // Serial.available()>0

  if ( (millis() - last_cmd) > auto_stop_timeout ) {
    if (left_motor.isRunning() || right_motor.isRunning()) {
      stopCar();
    }
  }

  if ( millis() - last_ping > 1500) {
      Serial.print("ping");  
      last_ping = millis();
  }
}

void finishCommand()
{
  //clear cmd
  cmd = ""; // command name
  pos = 0;  // position in command buffer
  last_cmd = millis();  // for autostop
}
void stopCar()
{
  //stop
  left_motor.stop();
  right_motor.stop();

  left_motor.dumpState();
  right_motor.dumpState();

  if (debug_enabled)Serial.println(F("STOP"));

  last_cmd = 0;
}
void processDebug(char a)
{
  int val = readDigit(a);
  if (val > 0) {
    debug_enabled = true;
    Serial.println(F("DEBUG ON"));
  }
  else {
    debug_enabled = false;
    Serial.println(F("DEBUG OFF"));
  }
}
void processLights(char a)
{
  static int buf[4] = {0, 0, 0, 0};

  if (pos < 4) {
    //0=> RED from 0 to 9
    //1=> GREEN from 0 to 9
    //2=> BLUE from 0 to 9
    //3=> Brightness from 0 to 9
    buf[pos] = readDigit(a);
    pos++;
  }

  if (pos == 4) {
    if (debug_enabled) Serial.print(F("LIGHTS: "));

    uint8_t red = map(buf[0],  0, 9, 0, 255);
    uint8_t green = map(buf[1], 0, 9, 0, 255);
    uint8_t blue = map(buf[2], 0, 9, 0, 255);
    uint8_t i = map(buf[3], 0, 9, 0, 255);

    strip.setPixelColor(0, strip.Color(red,green,blue)); // Moderately bright green color.

    strip.show(); // This sends the updated pixel color to the hardware.
    
    if (debug_enabled) {
      Serial.print(F("R: "));
      Serial.print(red);
      Serial.print(F("G: "));
      Serial.print(green);
      Serial.print(F("B: "));
      Serial.print(blue);
      Serial.print(F("I: "));
      Serial.println(i);
    }
    finishCommand();

  }
}
void processMove(char a)
{
  static int buf[4] = {0, 0, 0, 0};

  //execute command
  if (pos < 4) {
    //0=> left motor speed from 0 to 9
    //1=> left motor direction 0=forward 1=backward
    //2=> right motor speed from 0 to 9
    //3=> right motor direction 0=forward 1=backward
    buf[pos] = readDigit(a);
    pos++;
  }

  if (pos == 4) {

    if (debug_enabled) Serial.print(F("MOVE: "));

    if (buf[1] > 0) buf[0] = -buf[0];
    if (buf[3] > 0) buf[2] = -buf[2];

    left_motor.drive( buf[0] );
    right_motor.drive( buf[2] );

    if (debug_enabled) {
      left_motor.dumpState();
      right_motor.dumpState();
    }

    finishCommand();
  }

}
int readDigit(char a)
{
  int c = (int)a - 48;
  if (c < 0) c = 0;
  if (c > 9) c = 9;
  return c;

}
void processSimple(char a)
{
  if (a == 'f') {

    if (left_motor.getSpeed() == 0) {
      left_motor.drive(5);
    }
    else {
      left_motor.drive(left_motor.getSpeed());
    }

    if (right_motor.getSpeed() == 0) {
      right_motor.drive(5);
    }
    else {
      right_motor.drive(right_motor.getSpeed());
    }

    left_motor.dumpState();
    right_motor.dumpState();

  }
  else if (a == 'b') {

    if (left_motor.getSpeed() == 0) {
      left_motor.drive(-5);
    }
    else {
      left_motor.drive(-left_motor.getSpeed());
    }

    if (right_motor.getSpeed() == 0) {
      right_motor.drive(-5);
    }
    else {
      right_motor.drive(-right_motor.getSpeed());
    }

    left_motor.dumpState();
    right_motor.dumpState();

  }
  else if (a == 's') {
    stopCar();

  }
  //1-to-0 digits processed as percent speed of motor
  else {
    //duty cycle = 90 motor can not move 10 off 90 on
    //highest speed can be acheived using 0v pwm i.e. 0 duty cycle

    int spd = readDigit(a);

    left_motor.drive( spd * left_motor.getDirection() );
    right_motor.drive( spd * right_motor.getDirection() );

    left_motor.dumpState();
    right_motor.dumpState();

  }
}

