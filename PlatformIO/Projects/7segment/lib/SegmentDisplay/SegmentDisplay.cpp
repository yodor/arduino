
#include "SegmentDisplay.h"
#include <Arduino.h>

//bottom row of segment panel - left to right
const int SD_CATHODE_E  = A0;
const int SD_CATHODE_D  = A1;
const int SD_CATHODE_DP = A2;
const int SD_CATHODE_C  = A3;
const int SD_CATHODE_G  = A4;
//const int SD_CATHODE_G  A5; //not used

//top row of segment panel - right to right
const int SD_CATHODE_B      =4;
const int SD_COM_ANODE_D3   =5;
const int SD_COM_ANODE_D2   =6;
const int SD_CATHODE_F      =7;
const int SD_CATHODE_A      =8;
const int SD_COM_ANODE_D1   =9;

const bool digits[11][7] = {
  {1, 1, 1, 1, 1, 1, 0}, //0
  {0, 1, 1, 0, 0, 0, 0}, //1
  {1, 1, 0, 1, 1, 0, 1}, //2
  {1, 1, 1, 1, 0, 0, 1}, //3
  {0, 1, 1, 0, 0, 1, 1}, //4
  {1, 0, 1, 1, 0, 1, 1}, //5
  {1, 0, 1, 1, 1, 1, 1}, //6
  {1, 1, 1, 0, 0, 0, 0}, //7
  {1, 1, 1, 1, 1, 1, 1}, //8
  {1, 1, 1, 1, 0, 1, 1}, //9
  {0, 0, 0, 0, 0, 0, 0}, //off
};


const int segments[7] = { SD_CATHODE_A, SD_CATHODE_B, SD_CATHODE_C, SD_CATHODE_D, SD_CATHODE_E, SD_CATHODE_F, SD_CATHODE_G };


#define DELAY_ON 500

SegmentDisplay::SegmentDisplay()
{
  pinMode(SD_COM_ANODE_D1, OUTPUT);
  digitalWrite(SD_COM_ANODE_D1, LOW);
  pinMode(SD_COM_ANODE_D2, OUTPUT);
  digitalWrite(SD_COM_ANODE_D2, LOW);
  pinMode(SD_COM_ANODE_D3, OUTPUT);
  digitalWrite(SD_COM_ANODE_D3, LOW);
  
  pinMode(SD_CATHODE_A, OUTPUT);
  digitalWrite(SD_CATHODE_A, HIGH);
  pinMode(SD_CATHODE_B, OUTPUT);
  digitalWrite(SD_CATHODE_B, HIGH);
  pinMode(SD_CATHODE_C, OUTPUT);
  digitalWrite(SD_CATHODE_C, HIGH);
  pinMode(SD_CATHODE_D, OUTPUT);
  digitalWrite(SD_CATHODE_D, HIGH);
  pinMode(SD_CATHODE_E, OUTPUT);
  digitalWrite(SD_CATHODE_E, HIGH);
  pinMode(SD_CATHODE_F, OUTPUT);
  digitalWrite(SD_CATHODE_F, HIGH);
  pinMode(SD_CATHODE_G, OUTPUT);
  digitalWrite(SD_CATHODE_G, HIGH);
  pinMode(SD_CATHODE_DP, OUTPUT);
  digitalWrite(SD_CATHODE_DP, HIGH);
  this->running = true;
}
SegmentDisplay::~SegmentDisplay()
{

}

void SegmentDisplay::off()
{
    digitalWrite(SD_COM_ANODE_D1, LOW);
    digitalWrite(SD_COM_ANODE_D1, LOW);
    digitalWrite(SD_COM_ANODE_D1, LOW);

    for (int a=0;a<7;a++) {
      digitalWrite(segments[a], LOW);
    }

    digitalWrite(SD_CATHODE_DP, LOW);
    this->running = false;
}

void SegmentDisplay::on()
{
    this->running = true;
}

void SegmentDisplay::setValue(float number)
{

  cell[0].value = 0;
  cell[0].dot = false;
  cell[1].value = 0;
  cell[1].dot = false;
  cell[2].value = 0;
  cell[2].dot = false;
  
  //overflow
  if (number>=1000) {
    cell[0].value = 10;
    cell[0].dot = true;
    cell[1].value = 10;
    cell[1].dot = true;
    cell[2].value = 10;
    cell[2].dot = true;
  
  }
  else if (number>=100){
    cell[0].value = (int(number) % 1000) / 100;
    cell[1].value = (int(number) % 100) / 10;
    cell[2].value = (int(number) % 10);
  }
  else if (number>=10) {
    cell[0].value = (int(number) % 100) / 10;
    cell[1].value = (int(number) % 10);
    cell[1].dot = true;
    cell[2].value = (int(number * 10) % 10);
  }
  else {

    cell[0].value = (int(number) % 10);
    cell[0].dot = true;
    cell[1].value = (int(number * 10) % 10);
    cell[2].value = (int(number * 100) % 10);
  }

}

void SegmentDisplay::update(int seg_num, int dgt, bool dot)
{
  //turnoff all digits
  digitalWrite(SD_COM_ANODE_D1, LOW);
  digitalWrite(SD_COM_ANODE_D2, LOW);
  digitalWrite(SD_COM_ANODE_D3, LOW);
  
  //delayMicroseconds(DELAY_OFF);

  //set segment values
  for (int a=0;a<7;a++) {
    digitalWrite(segments[a], !digits[dgt][a]);  
  }
  digitalWrite(SD_CATHODE_DP, !dot);

  //senable specified segment
  if (seg_num==0) {
      digitalWrite(SD_COM_ANODE_D1, HIGH);
  }
  else if (seg_num==1) {
      digitalWrite(SD_COM_ANODE_D2, HIGH);
  }
  else if (seg_num==2) {
      digitalWrite(SD_COM_ANODE_D3, HIGH);
  }
  

}


void SegmentDisplay::refresh() {
  // put your main code here, to run repeatedly:
  if (!this->running) return;

  for (int a=0;a<3;a++) {
    SegmentStatus current = cell[a];
    this->update(a, current.value, current.dot);
    delayMicroseconds(DELAY_ON);
  }
 
}
