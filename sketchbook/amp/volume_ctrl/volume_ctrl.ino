#include <DallasTemperature.h>
#include <Wire.h>
#include <IRremote.h>
#include "IRButtons.h"
#include "SharedDefs.h"

const uint8_t PIN_MUTE = 2;
const uint8_t PIN_TEMP = 3;

const uint8_t unused_pins[] = {4, 5, 10, 11, 12, 13, A0, A1, A2, A3};

//IR receiver pin
IRrecv irrecv(9);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(PIN_TEMP);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


const unsigned long motor_stop_delay = 70;
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


//ir status
long unsigned int last_button = 0; // code of last pressed IR button
boolean is_repeat = false; //IR button is repeating code

////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////

long unsigned last_update = 0;
const long unsigned interval_update = 1000;




void setup()
{

  Serial.begin(9600);


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

  irrecv.enableIRIn(); // Start the receiver
  irrecv.blink13(false);



  Wire.begin(ADDR_VOLUME);
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveEvent); // other device writes to us using beginTransmission (not used yet)


  Serial.println(F("Setup"));



  delay(1500);
  setMute(false);

  sensors.begin();
}

decode_results results;
boolean had_power_loss = false;
boolean skip_sensor = false;

void loop()
{

  while (true) {
    checkVCC();
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




  if (millis() - last_update > interval_update) {

    sensors.requestTemperatures(); // Send the command to get temperatures

    float temp = sensors.getTempCByIndex(0);

    current_temp  = temp * 10000;

    last_update = millis();
    //Serial.println(current_temp);

  }

  if (irrecv.decode(&results)) {

    processIRButton(results.value);

    irrecv.resume();

  }

  processMotor();

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
  for (int i = 0; i < 4; i++) {
    buf[3 - i] = (uint8_t)(current_temp >> (i * 8));
  }
  memcpy(&data[2], &buf, 4);

  //vcc
  for (int i = 0; i < 4; i++) {
    buf[3 - i] = (uint8_t)(current_vcc >> (i * 8));
  }
  memcpy(&data[6], &buf, 4);

  Wire.write(data, 10);

}

void processMotor()
{

  if ( motor_direction == UP ) {

    digitalWrite(PIN_MOTOR_ENABLE, HIGH);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, HIGH);
    digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
    if (COUNTERS_motor_state == 0) {
      COUNTERS_motor_state = millis();
    }
  }
  else if ( motor_direction == DOWN ) {

    digitalWrite(PIN_MOTOR_ENABLE, HIGH);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
    digitalWrite(PIN_MOTOR_DIR_LEFT, HIGH);
    if (COUNTERS_motor_state == 0) {
      COUNTERS_motor_state = millis();
    }
  }
  else if (motor_direction == STOPPING) {
    digitalWrite(PIN_MOTOR_ENABLE, LOW);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
    digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
    motor_direction = STOPPED;
    COUNTERS_motor_state = 0;
  }

  if (millis() - COUNTERS_motor_state > motor_stop_delay) {

    if ( motor_direction == UP || motor_direction == DOWN ) {
      motor_direction = STOPPING;
      COUNTERS_motor_state = 0;

    }

  }


}


void resetVolume()
{
  digitalWrite(PIN_MOTOR_ENABLE, LOW);
  digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
  digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
  motor_direction = STOPPED;

}



void processIRButton( long  unsigned int current )
{

  if (current == BTN_REPEAT) {
    current = last_button;
    is_repeat = true;
  }
  else {
    is_repeat = false;

  }

  last_button = current;



  switch (current) {


    case BTN_MUTE:
      if (is_repeat == false) {
        toggleMute();
      }
      break;

    case BTN_VOL_DOWN:
      motor_direction = DOWN;
      Serial.println(F("Vol DN: "));
      break;

    case BTN_VOL_UP:
      motor_direction = UP;
      Serial.println(F("Vol UP: "));
      break;

  }//switch



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



void checkVCC() {
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  result = ADCL;
  result |= ADCH << 8;
  result = 1126400L / result; // Back-calculate AVcc in mV

  current_vcc = result;


}

void readTempInternal() {
  long result;
  // Read temperature sensor against 1.1V reference
  ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX3);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  result = ADCL;
  result |= ADCH << 8;
  result = (result - 125) * 1075;
  result += 50000;
  current_temp = result;

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


