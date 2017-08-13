#include <SoftwareSerial.h>
#include <Wire.h>
#include "EEPROMAnything.h"

#include "WiiChuck.h"

WiiChuck chuck = WiiChuck();
//SoftwareSerial dbg(A2,A1); // RX-green, TX-white

//joystick buttons on original controller
#define JOYL_UP_P 3
#define JOYL_DN_P 4
#define JOYM_UP_P 2
#define JOYM_DN_P 5
#define JOYR_UP_P 7
#define JOYR_DN_P 6

#define JOYL_UP 0
#define JOYL_DN 1
#define JOYM_UP 2
#define JOYM_DN 3
#define JOYR_UP 4
#define JOYR_DN 5
uint8_t ctrlbtn[6] = {JOYL_UP_P, JOYL_DN_P, JOYM_UP_P, JOYM_DN_P, JOYR_UP_P, JOYR_DN_P};
boolean btnstate[6] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};

struct color {

  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t i;
  color(uint8_t cr=0, uint8_t cg=0, uint8_t cb=0, uint8_t ci=0)
  {
    r=cr;
    g=cg;
    b=cb;
    i=ci;
  }
};

typedef struct color RGBColor;

RGBColor white(255,255,255,255); 
RGBColor black(0,0,0,0);

RGBColor lights = white;

uint8_t rgb_wheel[13][3] = {
 
  {255, 0,   0},
  {255, 128, 0},
  {255, 255, 0},
  {128, 255, 0},
  {0,   255, 0},
  {0,   255, 128},
    {255, 255, 255},
  {0,   255, 255},
  {0,   128, 255},
  {0,   0,   255},
  {128, 0,   255},
  {255, 0,   255},
  {255, 0,   128}
};

const int SPD_MAX = 6;
const int SPD_MIN = 0;
const int SPD_TBO = 9;

int max_speed = SPD_MAX;
int min_speed = SPD_MIN;

#define STORE_ADDR 1

void setup()
{

  EEPROM_read(STORE_ADDR, lights);
  
  //bluetooth module connected on hwserial
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  //input pullup for buttons
  for (int a=0;a<6;a++) {
    pinMode(ctrlbtn[a], INPUT);
    digitalWrite(ctrlbtn[a], HIGH);//pullup
  }

  //join i2c bus as master
  Wire.begin();                      

  //block until first read from chuck
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


//nunchuck stick 
bool stick_turning = false;

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
  
  
  readButtons();

  //config
  if (buttonPressed(JOYM_UP) || buttonPressed(JOYM_DN)) {
      

      if (buttonPressed(JOYL_DN)) {
        sendLights(black);  
      }
      else if (buttonPressed(JOYL_UP)) {
        sendLights(lights);  
      }

      if (buttonPressed(JOYR_DN) && chuck.update()) {

          int roll = chuck.readRoll();
          if (roll < -90) roll = 90;
          if (roll > 90) roll = 90;
          
          int idx = map(roll, -90, 90, 0, 12);
          
          if (idx<0) idx=0;
          if (idx>12) idx = 12;
          
          lights.r = rgb_wheel[idx][0];
          lights.g = rgb_wheel[idx][1];
          lights.b = rgb_wheel[idx][2];
          lights.i = 255;

              
          
          sendLights(lights);

          EEPROM_write(STORE_ADDR, lights);
      }
      
      
  } //config
  
  else {
      processMove();
      
  }



}

void processMove()
{
  int spd = 0;
  int dir = 0;
  

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

    spd = map(c, 0, 80, min_speed, max_speed);


    //top vs bottom of joystick relaxed to 20
    if (abs(joyY) != joyY && abs(joyY) > 20) {
      spd = -spd;
    }

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
  
}

void sendLights(const RGBColor& clr)
{

  
  uint8_t red = map(clr.r,  0, 255, 0, 9);
  uint8_t green = map(clr.g, 0, 255, 0, 9);
  uint8_t blue = map(clr.b, 0, 255, 0, 9);
  uint8_t i = map(clr.i, 0, 255, 0, 9);
  
  String lights = ""+red+green+blue+i;
  
  //snprintf(lights_nvalue, 5, "%u%u%u%u", red, green, blue, i);

  Serial.print(F("l"));
  Serial.print(red);
  Serial.print(green);
  Serial.print(blue);
  Serial.print(i);
  Serial.flush();
  
}
void readButtons()
{
  for (int a=0;a<6;a++) {
      btnstate[a] = digitalRead(ctrlbtn[a]);  
      
  }
}

bool buttonPressed(int idx)
{
  return (btnstate[idx] == 0);  
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

void dumpButtons()
{
 Serial.print(F("JOYL_UP="));
         Serial.print(btnstate[JOYL_UP]);
         Serial.print(F(" | "));
      Serial.print(F("JOYL_DN="));
         Serial.print(btnstate[JOYL_DN]);
         Serial.print(F(" | "));
      Serial.print(F("JOYM_UP="));
         Serial.print(btnstate[JOYM_UP]);
         Serial.print(F(" | "));
      Serial.print(F("JOYM_DN="));
         Serial.print(btnstate[JOYM_DN]);
         Serial.print(F(" | "));
      Serial.print(F("JOYR_UP="));
         Serial.print(btnstate[JOYR_UP]);
         Serial.print(F(" | "));
      Serial.print(F("JOYR_DN="));
         Serial.print(btnstate[JOYR_DN]);
         Serial.println("");  
}
