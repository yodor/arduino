#include <DallasTemperature.h>
#include <Wire.h>

#include "IRButtons.h"
#include "IRProcessor.h"
#include "AmpShared.h"

#define PIN_MUTE 2
#define PIN_TEMP 3
#define PIN_IRIN 9

const uint8_t unused_pins[] = {4, 5, 10, 11, 12, 13, A0, A1, A2, A3};

//IR receiver pin
IRProcessor irproc(PIN_IRIN);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(PIN_TEMP);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


unsigned long COUNTERS_motor_state = 0;
const int shutoff_voltage = 4000;

//volume
const uint8_t PIN_MOTOR_ENABLE = 8;
const uint8_t PIN_MOTOR_DIR_RIGHT = 7; //dirA1 = 10;
const uint8_t PIN_MOTOR_DIR_LEFT = 6; //dirA2 = 11;

//mute/power
volatile MOTOR_STATE motor_direction = STOPPED;

volatile boolean is_mute = true;
volatile boolean have_power = true;

volatile unsigned long current_temp = 0;
volatile unsigned long current_vcc = 5432;


volatile boolean initial_boot = true;



////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////

long unsigned last_update = 0;
const long unsigned interval_update = 2000;

DeviceAddress tempDeviceAddress;

#define IR_LUP      0xFF906F
#define IR_LDN      0xFFB847

void setup()
{

  //Serial.begin(9600);


  for (int a = 0; a < sizeof(unused_pins) - 1; a++) {
    pinMode(unused_pins[a], INPUT_PULLUP);
  }

  pinMode(PIN_MUTE, OUTPUT);
  setMute(true);


  //setup motor
  pinMode(PIN_MOTOR_ENABLE, OUTPUT);
  pinMode(PIN_MOTOR_DIR_RIGHT, OUTPUT);
  pinMode(PIN_MOTOR_DIR_LEFT, OUTPUT);
  resetVolume();

  irproc.enableIRIn(); // Start the receiver
  irproc.blink13(false);



  Wire.begin(ADDR_VOLUME);
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveEvent); // other device writes to us using beginTransmission (not used yet)


  Serial.println(F("Setup"));



  delay(1500);
  setMute(false);

  sensors.getAddress(tempDeviceAddress, 0);
  sensors.setResolution(tempDeviceAddress, 12);
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
}


boolean had_power_loss = false;
boolean skip_sensor = false;

void loop()
{

  while (true) {
    current_vcc = AMPSHARED.readVoltageMCU();
    if (current_vcc < shutoff_voltage) {
      setMute(true);
      resetVolume();
      delay(1500);
      had_power_loss = true;
    }
    else {
      break;
    }

  }

  if (had_power_loss == true) {
    setMute(false);
    had_power_loss = false;
  }




  if (millis() - last_update >= interval_update) {

     if (sensors.isConversionAvailable(tempDeviceAddress)) {
          float temp = sensors.getTempC(tempDeviceAddress);
          sensors.requestTemperaturesByAddress(tempDeviceAddress);
          current_temp  = temp * 10000;
     }
        
    last_update = millis();
    

  }


  irproc.process();
  
  unsigned long button = irproc.buttonPressed();
  
  //clears button released 
  unsigned long lastButton = irproc.lastButton();

  if (button == BTN_NOBUTTON) {
    if (lastButton == BTN_MUTE) {
        toggleMute();
    }
    else if (lastButton != BTN_NOBUTTON) {
        digitalWrite(PIN_MOTOR_ENABLE, LOW);
        digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
        digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
        motor_direction = STOPPED;
    }
  }
  else if ( button == BTN_VOL_UP || button == IR_LUP ) {

    digitalWrite(PIN_MOTOR_ENABLE, HIGH);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, HIGH);
    digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
    motor_direction = UP;

    //Serial.println(F("Volume UP"));
  }
  else if ( button == BTN_VOL_DOWN || button == IR_LDN ) {

    digitalWrite(PIN_MOTOR_ENABLE, HIGH);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
    digitalWrite(PIN_MOTOR_DIR_LEFT, HIGH);
    motor_direction = DOWN;

    //Serial.println(F("Volume DOWN"));
  }


}




void receiveEvent(int howMany)
{
  uint8_t i2c_command = CMD_NONE;

  if (Wire.available()) {
    i2c_command = Wire.read();
  }


  if (i2c_command == CMD_VOL_MUTE_ON) {
    if (is_mute == false) {
      setMute(true);
    }
  }
  else if (i2c_command == CMD_VOL_MUTE_OFF) {
    if (is_mute == true) {
      setMute(false);
    }
  }

}



void requestEvent()
{


  uint8_t data[10];
  data[0] = motor_direction,
  data[1] = is_mute;

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



void resetVolume()
{
  digitalWrite(PIN_MOTOR_ENABLE, LOW);
  digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
  digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
  motor_direction = STOPPED;

}





void setMute(boolean mode)
{

  if (mode == true) {

    digitalWrite(PIN_MUTE, LOW);

  }
  else if (mode == false) {

    digitalWrite(PIN_MUTE, HIGH);

  }

  is_mute = mode;

  Serial.print(F("Mute is now: "));
  Serial.println(is_mute);

}
void toggleMute()
{

  setMute(!is_mute);
  //delay(1000);

}




void printVCC()
{
  uint8_t data[10] = {
  };
  data[0] = motor_direction;
  data[1] = is_mute;


  //temp
  uint8_t buf[4];
  for (int i = 0; i < 4; i++) {
    buf[3 - i] = (uint8_t)(current_temp >> (i * 8));
  }
  memcpy(&data[2], &buf, 4);

  //vcc
  for (int i = 0; i < 4; i++) {
    buf[3 - i] = (uint8_t)(current_vcc >> (i * 8));
  }
  memcpy(&data[6], &buf, 4);

}


