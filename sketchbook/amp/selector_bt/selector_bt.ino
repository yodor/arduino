
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


const uint8_t unused_pins[] = {2,3,4,5,6,7,8,11,A0,A1,A3};

void setup()
{

  Serial.begin(9600);
  

  for (int a=0;a<11;a++){
    pinMode(unused_pins[a], INPUT_PULLUP);
  }
  
  irproc.enableIRIn(); // Start the receiver
  irproc.blink13(false); 


  Wire.begin(ADDR_SELECTOR);

  //Wire.setClock(50000L);
  
  //digitalWrite(SDA, 1);
  //digitalWrite(SCL, 1);
 
  
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveCommand); // other device writes to us using beginTransmission

  EXPANDER.start();

  CONFIG.load();

  SELECTOR.setMute(true);

  MDISPLAY.powerOn();

  setSyncProvider(RTC.get); 

  MDISPLAY.changeDisplayMode(DisplayDevice::MODE_STANDBY);

}

boolean init_boot = true;

//other device writes to us using beginTransmission
void receiveCommand(int howMany)
{
  uint8_t i2c_command = CMD_NONE;

  if (Wire.available()) {
    i2c_command = Wire.read();
  }
}

//other device writes to us using requestFrom
void requestEvent()
{

}
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
    
    //readVolume();
    
    //if (MDISPLAY.currentMode() == DisplayDevice::MODE_SIGNAL) {
      //readPower();
    //}
    
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
    

    MDISPLAY.processLoop();

}

void powerOn()
{
 

  MDISPLAY.powerOn();
  MDISPLAY.changeDisplayMode(MDISPLAY.MODE_SELECTOR);
  
  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  SELECTOR.setMute(false);

  SELECTOR.setActiveChannel(BLUETOOTH);

  delay(1000);
  
}

void powerOff()
{

  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  SELECTOR.setMute(true);
  
  if (GLOBALS.bluetooth_power_enabled == true) {

    //sendCommand(CMD_BT_POWER);
    MDISPLAY.showMessage(F("Power off ..."));
            
    EXPANDER.bluetoothPress(BT_POWER);

  }
  MDISPLAY.powerOff();
    
}

void standBy()
{

  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.display_update = 0;

  SELECTOR.setMute(true);
  
  if (GLOBALS.bluetooth_power_enabled == true) {

    //sendCommand(CMD_BT_POWER);
    MDISPLAY.showMessage(F("Standby ..."));
            
    EXPANDER.bluetoothPress(BT_POWER);

  }

  MDISPLAY.changeDisplayMode(MDISPLAY.MODE_STANDBY);
  
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

    
   
    case BTN_VOL_DOWN:
        EXPANDER.bluetoothPress(BT_VOLDN);
        break;
        
    case BTN_VOL_UP:
        EXPANDER.bluetoothPress(BT_VOLUP);
        break;
        
    case BTN_MUTE:
      SELECTOR.setMute(!SELECTOR.isMute());
      break;
    
    case BTN_POWER:
      if (MDISPLAY.currentMode() == MDISPLAY.MODE_STANDBY) {
        powerOn();
      }
      else {
        
        standBy();
        
      }
      break;

    }//switch

}










