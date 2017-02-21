// Include the libraries we need
//#include <OneWire.h>
//#include <DallasTemperature.h>

// Data wire is plugged into port 2 on the Arduino
//#define ONE_WIRE_BUS A3

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
//OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
//DallasTemperature sensors(&oneWire);

#include <Wire.h>

#include <AmpShared.h>

#include "IRButtons.h"

#include <IRProcessor.h>


const uint8_t unused_pins[] = {2, 3, 4, 5, 11, 12, 13, A1, A2};

//IR receiver pin
IRProcessor irrecv(9);

const uint8_t PIN_LED = 10;
const uint8_t PIN_BTN_POWER = 6;

const uint8_t PIN_RELAY_HARD = 7;
const uint8_t PIN_RELAY_SOFT = 8;

const uint8_t PIN_12V_FET = A0;


volatile boolean power_enabled = false;
volatile boolean power_amp_enabled = false;


#define INTERVAL_POWER_START 1000

unsigned long pnlPressTime = 0;
unsigned long pnlReleaseTime = 0;

int LED_VALUE = 0;
int LED_STEP = 1;
int LED_INDEX = 0;

const uint8_t LED_VALUES[] = {254, 180, 128, 90, 64, 45, 32, 23, 16, 12, 8, 6, 4, 3, 0, 0, 0};

//temperature sensor
volatile unsigned long current_temp = 0;
volatile unsigned long current_vcc = 0;

unsigned long last_update = 0;
unsigned long interval_update = 500;

int process_release = 0;
unsigned long setup_start = 0;
bool i2c_started = false;

void setup()
{

  Serial.begin(9600);

  for (int a = 0; a < sizeof(unused_pins) - 1; a++) {
    pinMode(unused_pins[a], INPUT_PULLUP);

  }
  pinMode(PIN_LED, OUTPUT);
  analogWrite(PIN_LED, 128);

  pinMode(PIN_BTN_POWER, INPUT_PULLUP);

  pinMode(PIN_12V_FET, OUTPUT);
  digitalWrite(PIN_12V_FET, LOW);

  pinMode(PIN_RELAY_HARD, OUTPUT);
  digitalWrite(PIN_RELAY_HARD, LOW);

  pinMode(PIN_RELAY_SOFT, OUTPUT);
  digitalWrite(PIN_RELAY_SOFT, LOW);



  irrecv.enableIRIn(); // Start the receiver
  irrecv.blink13(false);


  Serial.println(F("Setup"));

  //sensors.begin();

  setup_start = millis();
  
  
}


void i2c_on()
{
  Wire.begin(ADDR_PWR);
  Wire.setClock(50000L);
  
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveEvent); // other device writes to us using beginTransmission (not used yet)

}
void i2c_off()
{
  Wire.end();
}

int pnlButtonState = LOW;

void loop()
{

  if (i2c_started == false && (millis() - setup_start > 2000)) {
      
        i2c_on();
        i2c_started = true;  
      
  }
  
  if (millis() - last_update > interval_update ) {

    if (power_enabled) {
//      sensors.requestTemperatures();
//
//      float temp = sensors.getTempCByIndex(0);
//
//      if (temp != -127)
//      {
//        current_temp = 0;
//      }
//      else {
//        current_temp = temp * 10000;
//
//        if (current_temp > 500000) { // 50.00 centigrade celsius
//          powerOff();
//          analogWrite(PIN_LED, 0);
//          Serial.println(F("Thermal shutdown"));
//          while (true)
//          {
//            delay(1000);
//          }
//        }
//      }
//
//      current_vcc = AMPSHARED.readVoltageMCU();
    }



    last_update = millis();

    //Serial.println(current_temp);
  }


  irrecv.process();

  unsigned long irButton = irrecv.buttonPressed();
  //Serial.println(irButton, HEX);
  
  bool pnlButtonPressed = false;

  int pin_state = digitalRead(PIN_BTN_POWER);
  delay(50);
  if (pin_state == digitalRead(PIN_BTN_POWER)) {
    pnlButtonPressed = (pin_state == LOW) ? true : false;
  }


  if (pnlButtonState == LOW && pnlButtonPressed) {
    pnlPressTime = millis();
    pnlButtonState = HIGH;

    Serial.print(F("press | "));
    Serial.println(process_release);
  }
  else if (pnlButtonState == HIGH && !pnlButtonPressed)
  {
    pnlReleaseTime = millis();
    pnlButtonState = LOW;
    pnlPressTime = 0;
    Serial.print(F("release | "));
    Serial.println(process_release);
  }



  if ( isIRPowerBTN(irButton) || pnlPressTime > 0) {

    if (LED_VALUE > 0) {
      LED_VALUE = 0;
    }
    else {
      LED_VALUE = 255;
    }

  }
  else {
    if (power_enabled) {
      LED_VALUE = 255;
    }
    else {

      LED_VALUE = LED_VALUES[LED_INDEX];
    }
  }

  analogWrite(PIN_LED, LED_VALUE);




  if (power_enabled == false) {
    
    if (process_release>0) {

      if ( (isIRPowerBTN(irrecv.lastButton()) && irButton == BTN_NOBUTTON) && process_release == 1) {
        process_release = false;
        Serial.println(F("Release processed"));
      }
      else if (pnlButtonState == LOW && pnlReleaseTime > 0 && process_release == 2) {
        process_release = false;
        pnlReleaseTime = 0;
        Serial.println(F("Release processed"));
      }

    }
    else {

      if (  isIRPowerBTN(irButton) && ( millis() - irrecv.buttonPressTime() ) >= INTERVAL_POWER_START ) {
        powerOn();
        process_release = 1;
        
      }
      if (  pnlButtonState == HIGH && ( millis() - pnlPressTime) >= INTERVAL_POWER_START ) {
        powerOn();
        process_release = 2;
      }

      
    }

    //update led fade index
    delay(40);
    LED_INDEX += LED_STEP;

    if ( LED_INDEX > 16 ) {
      LED_STEP = -LED_STEP;
      LED_INDEX = 16;
    }
    else if ( LED_INDEX < 0 ) {
      LED_STEP = -LED_STEP;
      LED_INDEX = 0;
    }

  }
  else {

    if (process_release > 0) {
      if ( isIRPowerBTN(irrecv.lastButton()) && irButton == BTN_NOBUTTON && process_release == 1) {
        Serial.println(F("Release processed"));
        process_release = 0;
      }
      else if (pnlButtonState == LOW && pnlReleaseTime > 0 && process_release == 2) {
        process_release = 0;
        pnlReleaseTime = 0;
        Serial.println(F("Release processed"));
      }
    }
    else {
      if ( isIRPowerBTN(irButton) || pnlButtonState == HIGH) {
        powerOff();
        process_release = 1;
      }
      else if ( pnlButtonState == HIGH ) {
        powerOff();
        process_release = 2;
      }
    }


  }



}

bool isIRPowerBTN(unsigned long value)
{
    if (value == BTN_POWER || value == PANA_BTN_POWER) {
        
        return true;
    }
    else {
        
        return false;
    }
    
}

void powerOnAmp()
{
  power_amp_enabled = true;
  digitalWrite(PIN_RELAY_HARD, HIGH);
  delay(300);
  digitalWrite(PIN_RELAY_SOFT, HIGH);
  Serial.println(F("Amp ON"));
  
  
}

void powerOn()
{
  power_enabled = true;

  powerOnAmp();

  digitalWrite(PIN_12V_FET, HIGH);

  analogWrite(PIN_LED, 255);

  Serial.println(F("Full ON"));

  //i2c_on();

  delay(2000);

}

void powerOffAmp()
{
  digitalWrite(PIN_RELAY_HARD, LOW);
  digitalWrite(PIN_RELAY_SOFT, LOW);
  Serial.println(F("Amp OFF"));
  power_amp_enabled = false;
}

void powerOff()
{
  uint8_t cmd = (uint8_t)(CMD_VOL_MUTE_ON);
  Wire.beginTransmission(ADDR_VOLUME);
  Wire.write(cmd);
  Wire.endTransmission();
  analogWrite(PIN_LED, 0);

  delay(1000);

  powerOffAmp();

  digitalWrite(PIN_12V_FET, LOW);

  power_enabled = false;

  Serial.println(F("Full OFF"));

  //i2c_off();

  delay(2000);
}

// respond to commands from master
// master use Wire.beginTransmission(ADDR_PWR)
// Wire.write(I2C_COMMAND);
// Wire.endTransmission();
void receiveEvent(int howMany)
{

  uint8_t i2c_command = CMD_NONE;

  if (Wire.available()) {
    i2c_command = Wire.read();
  }


  if (i2c_command == CMD_PWR_FULL_ON) {
    if (power_enabled == false) {
      powerOn();
    }
  }
  else if (i2c_command == CMD_PWR_FULL_OFF) {
    if (power_enabled == true) {
      powerOff();
    }
  }
  else if (i2c_command == CMD_PWR_AMP_ON) {
    if (power_amp_enabled == false) {
      if (power_enabled == true) {
        powerOnAmp();
      }
      else {
        powerOn();
      }
    }
  }
  else if (i2c_command == CMD_PWR_AMP_OFF) {
    if (power_amp_enabled == true) {
      powerOffAmp();
    }
  }

}


//return temperature and vcc and power state data
//master device should use Wire.requestFrom(ADDR_PWR, 10);
void requestEvent()
{

  uint8_t data[10];
  data[0] = power_enabled;
  data[1] = power_amp_enabled;

  //temp
  uint8_t buf[4];
  memset(&buf, 0, 4);
  AMPSHARED.writeFloat(current_temp, buf);
  memcpy(&data[2], &buf, 4);

  //vcc
  memset(&buf, 0, 4);
  AMPSHARED.writeFloat(current_vcc, buf);
  memcpy(&data[6], &buf, 4);

  Wire.write(data, 10);

}


