#include <SoftwareSerial.h>
#include <Wire.h>

#include "WiiChuck.h"

WiiChuck chuck = WiiChuck();

//SoftwareSerial dbg(A2,A1); // RX-green, TX-white


const int SPD_MAX = 6;
const int SPD_MIN = 0;
const int SPD_TBO = 9;

int max_speed = SPD_MAX;
int min_speed = SPD_MIN;

void setup()
{

  //bluetooth module
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }




  Wire.begin();                      // join i2c bus as master

  chuck.begin();

  while (!chuck.update()) {
    delay(1);
  }

  chuck.calibrateJoy();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);



  //dbg.begin(9600);

  //dbg.println(F("Finished setup"));
}
bool stop_sent = false;

String cmd = "";

char recv = 0;

bool ping_val = LOW;

bool stick_turning = false;

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

  if (chuck.update()) {

    //button z is pressed
    if (chuck.buttonZ) {
      max_speed = SPD_TBO;
    }
    else {
      max_speed = SPD_MAX;
    }



    int joyX = chuck.readJoyX();
    int joyY = chuck.readJoyY();

    //clamp to 80 max
    int c = min(sqrt((joyX * joyX)  + (joyY * joyY)), 80);

    int spd = map(c, 0, 80, min_speed, max_speed);


    //top vs bottom of joystick relaxed to 20
    if (abs(joyY) != joyY && abs(joyY) > 20) {
      spd = -spd;
    }


    int dir = 0;

    //turn using joystick
    if (stick_turning) {
      if (abs(joyX) != joyX && abs(joyX) > 20) {
        dir = -1;
      }
      else if (abs(joyX) == joyX && abs(joyX) > 20) {
        dir = 1;
      }
    }
    //turn using accelerometer
    else {
      int angle = chuck.readRoll();
      if (angle >= 20) {
        //rotated on axis out of button c to the right
        dir = 1;
      }
      else if (angle <= -20) {
        //rotated on axis out of button c to the left
        dir = -1;
      }
    }
    //end turn left/right accel


//    Serial.print(" X=");
//    Serial.print(joyX);
//    Serial.print(" Y=");
//    Serial.print(joyY);
//    Serial.print(" DIST=");
//    Serial.print(c);
//    Serial.print(" SPD=");
//    Serial.print(spd);
//    Serial.print(" DIR=");
//    Serial.print(dir);
//    Serial.println();

    moveCar(spd, dir);

  }//chuck update


  //delay(20);





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



