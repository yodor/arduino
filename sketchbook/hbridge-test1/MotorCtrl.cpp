#include "MotorCtrl.h"
#include <Arduino.h>
#include <TimerOne.h>

MotorCtrl::MotorCtrl(int p_pwm, int p_ls, int p_rs):pwm_pin(p_pwm),ls_pin(p_ls), rs_pin(p_rs)
{
  pinMode(ls_pin, OUTPUT);
  digitalWrite(ls_pin, LOW);
  
  pinMode(rs_pin, OUTPUT);
  digitalWrite(rs_pin, LOW);

  spd = 0;
  duty = 0;
  dir = 0;//stoped 1=forward -1=backward
}
MotorCtrl::~MotorCtrl()
{}
bool MotorCtrl::isForward()
{
  return (dir == 1); 
}
bool MotorCtrl::isBackward()
{
  return (dir == -1);
}
bool MotorCtrl::isRunning()
{
  return (dir != 0);  
}
int MotorCtrl::getDirection()
{
  return dir;
}
int MotorCtrl::getSpeed()
{
  return spd;
}
void MotorCtrl::forward()
{
    if (isBackward()) {
      digitalWrite(ls_pin, LOW);
      digitalWrite(rs_pin, LOW);
      //delay(100);
    }
    digitalWrite(ls_pin, HIGH);
    digitalWrite(rs_pin, LOW);
    
    dir = 1;
    
      
}
void MotorCtrl::backward()
{
    if (isForward()) {
      digitalWrite(ls_pin, LOW);
      digitalWrite(rs_pin, LOW);
      //delay(50);
    }
    digitalWrite(ls_pin, LOW);
    digitalWrite(rs_pin, HIGH);

    dir = -1;
    
}
void MotorCtrl::stop()
{
    digitalWrite(ls_pin, LOW);
    digitalWrite(rs_pin, LOW);
    Timer1.pwm(pwm_pin, 0);
    spd = 0;
    duty = 0;
    dir = 0;
    
}
// in percent slowest 0 -> 100% fastest
void MotorCtrl::setSpeed(int val)
{
    spd = abs(val);
    if (spd>100)spd = 100;

    duty = map(spd, 0, 100, 90, 0);

    Timer1.pwm(pwm_pin, (duty / 100.0) * 1024);
    
    
}
//ATM255|255 spd from -255 to 255
void MotorCtrl::drive(int val)
{

    setSpeed(val);

    if (val>0) {

      forward();
            
    }
    else if (val<0) {

      backward();
      
    }
    else {
      stop();
    }

}
void MotorCtrl::dumpState()
{
    Serial.print(F("SPD: "));  
    Serial.print(spd);
    Serial.print(F(" | DIR: "));
    Serial.print(dir);  
    Serial.print(F(" | DUTY: "));
    Serial.println(duty);
}
