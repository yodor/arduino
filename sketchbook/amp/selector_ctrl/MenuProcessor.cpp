#include "MenuProcessor.h"
#include "Menu.h"
#include <stdio.h>
#include <Arduino.h>

#include "DisplayDevice.h"
#include "ChannelSelector.h"
#include "SelectorConfig.h"
#include "Expander.h"


const __FlashStringHelper *STR_YES = F2("YES");
const __FlashStringHelper *STR_NO = F2("NO");
const __FlashStringHelper *STR_PROCESSING = F2("Processing ...");
const __FlashStringHelper *STR_MENU = F2("Menu");
const __FlashStringHelper *STR_STORED = F2("Config Stored");

MenuProcessor::MenuProcessor()
{
}

void MenuProcessor::renderLine1(char *line1)
{

  
  if (Menu::Current->parent) {
    Menu *parent_menu = Menu::Current->parent;
    MenuItem *item = parent_menu->itemAt(parent_menu->index);
    snprintf(line1, 17, "%16s", item->name);
  }
  else {
    snprintf(line1, 17, "%16s", (const char*)STR_MENU);    
  }
}

void MenuProcessor::renderLine2(char *line2) 
{
  
  MenuItem *item = Menu::Current->itemAt(Menu::Current->index);
  if (item) {
    snprintf(line2, 17, "%-16s", item->name);  
  }
  else {
    snprintf(line2, 17, "%-16s", "...");  
  }

}

void MenuProcessor::renderScreen(char *line1, char* line2)
{
  renderLine1(line1);
  renderLine2(line2);
}

void MenuProcessor::processCancelButton(const char* msg)
{
  if (Menu::Current->parent) {

    Menu *parent_menu = Menu::Current->parent;
    int parent_index = parent_menu->index;

    Menu::Current->index = 0;
    Menu::Current = parent_menu;

  }
  else {
    //popout from menu
    //Menu::Current->parent = 0;
    //Menu::Current->index = -1;

    MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SELECTOR);

  }

  if (strlen(msg)>0) {
      MDISPLAY.showMessage(msg);
  }
}

void MenuProcessor::processButton(PANEL_BUTTON button) 
{
  if (button == BUTTON_CANCEL) {

    processCancelButton();

  }
  else if (button == BUTTON_ENTER) {

    MenuItem *item = Menu::Current->itemAt(Menu::Current->index);
    if (item->menu) {
      //Serial.println(F2("Current set to submenu"));
      Menu* current = Menu::Current;
      Menu::Current = item->menu;
      Menu::Current->parent = current;
      Menu::Current->index = 0;

    }
    else {
          //Serial.println(F2("Current have no submenu"));
    }
  }
  else if (button == BUTTON_UP) {
    Menu::Current->index+=1;
    if (Menu::Current->index >= Menu::Current->itemCount()) {
      Menu::Current->index = 0;
    }

  }
  else if (button == BUTTON_DOWN) {
    Menu::Current->index-=1;

    if (Menu::Current->index < 0) {
      Menu::Current->index = Menu::Current->itemCount()-1;
    }     

  }
}


/////
/////
ClockMenuProcessor::ClockMenuProcessor() : MenuProcessor() 
{ 
  clock_adjust = CLOCK_INIT;
}

void ClockMenuProcessor::renderLine2(char *line2) 
{

  if (clock_adjust == CLOCK_INIT) {

    MDISPLAY.moveCursor(1, 1);
    //read current clock
    //tm.Hour = 0;
    //tm.Minute = 0;

    adj_hour = hour();
    adj_minute = minute();

    clock_adjust = CLOCK_HOUR;
  }
  char buf[6];
  snprintf(buf, 6, "%02d:%02d", adj_hour, adj_minute);
  snprintf(line2, 17, "%-16s", buf);
}

void ClockMenuProcessor::processCancelButton(const char* msg)
{
  MDISPLAY.moveCursor(-1,-1);
  clock_adjust = CLOCK_INIT;
  MenuProcessor::processCancelButton(msg);
}

void ClockMenuProcessor::processButton(PANEL_BUTTON button)
{


  if (button == BUTTON_ENTER) {

    if (clock_adjust == CLOCK_HOUR) {
      clock_adjust = CLOCK_MINUTE;
      MDISPLAY.moveCursor(4,1);

    }
    else if (clock_adjust == CLOCK_MINUTE) {
      //store clock to RTC
      
      tmElements_t tm;
      RTC.read(tm);
      
      tm.Hour = adj_hour;
      tm.Minute = adj_minute;
      tm.Second = (uint8_t)0;
      
      time_t current_time = makeTime(tm);
      
      setTime(adj_hour,adj_minute,0,day(),month(),year());

      RTC.set(current_time);
    
      MDISPLAY.moveCursor(-1,-1);
      processCancelButton("Clock Stored");
      
    }
    
  }

  else if (button == BUTTON_CANCEL) {

    processCancelButton();

  }

  else if (button == BUTTON_UP) {

    if (clock_adjust == CLOCK_HOUR) {
      adj_hour ++;
      if (adj_hour>23) {
        adj_hour = 0;
      }
    }
    else if (clock_adjust == CLOCK_MINUTE) {
      adj_minute ++;
      if (adj_minute>59) {
        adj_minute = 0;
      }
    }

  }

  else if (button == BUTTON_DOWN) {

    if (clock_adjust == CLOCK_HOUR) {
      adj_hour --;
      if (adj_hour<0) {
        adj_hour = 23;
      }
    }
    else if (clock_adjust == CLOCK_MINUTE) {
      adj_minute --;
      if (adj_minute<0) {
        adj_minute = 59;
      }
    }

  }

}
///
///
SettingsMenuProcessor::SettingsMenuProcessor() : MenuProcessor() 
{ 
  edit_setting = false;    
}

void SettingsMenuProcessor::renderLine2(char *line2)
{

  
  MenuItem *item = Menu::Current->itemAt(Menu::Current->index);
  if (item->menu) {
    snprintf(menu_buf, 17, "%s ...", item->name);      
  }
  else {
    renderLine2Impl(item);
    
  }
  snprintf(line2, 17, "%-16s", menu_buf);
  
}
void SettingsMenuProcessor::renderLine2Impl(MenuItem *item)
{

    long unsigned sleep_timeout = CONFIG.values().signal_sleep_timeout;
    long unsigned timeout_minutes = sleep_timeout / 1000 / 60;
  
    if (Menu::Current->index == 2) {
      snprintf(menu_buf, 17, "%s: %s", item->name, ((CONFIG.values().signal_sleep_enabled) ? (const char*)STR_YES : (const char*)STR_NO));      
    }
    else if (Menu::Current->index == 3) {
      snprintf(menu_buf, 17, "%s: %2lu", item->name, timeout_minutes);      
    }
    else if (Menu::Current->index == 4) {
      snprintf(menu_buf, 17, "%s: %2d", item->name, CONFIG.values().sleep_toggle_dbV);      
    }
    else if (Menu::Current->index == 5) {
      snprintf(menu_buf, 17, "%s: %s", item->name, (GLOBALS.amp_power_enabled ? (const char*)STR_YES : (const char*)STR_NO));      
    }

}
void SettingsMenuProcessor::processCancelButton(const char* msg)
{
  CONFIG.store();
  Menu::Current->index = 0;
  edit_setting = false;
  MDISPLAY.moveCursor(-1,-1);

  MenuProcessor::processCancelButton((const char*)STR_STORED);

}
void SettingsMenuProcessor::processEditDown()
{
  long unsigned sleep_timeout = CONFIG.values().signal_sleep_timeout;
  long unsigned timeout_minutes = sleep_timeout / 1000 / 60;

  
      if (Menu::Current->index == 2) {
        bool val = CONFIG.values().signal_sleep_enabled;
        val = !val;
        CONFIG.values().signal_sleep_enabled = val;
      }
      else if (Menu::Current->index == 3) {

        timeout_minutes--;
        if (timeout_minutes<0)timeout_minutes=60;
        CONFIG.values().signal_sleep_timeout = (timeout_minutes * 60 * 1000);
      }
      else if (Menu::Current->index == 4) {              
        CONFIG.values().sleep_toggle_dbV--;
        if (CONFIG.values().sleep_toggle_dbV < -50) {
          CONFIG.values().sleep_toggle_dbV = -30;
        }
      }
      else if (Menu::Current->index == 5) {              
        bool state = !GLOBALS.amp_power_enabled;
        SELECTOR.setAmpPower(state);
      }
}
void SettingsMenuProcessor::processEditUp()
{

  long unsigned sleep_timeout = CONFIG.values().signal_sleep_timeout;
  long unsigned timeout_minutes = sleep_timeout / 1000 / 60;

  
      if (Menu::Current->index == 2) {
        bool val = CONFIG.values().signal_sleep_enabled;
        val = !val;
        CONFIG.values().signal_sleep_enabled = val;
      }
      else if (Menu::Current->index == 3) {
        timeout_minutes++;
        if (timeout_minutes>60)timeout_minutes=0;
        CONFIG.values().signal_sleep_timeout = (timeout_minutes * 60 * 1000);
      }
      else if (Menu::Current->index == 4) {              
        CONFIG.values().sleep_toggle_dbV++;
        if (CONFIG.values().sleep_toggle_dbV > -30) {
          CONFIG.values().sleep_toggle_dbV = -50;
        }
      }
      else if (Menu::Current->index == 5) {              
        bool state = !GLOBALS.amp_power_enabled;
        SELECTOR.setAmpPower(state);
      }
      
}
void SettingsMenuProcessor::processButton(PANEL_BUTTON button)
{

  


  if (button == BUTTON_UP) {

    if (edit_setting == false) {
      Menu::Current->index++;
      if (Menu::Current->index>=Menu::Current->itemCount()) {
        Menu::Current->index = 0;
      }
    }
    else {

        processEditUp();
    }
  }
  else if (button == BUTTON_DOWN) {

    if (edit_setting == false) {
      Menu::Current->index--;
      if (Menu::Current->index<0) {
        Menu::Current->index = Menu::Current->itemCount()-1;
      }
    }
    else {
        processEditDown();
    }
  }
  else if (button == BUTTON_ENTER) {

    if (Menu::Current->indexItem()) {
        if (Menu::Current->indexItem()->menu) {

          MenuProcessor::processButton(button);
        }
        else {
          
          if (edit_setting == false) {

            edit_setting = true;
            MDISPLAY.moveCursor(15,1);
          }
          else if (edit_setting == true) {

            edit_setting = false;
            MDISPLAY.moveCursor(-1,-1);
          }
        }

    }
    
  }
  else if (button == BUTTON_CANCEL) {

    processCancelButton();
    
  }


}


BluetoothMenuProcessor::BluetoothMenuProcessor() : SettingsMenuProcessor()
{

}
void BluetoothMenuProcessor::renderLine2Impl(MenuItem *item)
{
  
  //todo get current bluetooth power state

  
  if (Menu::Current->index == 0) {
    snprintf(menu_buf, 17, "%s ...", item->name);      
  }
  else if (Menu::Current->index == 1) {
    snprintf(menu_buf, 17, "%s: %s", item->name, (GLOBALS.bluetooth_power_enabled ? (const char*)STR_YES : (const char*)STR_NO));      
  }
}
void BluetoothMenuProcessor::processEditDown()
{
  
      bool bluetooth_powered = GLOBALS.bluetooth_power_enabled;
        
      //power cycle
      if (Menu::Current->index == 0) {
        if (bluetooth_powered) {
            MDISPLAY.showMessage(STR_PROCESSING);  
            EXPANDER.bluetoothPress(BT_POWER);
            MDISPLAY.showMessage(STR_PROCESSING); 
            EXPANDER.bluetoothPress(BT_POWER); 
        }    
      }
      //power status toggle
      else if (Menu::Current->index == 1) {
           MDISPLAY.showMessage(STR_PROCESSING); 
           EXPANDER.bluetoothPress(BT_POWER); 
      }
}
void BluetoothMenuProcessor::processEditUp()
{
     processEditDown();
}


CubieboardMenuProcessor::CubieboardMenuProcessor() : SettingsMenuProcessor()
{

}
void CubieboardMenuProcessor::renderLine2Impl(MenuItem *item)
{

  if (Menu::Current->index == 0) {
    snprintf(menu_buf, 17, "%s: %s", item->name, (GLOBALS.cubie_power_enabled ? (const char*)STR_YES : (const char*)STR_NO));  
    
  }
  else if (Menu::Current->index == 1) {
    snprintf(menu_buf, 17, "%s: %s", item->name, (CONFIG.values().cubie_power_auto ? (const char*)STR_YES : (const char*)STR_NO));      
  }
}
void CubieboardMenuProcessor::processEditDown()
{
      //power
      if (Menu::Current->index == 0) {
          bool state = !GLOBALS.cubie_power_enabled;
          SELECTOR.setCubiePower(state);  
      }
      //auto-power
      else if (Menu::Current->index == 1) {
          CONFIG.values().cubie_power_auto = !CONFIG.values().cubie_power_auto;
      }
}
void CubieboardMenuProcessor::processEditUp()
{
      //power
      if (Menu::Current->index == 0) {
          bool state = !GLOBALS.cubie_power_enabled;
          SELECTOR.setCubiePower(state);          
      }
      //auto-power
      else if (Menu::Current->index == 1) {
          CONFIG.values().cubie_power_auto = !CONFIG.values().cubie_power_auto;
          
      }
      
}

