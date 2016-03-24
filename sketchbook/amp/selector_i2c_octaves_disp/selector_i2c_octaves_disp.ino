
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif


#include <Time.h>  
#include <Wire.h>  
#include <DS1307RTC.h>  // a basic DS1307 library that returns time as a time_t

#include <LiquidCrystal_I2C.h>

#include <IRremote.h>

#include "IRButtons.h"

#include <EEPROM.h>

#include "SharedDefs.h"

int command_request = CMD_NONE;

#define OCTAVE 1
#define FHT_N 128
#define OCT_NORM 1

#include <FHT.h>


const int PIN_AMP_POWER  = A1;
const int PIN_BUS_POWER = 12;

//IR_DEBUG_LED
const int PIN_VU_LED = 13;


//IR receiver pin
const int PIN_IR = 4;
IRrecv irrecv(PIN_IR);


const unsigned long interval_power_up = 2000;

const unsigned long interval_clock_refresh = 60000;
const unsigned long interval_signal_refresh = 1000;
//const unsigned long interval_fft_refresh = 20;

const unsigned long button_click_delay = 150;
const unsigned long menu_toggle_delay = 3000;
const unsigned long motor_stop_delay = 250;


unsigned long button_state[] = {  
  0, 0, 0, 0 };

boolean power_enabled = true;
boolean is_mute = false;

//volume
const int PIN_MOTOR_ENABLE = 9;
const int PIN_MOTOR_DIR_RIGHT = 10; //dirA1 = 10;
const int PIN_MOTOR_DIR_LEFT = 11; //dirA2 = 11;

enum MOTOR_STATE {
  UP = -1,
  DOWN = 1,
  STOPPING = 0,
  STOPPED = -2
};
MOTOR_STATE motor_direction = STOPPED;


//ir status
long unsigned int last_button = -1; // code of last pressed IR button
boolean is_repeat = false; //IR button is repeating code


//display
byte bar[8] = { 
  0,0,0,0,0,0,0,0 };


//menu

//normal buttons operation - pressing button on selector changes channel
//buttons act as menu keys 1 - up, 2 - down, 3 - enter/yes/ok, 4 - back/cancel/no
//Channel selector buttons
const int BTN_CH1 = 5; 
const int BTN_CH2 = 6; 
const int BTN_CH3 = 7; 
const int BTN_CH4 = 8; 
//
int button_pins[] = { 
  BTN_CH1, BTN_CH2, BTN_CH3, BTN_CH4 };

const char *button_names[]  PROGMEM = {
  "Up", "Down", "Enter", "Cancel"
};



#include "SelectorConfig.h"
#include "DisplayDevice.h"
#include "SignalSampler.h"

#include "ChannelSelector.h"



////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////


double average = 0;

double fft_sum = 0;

const int numReadings = 10;

double readings[numReadings];      // the readings from the analog input
int index = 0;                  // the index of the current reading
double total = 0;                  // the running total


void setup()
{

  //prescaler 4
  //cbi(ADCSRA,ADPS2);
  //sbi(ADCSRA,ADPS1);
  //cbi(ADCSRA,ADPS0);

  //prescaler 8 default for 16Mhz?
  //cbi(ADCSRA,ADPS2);
  //sbi(ADCSRA,ADPS1);
  //sbi(ADCSRA,ADPS0);

  //prescaler 16 for 8Mhz => 8000000 / 16 = 500000 / 13adc cycles = 38400
  sbi(ADCSRA,ADPS2);
  cbi(ADCSRA,ADPS1);
  cbi(ADCSRA,ADPS0);

  //prescaler 32
  //sbi(ADCSRA,ADPS2);
  //cbi(ADCSRA,ADPS1);
  //sbi(ADCSRA,ADPS0);

  //TIMSK0 = 0; // turn off timer0 for lower jitter - turns off delay too
  //ADCSRA = 0xe5; // set the adc to free running mode?
  //ADMUX = 0x40; // use adc0
  //DIDR0 = 0x01; // turn off the digital input for adc0

    //Serial.begin(4800);
  Serial.println(F("Setup"));


  analogWrite(PIN_ONBOARD_LED, 254);    

  //setup motor
  pinMode(PIN_MOTOR_ENABLE, OUTPUT);
  digitalWrite(PIN_MOTOR_ENABLE, LOW);
  pinMode(PIN_MOTOR_DIR_RIGHT, OUTPUT);
  digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
  pinMode(PIN_MOTOR_DIR_LEFT, OUTPUT);
  digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);

  //bus power enabled (NPN)
  pinMode(PIN_BUS_POWER, OUTPUT);
  digitalWrite(PIN_BUS_POWER, LOW);
  pinMode(PIN_VU_LED, OUTPUT);
  digitalWrite(PIN_VU_LED, LOW);
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  analogWrite(PIN_ONBOARD_LED, 512);

  pinMode(BTN_CH4, INPUT_PULLUP);
  //digitalWrite(BTN_CH4, HIGH);
  pinMode(BTN_CH3, INPUT_PULLUP);
  //digitalWrite(BTN_CH3, HIGH);
  pinMode(BTN_CH2, INPUT_PULLUP);
  //digitalWrite(BTN_CH2, HIGH);
  pinMode(BTN_CH1, INPUT_PULLUP);
  //digitalWrite(BTN_CH1, HIGH);

  setBusPower(true);

  pinMode(PIN_AMP_POWER, OUTPUT);
  //amp power => on
  digitalWrite(PIN_AMP_POWER, HIGH);

  irrecv.enableIRIn(); // Start the receiver
  irrecv.blink13(false); 

  Wire.begin(CONTROLLER_ADDRESS);
  //Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveCommand); // other device writes to us using beginTransmission

  MDISPLAY.init();

  loadChars();

  MDISPLAY.backlight();

  CONFIG.load();


  MDISPLAY.updateClock();

  CHANNEL stored_channel = CHANNEL(CONFIG.values().stored_channel);

  if (stored_channel>CHANNEL_NONE && stored_channel<CHANNEL_LAST) {
    SELECTOR.setActiveChannel(stored_channel);
  }
  else {
    SELECTOR.setActiveChannel(CH1);
  }

  clearButtonStates();

  analogWrite(PIN_ONBOARD_LED, 0); 

  for (int thisReading = 0; thisReading < numReadings; thisReading++) {
    readings[thisReading] = 0.0;    
  }
}

decode_results results;
PANEL_BUTTON menu_toggle_button = BUTTON_NONE;
boolean menu_toggle = false;


int cnt = 0;


void loop()
{

  long unsigned current_millis = 0;

  while(1) {

    current_millis = millis();

    if (irrecv.decode(&results)) {

      COUNTERS.sleep_signal = 0;

      if (CONFIG.values().pulse_led_ir_enabled) {

        analogWrite(PIN_ONBOARD_LED, 254);

      }

      processIRButton(results.value);

      irrecv.resume(); 

      if (CONFIG.values().pulse_led_ir_enabled) {

        if (!power_enabled)delay(30);

        analogWrite(PIN_ONBOARD_LED, 0);

      }

    }



    PANEL_BUTTON button_pressed = processPanelButtons();

    if (menu_toggle_button != BUTTON_NONE && menu_toggle == true && power_enabled) {

      COUNTERS.sleep_signal = 0; 

      MDISPLAY.changeDisplayMode(DisplayDevice::MODE_MENU);      
      menu_toggle = false;

    }
    else if (button_pressed>BUTTON_NONE && button_pressed<BUTTON_LAST) {

      COUNTERS.sleep_signal = 0;

      if (!power_enabled) {
        powerOn();
      }
      else if (power_enabled) {
        if (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) {
          SELECTOR.setActiveChannel(CHANNEL(button_pressed));
        }
        else if (MDISPLAY.currentMode() == DisplayDevice::MODE_MENU) {
          MDISPLAY.processButton(button_pressed);
        }
      }

    }  


    if (!power_enabled) continue;


    processMotor(current_millis);





    calculateFFT();

    SAMPLER.process(PIN_AUDIO_MONITOR, CONFIG.values().sample_window);

    total= total - readings[index];         
    // read from the sensor:  
    readings[index] = fft_sum;
    // add the reading to the total:
    total= total + readings[index];       
    // advance to the next position in the array:  
    index = index + 1;                    

    // calculate the average:
    average = total / numReadings;        

    // if we're at the end of the array...
    if (index >= numReadings) {
      // ...wrap around to the beginning: 
      index = 0;                               

    }


    if (current_millis - COUNTERS.clock_update >= interval_clock_refresh) {
      MDISPLAY.updateClock();
      COUNTERS.clock_update = current_millis;
    }

    if (current_millis - COUNTERS.signal_update >= interval_signal_refresh) {

      cnt++;

      //numReadings * interval_signal_refresh delay for enabling the timeout 
      if (cnt>10) {
        cnt = 0;
        if (average<=2.8) {
          if (COUNTERS.sleep_signal == 0) {
            COUNTERS.sleep_signal = millis();
          }
        }

      }   



      MDISPLAY.updateSignal(average, fft_sum);

      COUNTERS.signal_update = current_millis;

    }

    if (SAMPLER.getDBV() > CONFIG.values().sleep_toggle_dbV )  {
      COUNTERS.sleep_signal = 0;
    }


    if (current_millis - COUNTERS.fft_update >= CONFIG.values().fft_refresh) {

      if (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) { 
        renderFFT();
      }
      COUNTERS.fft_update = current_millis;

    }


    if ((COUNTERS.sleep_signal > 0 && COUNTERS.sleep_signal<current_millis) && (current_millis - COUNTERS.sleep_signal) >= CONFIG.values().signal_sleep_timeout ) {

      COUNTERS.sleep_signal = 0;  

      if (CONFIG.values().signal_sleep_enabled == true) {
        powerOff();
        return;
      }

    }

    MDISPLAY.processLoop();





  } // while (1)

}

//when other write to us using beignTransmission
void receiveCommand(int howMany)
{

  int cmd = CMD_NONE;

  while (Wire.available() > 0){
    cmd = Wire.read();

  }

  if (command_request == CMD_NONE) {
    command_request = cmd;
  }


}
void processMotor(unsigned long current_millis)
{

  if ( motor_direction == UP ) {
    //dbgOut("High Low");
    digitalWrite(PIN_MOTOR_ENABLE, HIGH);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, HIGH);
    digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
    if (COUNTERS.motor_state == 0) {
      COUNTERS.motor_state = current_millis;
      MDISPLAY.showMessage(F2("Volume Up"));
    }
  }
  else if ( motor_direction == DOWN ) {
    //dbgOut("Low High");
    digitalWrite(PIN_MOTOR_ENABLE, HIGH);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
    digitalWrite(PIN_MOTOR_DIR_LEFT, HIGH);
    if (COUNTERS.motor_state == 0) {
      COUNTERS.motor_state = current_millis;
      MDISPLAY.showMessage(F2("Volume Down"));

    }
  }

  if (motor_direction == STOPPING) {
    //dbgOut("Low Low");
    digitalWrite(PIN_MOTOR_ENABLE, LOW);
    digitalWrite(PIN_MOTOR_DIR_RIGHT, LOW);
    digitalWrite(PIN_MOTOR_DIR_LEFT, LOW);
    COUNTERS.motor_state = 0;
    motor_direction = STOPPED;
  }

  if (motor_direction != STOPPED && COUNTERS.motor_state > 0 && current_millis - COUNTERS.motor_state > motor_stop_delay) {
    motor_direction = STOPPING;
  }



}

PANEL_BUTTON processPanelButtons()
{

  PANEL_BUTTON menu_button = BUTTON_NONE;

  for (int a=0; a < 4; a++) {

    PANEL_BUTTON current_button = PANEL_BUTTON(a);
    unsigned long last_press = button_state[a];

    int button_pin = button_pins[a];
    int current_state = digitalRead(button_pin);

    //button is pressed
    if (current_state == LOW) {

      //button was already pressed
      if (last_press > 0) {
        if (menu_toggle_button != current_button) {
          //button was pressed for 3sec - send a toggle menu command
          if ( (millis() - last_press > menu_toggle_delay) && (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) ) {
            menu_toggle_button = current_button;
            menu_toggle = true;
            break;
          }
        }
      }
      else {
        button_state[a] = millis();  
      }


    }
    //default state is HIGH
    else if (current_state == HIGH) {

      if (menu_toggle_button == current_button) {

        menu_toggle_button = BUTTON_NONE;
        menu_button = BUTTON_NONE;
        menu_toggle = false;
        clearButtonStates();
        break;

      }
      else if (last_press > 0) {
        if (millis() - last_press > button_click_delay) {
          menu_button = current_button;
          clearButtonStates();
          break;
        }

      }


    }

  }

  return menu_button;


}

void powerOn()
{
  setBusPower(true);

  MDISPLAY.init();

  loadChars();

  MDISPLAY.display();
  MDISPLAY.backlight();

  digitalWrite(PIN_AMP_POWER, HIGH);

  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  SELECTOR.togglePower(true);

  SELECTOR.setActiveChannel(CHANNEL(CONFIG.values().stored_channel));


  power_enabled = true;

}

void powerOff()
{
  resetVolume();

  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  MDISPLAY.noDisplay();
  MDISPLAY.noBacklight();

  CONFIG.values().stored_channel = SELECTOR.getActiveChannel();
  CONFIG.store();


  SELECTOR.togglePower(false);


  digitalWrite(PIN_AMP_POWER, LOW);

  digitalWrite(PIN_VU_LED, LOW);
  analogWrite(PIN_ONBOARD_LED, 0);


  setBusPower(false);

  power_enabled = false;
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

  if (current == BTN_POWER) {


    if (!is_repeat) {

      if (power_enabled) {
        powerOff();
        last_button = 0;
      }
      else {
        COUNTERS.power_up = millis();
      }

    }
    else {

      if (!power_enabled && millis() - COUNTERS.power_up >= interval_power_up) {

        powerOn();
        last_button = 0;

      }

    }//is_repeat

  }//BTN_POWER

  if (!power_enabled) {
    is_repeat = false;
    return;
  }

  if (!is_repeat) {
    switch (current) {

    case BTN_RETURN:
      if (MDISPLAY.currentMode() == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_CANCEL);
      }
      break;

    case BTN_MTS:
      if (MDISPLAY.currentMode() == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_ENTER);
      }
      break;

    case BTN_FUNCTION:
      MDISPLAY.changeDisplayMode(DisplayDevice::MODE_MENU);
      break;

    case BTN_CH_PLUS:
      if (MDISPLAY.currentMode() == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_UP);
      }
      else if (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) {
        SELECTOR.setNextChannel();
      }
      break;

    case BTN_CH_MINUS:

      if (MDISPLAY.currentMode() == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_DOWN);
      }
      else if (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR && !is_repeat) {
        SELECTOR.setPreviousChannel();
      }

      break;

    case BTN_NEXT:
      SELECTOR.sendCommand(CMD_BT_FWD);
      break;

    case BTN_PREV:
      SELECTOR.sendCommand(CMD_BT_BACK);
      break;

    case BTN_PLAY_PAUSE:
      SELECTOR.sendCommand(CMD_BT_PLAY);
      break;

    case BTN_FULLSCREEN:
      if (MDISPLAY.currentMode() == DisplayDevice::MODE_SIGNAL) {
        MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SELECTOR);
      }
      else if (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) {
        MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SIGNAL);      
      }
      break;

    case BTN_ONE:
      SELECTOR.setActiveChannel(CH1);
      break;

    case BTN_TWO:
      SELECTOR.setActiveChannel(CH2);
      break;

    case BTN_THREE:
      SELECTOR.setActiveChannel(OPTICAL);
      break;

    case BTN_FOUR:
      SELECTOR.setActiveChannel(BLUETOOTH);
      break;

    case BTN_MUTE:
      SELECTOR.toggleMute();
      break;


    }//switch

  }
  else {
    switch (current) {

    case BTN_VOL_DOWN:
      motor_direction = DOWN;
      break;

    case BTN_VOL_UP:
      motor_direction = UP;
      break;

    }
  }


  is_repeat = false;

}

int bars[7] = { 
  0,0,0,0,0,0,0 };

void calculateFFT()
{



  for (int i=0;i<FHT_N;i++){

    //while(!(ADCSRA & 0x10)); // wait for adc to be ready

    //ADCSRA = 0xf5; // restart adc
    //byte m = ADCL; // fetch adc data
    //byte j = ADCH;
    //int k = (j << 8) | m; // form into an int

    int k = analogRead(PIN_AUDIO_MONITOR);


    k -= 0x0200; // form into a signed int
    k <<= 4; // form into a 16b signed int

    //k = k / 2 - 256;
    //k <<= 2;

    fht_input[i] = k; // put real data into bins

  }
  fht_window(); // window the data for better frequency response
  fht_reorder(); // reorder the data before doing the fht
  fht_run(); // process the data in the fht
  fht_mag_octave(); // take the output of the fht


  fft_sum = 0.0;

  for (int a=2;a<7;a++) {
    //double val = ( (fht_oct_out[a]) * ( 64.0 / 0x0200 )) - 2;
    double val =( (fht_oct_out[a]) * (10.0 / 128.0 )) - 2;
    if (val>7) val=7;
    else if (val<=0) val=0;

    bars[a] =  val;

    fft_sum+=val;
  }


}


void renderFFT()
{

  MDISPLAY.setCursor(0,0);
  for (int a=2;a<7;a++) {
    byte val = bars[a];
    MDISPLAY.write(val);
  }


}



void loadChars()
{
  for (int a=7;a>=0;a--) {
    bar[a] = B00000;
  }

  for (int a=7;a>=0;a--) {
    bar[a] = B11111;
    MDISPLAY.createChar((7-a), bar);

  }

}

void testChars()
{
  MDISPLAY.setCursor(0,1);

  for (int a=0;a<8;a++) {
    MDISPLAY.write(byte(a));
  }

  MDISPLAY.write(0x20);
  MDISPLAY.write(8);


}

//void pulseLed(int audio_sample)
//{
//int audio_sample = 1023 - sensorValue;

// Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 5V):
//double voltage = (audio_sample) * (5.0 / 1024.0);
// print out the value you read:
//double val = voltage - 2.5;



//}

void setBusPower(boolean mode)
{
  if (mode == true) {

    digitalWrite(PIN_BUS_POWER, LOW);

  }
  else if (mode == false) {
    digitalWrite(PIN_BUS_POWER, HIGH);

  }
}




void clearButtonStates()
{
  for (int a=0; a < 4; a++) {
    button_state[a] = 0;
  }

}







