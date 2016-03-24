
//#ifndef cbi
//#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
//#endif
//#ifndef sbi
//#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
//#endif


#include <Time.h>  
#include <Wire.h>  
#include <DS3231RTC.h>  // a basic DS1307 library that returns time as a time_t

#include <LiquidCrystal_I2C.h>

#include <IRremote.h>

#include "IRButtons.h"

#include <EEPROM.h>

#include "SharedDefs.h"

volatile uint8_t i2c_command = CMD_NONE;


//IR receiver pin
const int PIN_IR = 9;
IRrecv irrecv(PIN_IR);





//const unsigned long button_click_delay = 150;
//const unsigned long menu_toggle_delay = 3000;


//unsigned long button_state[] = {  
//  0, 0, 0, 0 };



//ir status
long unsigned int last_button = -1; // code of last pressed IR button
boolean is_repeat = false; //IR button is repeating code






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


void setup()
{

  
  
  Serial.begin(9600);
 

  irrecv.enableIRIn(); // Start the receiver
  irrecv.blink13(false); 

  Wire.begin();
  
  

  //Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  //Wire.onReceive(receiveCommand); // other device writes to us using beginTransmission

  
  
  
  
  EXPANDER.start();
  
  
  CONFIG.load();

  SELECTOR.setCubiePower(CONFIG.values().cubie_power_auto);



  MDISPLAY.powerOn();
  MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SELECTOR);
  
  
  CHANNEL stored_channel = CHANNEL(CONFIG.values().stored_channel);

  if (stored_channel>CHANNEL_NONE && stored_channel<CHANNEL_LAST) {
    SELECTOR.setActiveChannel(stored_channel);
  }
  else {
    SELECTOR.setActiveChannel(CUBIEBOARD);
  }

  
  
  EQ7.begin();

  setSyncProvider(RTC.get); 

  
  
  Serial.println("Setup");
}

boolean init_boot = true;

decode_results results;



//int cnt = 0;

//boolean is_mute = false;


void loop()
{
    //Serial.println("Loop");
    
    //checkVCC();
    
    if (init_boot==true) {
      init_boot = false;
      delay(1500);
    }
    long unsigned current_millis = millis();

    if (irrecv.decode(&results)) {

      COUNTERS.sleep_signal = 0;

      processIRButton(results.value);

      irrecv.resume(); 

    }

    if (MDISPLAY.currentMode() == DisplayDevice::MODE_SIGNAL) {
      readVolume();
      readPower();
    }
    
    EXPANDER.readButtons();
    
    
    
    if (EXPANDER.getMenuToggleButton() != BUTTON_NONE && MDISPLAY.currentMode() == DisplayDevice::MODE_SELECTOR) {
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
    
    Wire.requestFrom(ADDR_VOLUME, 10, true);
    
    
    int num_read = -1;
    
    while (Wire.available()) {
      
      data[++num_read] = Wire.read();
      if (num_read==10) break;
      
    }
    
    MOTOR_STATE state = (MOTOR_STATE)data[0];
    boolean is_mute = data[1];

    if (state == UP) {
        MDISPLAY.showMessage(F2("Volume Up"));
    }
    else if (state == DOWN) {
        MDISPLAY.showMessage(F2("Volume Down"));
    }
  
    SELECTOR.setMute(is_mute);
    

      long temp = 0;

      for(int i = 0; i < 4; i++){      
         temp <<= 8;  
         temp ^= (long)data[2+i] & 0xFF;      
      }
      float temp_volume = temp / 10000.0;
      GLOBALS.temp_volume = temp_volume;
      

      long vcc = 0;

      for(int i = 0; i < 4; i++){      
         vcc <<= 8;  
         vcc ^= (long)data[6+i] & 0xFF;      
      }
      GLOBALS.vcc_volume = vcc;
}
void readPower()
{
    //request 10 bytes
    uint8_t data[10]={false,false, 0,0,0,0 ,0,0,0,0};
    
    Wire.requestFrom(ADDR_PWR, 10, true);
    
    
    int num_read = -1;
    while (Wire.available()) {
      
      data[++num_read] = Wire.read();
      if (num_read==10) break;
      
    }
    
    boolean power_enabled = data[0];
    GLOBALS.amp_power_enabled = data[1];
    
      long temp = 0;

      for(int i = 0; i < 4; i++){      
         temp <<= 8;  
         temp ^= (long)data[2+i] & 0xFF;      
      }
      float temp_power = temp / 10000.0;
      GLOBALS.temp_power = temp_power;
      

      long vcc = 0;

      for(int i = 0; i < 4; i++){      
         vcc <<= 8;  
         vcc ^= (long)data[6+i] & 0xFF;      
      }
      GLOBALS.vcc_power = vcc;
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


  if (current == BTN_REPEAT) {
    current = last_button;
    is_repeat = true;
  }
  else {
    is_repeat = false;

  }

  last_button = current;

  
  DisplayDevice::DISPLAY_MODE current_mode = MDISPLAY.currentMode();
  
  if (!is_repeat) {
    switch (current) {

    case BTN_RETURN:
      MDISPLAY.processButton(BUTTON_CANCEL);
      break;

    case BTN_MTS:
      MDISPLAY.processButton(BUTTON_ENTER);
      break;

    case BTN_FUNCTION:
      MDISPLAY.changeDisplayMode(DisplayDevice::MODE_MENU);
      break;

    case BTN_CH_PLUS:
      MDISPLAY.processButton(BUTTON_UP);
      break;

    case BTN_CH_MINUS:

      MDISPLAY.processButton(BUTTON_DOWN);

      break;

    case BTN_NEXT:
      EXPANDER.bluetoothPress(BT_FWD);
      break;

    case BTN_PREV:
      EXPANDER.bluetoothPress(BT_BACK);
      break;

    case BTN_PLAY_PAUSE:
      EXPANDER.bluetoothPress(BT_PLAY);
      break;

    case BTN_FULLSCREEN:
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

    case BTN_POWER:
      //TODO
      //SELECTOR.toggleChannelPower();
      break;

    }//switch

  }
  else {
    
  }


  is_repeat = false;

}


//void checkVCC() {
//  
//  
//
//  long vcc_result = readVCC();
//
//  if (vcc_result<3800) {
//    CONFIG.store();
//    while (vcc_result<3800) {
//      vcc_result = readVCC();
//      delay(1000);
//    }
//  }
//
//  //MDISPLAY.setSelectorTempVCC(readTemp(), vcc_result);
//}











