

// Example 48.2 - tronixstuff.com/tutorials > chapter 48 - 30 Jan 2013 
// MSGEQ7 spectrum analyser shield and I2C LCD from akafugu
// for akafugu I2C LCD
#include "Wire.h"
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x20,16,2);

// create custom characters for LCD
byte level0[8] = { 
  0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111};
byte level1[8] = { 
  0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111};
byte level2[8] = { 
  0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111, 0b11111};
byte level3[8] = { 
  0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111, 0b11111, 0b11111};
byte level4[8] = { 
  0b00000, 0b00000, 0b00000, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111};
byte level5[8] = { 
  0b00000, 0b00000, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111};
byte level6[8] = { 
  0b00000, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111};
byte level7[8] = { 
  0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111};
int strobe = 6; // strobe pins on digital 4
int res = 7; // reset pins on digital 5
int left[7]; // store band values in these arrays
int right[7];
int band;
void setup()
{
  Serial.begin(9600);
  // setup LCD and custom characters
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.createChar(0,level0);
  lcd.createChar(1,level1);
  lcd.createChar(2,level2);
  lcd.createChar(3,level3);
  lcd.createChar(4,level4);
  lcd.createChar(5,level5);
  lcd.createChar(6,level6);
  lcd.createChar(7,level7);
  lcd.setCursor(0,1);
  lcd.print("Left");
  lcd.setCursor(11,1);
  lcd.print("Right");
  pinMode(res, OUTPUT); // reset
  pinMode(strobe, OUTPUT); // strobe
  digitalWrite(res,LOW); // reset low
  digitalWrite(strobe,HIGH); //pin 5 is RESET on the shield
  
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  
  digitalWrite(2, HIGH);
  digitalWrite(3, LOW);
  digitalWrite(4, LOW);
  digitalWrite(5, LOW);
  
}

void readMSGEQ7()
// Function to read 7 band equalizers
{
  digitalWrite(res, HIGH);
  digitalWrite(res, LOW);
  for( band = 0; band < 7; band++ )
  {
    digitalWrite(strobe,LOW); // strobe pin on the shield - kicks the IC up to the next band 
    delayMicroseconds(33); // 
    left[band] = analogRead(0); // store left band reading
    //right[band] = analogRead(1); // ... and the right
    digitalWrite(strobe,HIGH); 
  }
}

void loop()
{
  readMSGEQ7();
  // display values of left channel on LCD
  for( band = 0; band < 7; band++ )
  {
    lcd.setCursor(band,0);
  int val = left[band]  / 146;
  if (val>7) val = 7;
  if (val<0) val = 0;
  lcd.write(val);
  
    if (left[band]>=895) { 
      lcd.write(7); 
    } 
    else
      if (left[band]>=767) { 
        lcd.write(6); 
      } 
      else
        if (left[band]>=639) { 
          lcd.write(5); 
        } 
        else
          if (left[band]>=511) { 
            lcd.write(4); 
          } 
          else
            if (left[band]>=383) { 
              lcd.write(3); 
            } 
            else
              if (left[band]>=255) { 
                lcd.write(2); 
              } 
              else
                if (left[band]>=127) { 
                  lcd.write(1); 
                } 
                else
                  if (left[band]>=0) { 
                    lcd.write(0); 
                  }
  }

}

