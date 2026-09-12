#include <Arduino.h>

#include "max6675.h"

/* YourDuino.com Example Software Sketch
 16 character 2 line I2C Display
 NEW TYPE Marked "Arduino-IIC-LCD GY-LCD-V1"
 terry@yourduino.com */
/*-----( Import needed libraries )-----*/
#include <Wire.h>

#include <ezButton.h>

#include "DisplayDevice.h"
#include "EEPROMAnything.h"

#define FET_CTRL 10

#define BUTTON_CTRL 11

#define ADDR_CONFIG 0
#define CONFIG_CHK 1201

DisplayDevice Display = DisplayDevice();

//thermocouple
int thermoDO = 4;
int thermoCS = 5;
int thermoCLK = 6;

MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

//button debouncer
ezButton button(BUTTON_CTRL);



int selectedIndex = 0;
double tempSelect[] = {0.0, 180.0, 220.0, 300.0, 350.0, 400.0, 450.0};
const int maxSelect = 6;

char buf_temp[7];
char buf_dialed[7];

void writeConfig()
{
    EEPROM_write(ADDR_CONFIG, CONFIG_CHK);
    EEPROM_write(ADDR_CONFIG+1, selectedIndex);
}

void readConfig()
{
  int chkValue = 0;
  EEPROM_read(ADDR_CONFIG, chkValue);
  if (chkValue == CONFIG_CHK) {
    EEPROM_read(ADDR_CONFIG+1, selectedIndex);
    if (selectedIndex>maxSelect) {
      selectedIndex = 0;
    }
  }
  else {
    writeConfig();
  }

}
double currentTemp = 0.0;
// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long previousMillis = 0;        // will store last time LED was updated

// constants won't change:
const long intervalCheck = 250;           // interval at which to blink (milliseconds)

void loop()
{

  button.loop(); // MUST call the loop() function first
  //int btnState = button.getState();

  if(button.isPressed()) {
     selectedIndex ++;
     if (selectedIndex>6) {
        selectedIndex = 0;
     }
     writeConfig();
  }

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= intervalCheck) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    currentTemp = thermocouple.readCelsius();
    currentTemp/=1.9;
  }

  int dialedTemp = tempSelect[selectedIndex];

  Serial.print(currentTemp);
  Serial.print("->");
  Serial.println(dialedTemp);

  uint8_t fetValue = LOW;

  if (selectedIndex>0) {
    if ( currentTemp < (dialedTemp-10)) {
        fetValue = HIGH;
    }
    else {
        fetValue = LOW;
    }
  }
  else {
      fetValue = LOW;
  }

  digitalWrite(FET_CTRL, fetValue);

  dtostrf(currentTemp, 4, 2, buf_temp);
  snprintf(Display.getLine0(), 17, "%15s%c", buf_temp, '\1');

  if (selectedIndex>0) {

    dtostrf(dialedTemp, 4, 2, buf_dialed);
    char c = '^';
    if (fetValue == LOW) {
      c = '_';
    }
    snprintf(Display.getLine1(), 17, "%c%14s%c", c, buf_dialed, '\1');

  }
  else {
    snprintf(Display.getLine1(), 17, "%16s", "Off");
  }

  Display.print();


} // END Loop

void setup()
{
  pinMode(FET_CTRL, OUTPUT);
  digitalWrite(FET_CTRL, LOW);

  //pinMode(BUTTON_CTRL, INPUT);
  //digitalWrite(BUTTON_CTRL, HIGH);

  button.setDebounceTime(20); // set debounce time to 50 milliseconds

  Serial.begin(9600);
  Serial.println("Hello World");

  Wire.begin();
  //Wire.setClock(50000L);

  Display.init();
  Display.setContrast(128);

  Display.printData("Hello World");
  delay(1000);

  byte degSymbol[8];

  degSymbol[0] = 0b00110;
  degSymbol[1] = 0b01001;
  degSymbol[2] = 0b01001;
  degSymbol[3] = 0b00110;
  degSymbol[4] = 0b00000;
  degSymbol[5] = 0b00000;
  degSymbol[6] = 0b00000;
  degSymbol[7] = 0b00000;

  Display.createChar(1, degSymbol);
  readConfig();

}
