#include "Expander.h"

#include "SelectorConfig.h"


//normal buttons operation - pressing button on selector changes channel
//buttons act as menu keys 1 - up, 2 - down, 3 - enter/yes/ok, 4 - back/cancel/no
//Channel selector buttons
const int PIN_BTN1 = 8; 
const int PIN_BTN2 = 9; 
const int PIN_BTN3 = 10; 
const int PIN_BTN4 = 11; 
//
int button_pins[] = {  PIN_BTN1, PIN_BTN2, PIN_BTN3, PIN_BTN4 };



const int PIN_BT_POWER = 2;
const int PIN_BT_FWD = 3;
const int PIN_BT_BACK = 4;
const int PIN_BT_VOLUP = 5;
const int PIN_BT_VOLDN = 6;
const int PIN_BT_PLAY = 7;

int bt_button_pins[] = {
  PIN_BT_POWER, PIN_BT_FWD, PIN_BT_BACK, PIN_BT_VOLUP, PIN_BT_VOLDN, PIN_BT_PLAY
};



const int BT_POWER_TOGGLE_DELAY = 4000;
const int BT_BUTTON_TOGGLE_DELAY = 100;

const int MENU_TOGGLE_DELAY = 3000;
const int BUTTON_CLICK_DELAY = 350;

Expander::Expander()
{
  GLOBALS.bluetooth_power_enabled = false;
  
}
void Expander::start()
{
  int expander_pin;
  //Address 0x20 | 1 =>  0x21 on the i2c bus
  this->begin(1);
  
  for (int a=0;a<BUTTON_LAST;a++) {
      expander_pin = button_pins[a];
      this->pinMode(expander_pin, INPUT);
      this->pullUp(expander_pin, HIGH);  // turn on a 100K pullup internally
      button_state[a] = 0;
  }

  for (int a=0;a<BT_LAST;a++) {
      expander_pin = bt_button_pins[a];
      this->pinMode(expander_pin, OUTPUT);
      this->digitalWrite(expander_pin, LOW);
  }
  current_press = BUTTON_NONE;
  menu_toggle_button = BUTTON_NONE;
}
Expander::~Expander()
{

}
void Expander::clearButtonStates()
{
  for (int a=0;a<BUTTON_LAST;a++) {
      button_state[a] = 0;
  }
}

void Expander::readButtons()
{
  
  
  for (int a=0;a<BUTTON_LAST;a++) {
    
      PANEL_BUTTON current_button = PANEL_BUTTON(a);
      unsigned long last_press = button_state[a];

      int expander_pin = button_pins[a];
      int current_state = this->digitalRead(expander_pin);
      
      
      //button is down
      if (current_state == LOW) {
          //button was already pressed
          if (last_press > 0) {
            if (menu_toggle_button != current_button) {
              //button was pressed for 3sec - send a toggle menu command
              if (millis() - last_press > MENU_TOGGLE_DELAY)  {
                menu_toggle_button = current_button;
                //menu_toggle = true;
                break;
              }
            }
          }
          else {
            button_state[a] = millis();  
          }
      }
      //default state is HIGH from mcp.pullUp
      else if (current_state == HIGH) {
  
        if (menu_toggle_button == current_button) {
  
          menu_toggle_button = BUTTON_NONE;
          current_press = BUTTON_NONE;
          //menu_toggle = false;
          clearButtonStates();
          break;
  
        }
        else if (last_press > 0) {
          if (millis() - last_press > BUTTON_CLICK_DELAY) {
            current_press = current_button;
            clearButtonStates();
            break;
          }
  
        }
 
      }//current_state==HIGH
      
  } // for each buttons
  
}


PANEL_BUTTON Expander::getPressedButton()
{
    PANEL_BUTTON ret = current_press;
    if (current_press != BUTTON_NONE) {
      current_press = BUTTON_NONE;
    }
    return ret;
}
PANEL_BUTTON Expander::getMenuToggleButton()
{
    return menu_toggle_button;
    
}
void Expander::bluetoothPress(BT_BUTTON btn)
{
    if (btn<BT_POWER || btn>BT_PLAY) return;
    
    Serial.print(F("BT_BUTTON: "));  
    //Serial.print(bt_button_names[(int)btn]);
    Serial.print(btn);
    Serial.print(F(" | Expander PIN: "));
    
    int expander_pin = bt_button_pins[(int)btn];
    
    Serial.println(expander_pin);
    
    this->digitalWrite(expander_pin, HIGH);
    
    if (btn == BT_POWER) {
      delay(BT_POWER_TOGGLE_DELAY);
      GLOBALS.bluetooth_power_enabled  = !GLOBALS.bluetooth_power_enabled;
    }
    else {
      delay(BT_BUTTON_TOGGLE_DELAY);
    }
    this->digitalWrite(expander_pin, LOW);
    
}

Expander EXPANDER = Expander();


