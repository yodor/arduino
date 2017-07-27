#include <SoftwareSerial.h>
#include <Wire.h>

#include "WiiChuck.h"

WiiChuck chuck = WiiChuck();

//SoftwareSerial dbg(A2,A1); // RX-green, TX-white


const int SPD_MAX = 7;
const int SPD_MIN = 1;
const int SPD_TBO = 10;

int max_speed = SPD_MAX;
int min_speed = SPD_MIN;


void setup()
{
  //bluetooth module
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  //dbg.begin(9600);


  Wire.begin();                      // join i2c bus as master
  //Wire.setClock(100000L);

  digitalWrite(SDA, 1);
  digitalWrite(SCL, 1);

  chuck.begin();
  chuck.update();
  chuck.calibrateJoy();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  //dbg.println(F("Finished setup"));
}
bool stop_sent = false;

String cmd = "";

char recv = 0;

bool ping_val = LOW;

void loop()
{

  if (Serial.available()) {
    recv = Serial.read();
    //dbg.write(Serial.read());

    if (cmd == "" && recv == 'p') {
      cmd += recv;
    }
    else if (cmd[0] == 'p') {
      cmd += recv;
    }
    if (cmd == "ping")
    {
      cmd = "";
      ping_val = !ping_val;
      digitalWrite(LED_BUILTIN, ping_val);
    }
    else if (cmd.length() >= 4) {
      cmd = "";
    }
  }


  //  while (dbg.available()) {
  //    Serial.write(dbg.read());
  //  }

  chuck.update();

  //button z is pressed
  if (chuck.buttonZ) {
    max_speed = SPD_TBO;
  }
  else {
    max_speed = SPD_MAX;
  }

  int joyY = chuck.readJoyY();
  int joyX = chuck.readJoyX();
  
  int dir = 0;
  //turn using joystick x axis
  if (joyX>40)dir = 1;
  if (joyX<-40) dir = -1;
  

  //turn using accelerometer
//  int angle = chuck.readRoll();
//  if (angle >= 30) {
//    //rotated on axis out of button c to the right
//    dir = 1;
//  }
//  else if (angle <= -30) {
//    //rotated on axis out of button c to the left
//    dir = -1;
//  }
  //end turn left/right accel

  
  


  int spd = map(abs(joyY), 0, 100, min_speed, max_speed);
  if (abs(joyY) != joyY) {
    spd = -spd;
  }


//  Serial.print("JoyX=");
//    Serial.print(joyX);
//  Serial.print("SPD=");
//    Serial.print(spd);
//  Serial.println();

  
  //      Serial.print(" DIR=");
  //        Serial.print(dir);
  //      Serial.print(" ANG=");
  //Serial.print("Pitch=");
  //Serial.print(chuck.readPitch());


 // int spd = 0;


//  int pitch = chuck.readPitch()-45;
//  if (pitch<0) pitch = 0;
//  if (pitch>80) pitch = 80;
//  
//  if (pitch < 20) {
//    //forward  
//    spd = map(pitch, 0, 20, max_speed , min_speed);
//    
//  }
//  else if (pitch>40) {
//    //backward  
//    spd = map(pitch, 40, 70, min_speed , max_speed);
//    spd=-spd;
//  }




  moveCar(spd, dir);

  delay(80);


}




void moveCar(int spd, int dir)
{

  if (spd > 9)spd = 9;
  if (spd < -9)spd = -9;

  if (abs(spd) < 2) {
    if (!stop_sent) {
      Serial.print(F("m0000"));
      stop_sent = true;
    }
    return;
  }

  stop_sent = false;

  int mot_spdL = spd;
  int mot_spdR = spd;



  if (dir == 1) {
    if (chuck.buttonC) {
      mot_spdR = spd * -1;
    }
    else {
      mot_spdR = 0;
    }
  }
  else if (dir == -1) {
    if (chuck.buttonC) {
      mot_spdL = spd * -1;
    }
    else {
      mot_spdL = 0;
    }
  }


  //
  Serial.print(F("m"));
  Serial.print(abs(mot_spdL));
  if (mot_spdL < 0) {
    Serial.print(F("1"));
  }
  else {
    Serial.print(F("0"));
  }

  Serial.print(abs(mot_spdR));
  if (mot_spdR < 0) {
    Serial.print(F("1"));
  }
  else {
    Serial.print(F("0"));
  }

  //delay(50);

}



