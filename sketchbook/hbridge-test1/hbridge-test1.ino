#include <TimerOne.h>

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

void setup(void)
{
  Serial.begin(9600);
  while (!Serial){}
  

// 400 us = 2.5 kHz
// 800 us = 1.25 kHz
// 200 us = 5.0 kHz
  Timer1.initialize(200);

  
//  pinMode(7, OUTPUT);
//  digitalWrite(7, LOW);
//  
//  pinMode(8, OUTPUT);
//  digitalWrite(8, LOW);


  Serial.println("Setup Finished");
}

void loop(void)
{

  if (Serial.available()>0) {
    char a = Serial.read();
    
    if (a=='f') {

      if (left_motor.getSpeed()==0) {
          left_motor.drive(50);
      }
      else {
          left_motor.drive(left_motor.getSpeed());
      }

      if (right_motor.getSpeed()==0) {
          right_motor.drive(50);
      }
      else {
          right_motor.drive(right_motor.getSpeed());
      }

      left_motor.dumpState();
      right_motor.dumpState();
      
    }
    else if (a=='b') {
      
      if (left_motor.getSpeed()==0) {
          left_motor.drive(-50);
      }
      else {
          left_motor.drive(-left_motor.getSpeed());
      }
      
      if (right_motor.getSpeed()==0) {
          right_motor.drive(-50);
      }
      else {
          right_motor.drive(-right_motor.getSpeed());
      }

      left_motor.dumpState();
      right_motor.dumpState();
      
    }
    else if (a=='s') {
      //stop
      left_motor.stop();
      right_motor.stop();

      left_motor.dumpState();
      right_motor.dumpState();
      
    }
    //1-to-0 digits processed as percent speed of motor
    else {
      //duty cycle = 90 motor can not move 10 off 90 on
      //highest speed can be acheived using 0v i.e. 0 duty cycle
      //1
      int c = (int)a - 48;  
      if (c<0) c = 0;
      if (c>9) c = 9;
      if (c==0) c = 10;

      int spd = map(c, 1,10, 10,100);
      
      left_motor.drive( spd * left_motor.getDirection() );
      right_motor.drive( spd * right_motor.getDirection() );

      left_motor.dumpState();
      right_motor.dumpState();
      
    }
  }  
}

