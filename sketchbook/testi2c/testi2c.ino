#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define  ADDR_VOLUME                 0x9

uint8_t volbuf[2];

enum MOTOR_STATE {
  UP = -1,
  DOWN = 1,
  STOPPING = 0,
  STOPPED = -2
};

LiquidCrystal_I2C lcd(0x20, 16,2);

void setup()
{
  Wire.begin();
  Serial.begin(9600);
  lcd.init();
    lcd.backlight();
    lcd.cursor();
    lcd.blink();
    lcd.setCursor(0,0);
    lcd.print("Line1");
    lcd.setCursor(0,1);
    lcd.print("Line2");
    lcd.setCursor(0,1);
}
void loop()
{
   
    
   
    
    
    
    
}
