// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS A3

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

#include <Wire.h>  
#include <IRremote.h>
#include "IRButtons.h"
#include "SharedDefs.h"


const uint8_t unused_pins[] = {2,3,4,5,11,12,13,A1,A2};
 
//IR receiver pin
IRrecv irrecv(9);

const uint8_t PIN_LED = 10;
const uint8_t PIN_BTN_POWER = 6;

const uint8_t PIN_RELAY_HARD = 7;
const uint8_t PIN_RELAY_SOFT = 8;

const uint8_t PIN_12V_FET = A0;


unsigned long ir_last = 0; // code of last pressed IR button

decode_results results;
volatile boolean power_enabled = false;
volatile boolean power_amp_enabled = false;


int interval_power_start = 2000;
unsigned long btn_press_time_panel = 0;
unsigned long btn_press_time_ir = 0;

int LED_VALUE = 0;
int LED_STEP = 1;
int LED_INDEX = 0;
const uint8_t LED_VALUES[] = {255, 180, 128, 90, 64, 45, 32, 23, 16, 12, 8, 6, 4, 3, 0, 0, 0};

//temperature sensor
volatile unsigned long current_temp = 0;
volatile unsigned long current_vcc = 0;
unsigned long last_update = 0;
unsigned long interval_update = 1000;

void setup()
{

  Serial.begin(9600);

  for (int a=0;a<sizeof(unused_pins)-1;a++){
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
  
  sensors.begin();
 
}


void i2c_on()
{
  Wire.begin(ADDR_PWR);
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveEvent); // other device writes to us using beginTransmission (not used yet)
  Wire.setClock(50000L);
}
void i2c_off()
{
  Wire.end();  
}

void loop()
{

  if (millis() - last_update > interval_update ) {

    if (power_enabled) {
      sensors.requestTemperatures();
        
      float temp = sensors.getTempCByIndex(0);
  
      if (temp != -127)
      {
        current_temp = 0;
      }
      else {
        current_temp = temp * 10000;
       
        if (current_temp > 500000) { // 50.00 centigrade celsius
            powerOff();
            Serial.println(F("Thermal shutdown"));
            while(true)
            {
              delay(1000);
            }
        }
      }

      checkVCC();
    }
    
    
  
    last_update = millis();

    //Serial.println(current_temp);
  }
  
  boolean ir_repeat = false;
  
  if (irrecv.decode(&results)) {

  	if (results.value == BTN_REPEAT ) {
  	  ir_repeat = true;
  	  if (ir_last == BTN_POWER) {
    		if (btn_press_time_ir == 0) {
    		  btn_press_time_ir = millis();
    		}
  	  }
  	  else {
  		  btn_press_time_ir = 0;
  	  }
  	  
  	}
  	else {
  	  ir_last = results.value;
  	  btn_press_time_ir = 0;
  	}

    irrecv.resume();

  }

  int btn_state = digitalRead(PIN_BTN_POWER);
  if (btn_state == LOW) {
	  if (btn_press_time_panel == 0) btn_press_time_panel = millis();
  }
  else {
	  btn_press_time_panel = 0;
  }


  if ( (btn_press_time_ir > 0 && ir_repeat)  || btn_press_time_panel>0) {
	
	  if (LED_VALUE>0) {
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

  
  if (power_enabled==false) {
    delay(40);
    LED_INDEX+=LED_STEP;
  
    if ( LED_INDEX > 16 ) {
        LED_STEP = -LED_STEP;
        LED_INDEX = 16;
    }
    else if ( LED_INDEX < 0 ) {
        LED_STEP = -LED_STEP;
        LED_INDEX = 0;
    }
  }
  
  if ( (ir_last == BTN_POWER && ir_repeat==false) || btn_press_time_panel>0) {
	  if (power_enabled == true) {
		
		resetIR();
		powerOff();
		
	  }
  }
  
  boolean process_power = false;
  
  if (btn_press_time_ir > 0 && (millis() - btn_press_time_ir > interval_power_start)) {
	  process_power = true;
  }
  else if (btn_press_time_panel > 0 && (millis() - btn_press_time_panel > interval_power_start)) {
	  process_power = true;
  }
  
  if (process_power && power_enabled == false) {
	  
	  resetIR();
	  powerOn();
	  
  }
  

 
}

void resetIR()
{
	btn_press_time_ir = 0;
	btn_press_time_panel = 0;
	ir_last = 0;
  
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

  i2c_on();
  
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

  i2c_off();
  
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
  for(int i= 0; i < 4; i++){  
    buf[3 - i] = (uint8_t)(current_temp >> (i * 8)); 
  }  
  memcpy(&data[2], &buf, 4);

  //vcc
  for(int i= 0; i < 4; i++){  
    buf[3 - i] = (uint8_t)(current_vcc >> (i * 8)); 
  }  
  memcpy(&data[6], &buf, 4);

  Wire.write(data, 10);
  
}

void checkVCC() {
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV

  current_vcc = result;

}

