
//#ifndef cbi
//#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
//#endif
//#ifndef sbi
//#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
//#endif


#include <Time.h>  
#include <Wire.h>  
#include <DS3231RTC.h>  


#include <IRremote.h>

#include "IRButtons.h"
#include "IRProcessor.h"
#include <EEPROM.h>

#include "AmpShared.h"

volatile uint8_t i2c_command = CMD_NONE;


//IR receiver pin
const int PIN_IR = 9;
IRProcessor irproc(PIN_IR);


#include "SelectorConfig.h"
#include "DisplayDevice.h"


#include "ChannelSelector.h"

#include "Adafruit_MCP23017.h"

#include "Expander.h"

#include "MSGEQ7.h"
////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////

const uint8_t unused_pins[] = {2,3,4,A0,A1,A3};

void setup()
{

  delay(1000);
  
  //Serial.begin(9600);
  Serial.println(F("Setup"));

  for (int a=0;a<6;a++){
    pinMode(unused_pins[a], INPUT_PULLUP);
  }
  
  irproc.enableIRIn(); // Start the receiver
  irproc.blink13(false); 

  Wire.begin();

  Wire.setClock(50000L);
  
  //Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  //Wire.onReceive(receiveCommand); // other device writes to us using beginTransmission

  EXPANDER.start();

  CONFIG.load();

  MDISPLAY.powerOn();

  //SELECTOR.setActiveChannel(AUX);
  //SELECTOR.setActiveChannel(CUBIEBOARD);
  //SELECTOR.setActiveChannel(OPTICAL);

  setSyncProvider(RTC.get); 

  
  
  CHANNEL stored_channel = CHANNEL(CONFIG.values().stored_channel);

  if (stored_channel>CHANNEL_NONE && stored_channel<CHANNEL_LAST) {
    SELECTOR.setActiveChannel(stored_channel);
  }
  else {
    SELECTOR.setActiveChannel(CUBIEBOARD);
  }
  
  if (CONFIG.values().cubie_power_auto) {
// 	Serial.println(F("Auto-Power Cubie Enabled"));
	SELECTOR.setCubiePower(true);
  }
  else {
// 	Serial.println(F("Auto-Power Cubie Disabled"));
	SELECTOR.setCubiePower(false);
  }

  MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SELECTOR);
  
  MDISPLAY.showMessage(F("Booting ..."));
  
  delay(1500);
  Serial.println(F("Setup Done"));
}

boolean init_boot = true;



void loop()
{
    //Serial.println("Loop");
    
    //checkVCC();
    

    
    irproc.process();

    //catch repeat case
    //unsigned long button = irproc.lastButton();
    //processIRButton(button);

    //catch no repeat case
    if (irproc.buttonPressed() == BTN_NOBUTTON) {
      processIRButton(irproc.lastButton());
    }
    
    readVolume();
    
    if (MDISPLAY.currentMode() == DisplayDevice::MODE_SIGNAL) {
      readPower();
    }
    
    EXPANDER.readButtons();
    
    
    
    if (EXPANDER.getMenuToggleButton() != BUTTON_NONE) {
        MDISPLAY.changeDisplayMode(DisplayDevice::MODE_MENU);

    }    
    else {
        PANEL_BUTTON button_pressed = EXPANDER.getPressedButton();
        if (button_pressed != BUTTON_NONE) {
            if (MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) {
              SELECTOR.setActiveChannel(CHANNEL(button_pressed));
            }
            else if (MDISPLAY.currentMode() == DisplayDevice::MODE_MENU) {
              MDISPLAY.processButton(button_pressed);
            }
        }
    }
    


    /**
    if ((COUNTERS.sleep_signal > 0 && COUNTERS.sleep_signal<current_millis) && (current_millis - COUNTERS.sleep_signal) >= CONFIG.values().signal_sleep_timeout ) {

      COUNTERS.sleep_signal = 0;  

      if (CONFIG.values().signal_sleep_enabled == true) {
        //powerOff();
        /////
        //TODO:
      }

    }
    */

    MDISPLAY.processLoop();
    
    
}


void readVolume()
{
    //request 10 bytes
    uint8_t data[10]={STOPPED,false, 0,0,0,0 ,0,0,0,0};
    
    AMPSHARED.readDevice(ADDR_VOLUME, data);
    
    MOTOR_STATE state = (MOTOR_STATE)data[0];
    boolean is_mute = boolean(data[1]);

    if (state == UP) {
        MDISPLAY.showMessage(F("Volume Up"));
    }
    else if (state == DOWN) {
        MDISPLAY.showMessage(F("Volume Down"));
    }
  
    SELECTOR.setMute(is_mute);
    
    GLOBALS.temp_volume = (float)(AMPSHARED.readFloat(data, 2) / 10000.0);
    GLOBALS.vcc_volume = (float)AMPSHARED.readFloat(data, 6);
}
void readPower()
{
    //request 10 bytes
    uint8_t data[10]={false,false, 0,0,0,0 ,0,0,0,0};

    AMPSHARED.readDevice(ADDR_PWR, data);
    
    boolean power_enabled = data[0];
    GLOBALS.amp_power_enabled = data[1];

    GLOBALS.temp_power = (float)(AMPSHARED.readFloat(data, 2) / 10000.0);

    GLOBALS.vcc_power = (float)AMPSHARED.readFloat(data, 6);
}

void powerOn()
{
 

  MDISPLAY.powerOn();

  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  SELECTOR.setActiveChannel(CHANNEL(CONFIG.values().stored_channel));

}

void powerOff()
{

  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  
  CONFIG.values().stored_channel = SELECTOR.getActiveChannel();
  CONFIG.store();

  MDISPLAY.powerOff();
  
}



void processIRButton( long  unsigned int current )
{


  
  DisplayDevice::DISPLAY_MODE current_mode = MDISPLAY.currentMode();
  CHANNEL current_channel = SELECTOR.getActiveChannel();
 
    switch (current) {

    case BTN_RETURN:
      MDISPLAY.processButton(BUTTON_CANCEL);
      break;

    case BTN_PLAY_PAUSE:
      if (current_mode == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_ENTER);
      }
      else {
        if (current_channel == BLUETOOTH) { 
          EXPANDER.bluetoothPress(BT_PLAY);
        }
      }
      break;

    case BTN_MENU:
      MDISPLAY.changeDisplayMode(DisplayDevice::MODE_MENU);
      break;

    case BTN_NEXT:
      if (current_mode == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_UP);
      }
      else {
        if (current_channel == BLUETOOTH) { 
          EXPANDER.bluetoothPress(BT_FWD);
        }  
      }
      break;

    case BTN_PREV:
      if (current_mode == DisplayDevice::MODE_MENU) {
        MDISPLAY.processButton(BUTTON_DOWN);
      }
      else {
        if (current_channel == BLUETOOTH) { 
          EXPANDER.bluetoothPress(BT_BACK);
        }  
      }
      break;

    
   
    case BTN_EIGHT:
        if (current_mode != DisplayDevice::MODE_MENU) {
          if (current_channel == BLUETOOTH) { 
            EXPANDER.bluetoothPress(BT_VOLDN);
          }
        }
        break;
    case BTN_NINE:
        if (current_mode != DisplayDevice::MODE_MENU) {
          if (current_channel == BLUETOOTH) {
            EXPANDER.bluetoothPress(BT_VOLUP);
          }
        }
        break;
    
    case BTN_TEST:
      MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SIGNAL);      
      break;

    case BTN_ONE:
      SELECTOR.setActiveChannel(AUX);
      break;

    case BTN_TWO:
      SELECTOR.setActiveChannel(CUBIEBOARD);
      break;

    case BTN_THREE:
      SELECTOR.setActiveChannel(OPTICAL);
      break;

    case BTN_FOUR:
      SELECTOR.setActiveChannel(BLUETOOTH);
      break;

    }//switch



}










