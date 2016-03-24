
#include "DisplayDevice.h"
#include <Wire.h>
#include <DS1307RTC.h>  // a basic DS1307 library that returns time as a time_t

#include <Arduino.h>


const __FlashStringHelper *yes = F2("YES");
const __FlashStringHelper *no = F2("NO");

DS1307RTC RTC;


const __FlashStringHelper *sleep_menu[] = {
  F2("Auto-sleep"), F2("Timeout"), F2("Threshold DB"), F2("Sample Window"), F2("FFT Update")
};

const __FlashStringHelper *led_menu[] = {
  F2("Monitor"), F2("Scale"), F2("IR")
};

const __FlashStringHelper *main_menu[] = {
  F2("Adjust Clock"), F2("Service LED"), F2("Settings")
};

#define MENU_FIRST 0
#define MENU_LAST  2

int adj_clock_hour = 0;
int adj_clock_minute = 0;

boolean clock_dot_state = false;

tmElements_t tm;

const unsigned long interval_message_refresh = 1000;

DisplayDevice::DisplayDevice() : LiquidCrystal_I2C(0x20,16,2) //lcd(NULL)
{
  
  menu_index = 0;
  active_menu = -1;
  clock_adjust = CLOCK_INIT;
  cursor_x = -1;
  cursor_y = -1;

  current_mode = MODE_SELECTOR;


  need_update = false;
  COUNTERS.message_update = 0;
  
  sleep_index = 0;
  led_index = 0;
  edit_setting = false;
  
  timeout_in = CONFIG.values().signal_sleep_timeout  / 1000;
  
}
DisplayDevice::~DisplayDevice()
{

}

void DisplayDevice::showMessage(const __FlashStringHelper* text)
{
  showMessage(reinterpret_cast<const char*>(text));
}
void DisplayDevice::showMessage(const char* text)
{

  if (strlen(text)>0) {
    snprintf(message, 17, "%s", text);
    COUNTERS.message_update = millis();
    printText("");
    
  }
}

void DisplayDevice::processLoop()
{
  unsigned long current_millis = millis();

  if  ( COUNTERS.message_update > 0 &&  current_millis - COUNTERS.message_update > interval_message_refresh ) {
        //reset display
        COUNTERS.message_update = 0;
        printText("");        
  }
  
  //if ( COUNTERS.sleep_signal > 0  &&  current_millis - COUNTERS.sleep_signal > 5000 ) {
  //    printText("");
  //}
}

void DisplayDevice::changeDisplayMode(DISPLAY_MODE mode)
{
  clear();
  
  Serial.print(F("Mode=>"));
  if (mode > MODE_FIRST && mode < MODE_LAST) {
    current_mode = mode;
  }
  else {
    current_mode = MODE_SELECTOR;
  }
  Serial.println(current_mode);
   
  if (current_mode == MODE_SELECTOR) {

    printText("");

  }
  else if (current_mode == MODE_MENU) {

    menu_index = 0;
    active_menu = -1;
    printText((const char*)main_menu[menu_index]);
    
  }
  else if (current_mode == MODE_SIGNAL) {
    printText("");
  }
  
}

DisplayDevice::DISPLAY_MODE DisplayDevice::currentMode()
{
  return current_mode;
}



void DisplayDevice::updateSignal(double average, double fft_sum)
{
  //if (!m_sampler)return;
   
  dtostrf(SAMPLER.getDBV(), 3, 0, signal_buf);
  sprintf(signal_line0, "dV:%s", signal_buf);
  
//  dtostrf(SAMPLER.getDBU(), 3, 0, signal_buf);
  dtostrf(SAMPLER.getSNRDB(), 3, 0, signal_buf);
  sprintf(signal_line0, "%s SNR:%s", signal_line0, signal_buf);
  
  
 // dtostrf(SAMPLER.getVRMS(), 1, 2, signal_buf);
 // sprintf(signal_line1, "V:%s", signal_buf);
  dtostrf(fft_sum, 2, 2, signal_buf);
  sprintf(signal_line1, "S:%s", signal_buf);
  
  
  dtostrf(average, 2, 2, signal_buf);
  sprintf(signal_line1, "%s A:%s", signal_line1, signal_buf);
  
  if (current_mode == DisplayDevice::MODE_SIGNAL) {
    printText("");
  }

  else if (current_mode == DisplayDevice::MODE_SELECTOR){
    setCursor(7,0);
    if (COUNTERS.sleep_signal>0) {
      timeout_in = (CONFIG.values().signal_sleep_timeout  - (millis() - COUNTERS.sleep_signal) ) / 1000;
      print(timeout_in);
    }
    else {
      print("   ");
    }
    //sprintf(line0, "%4lu", timeout_in);
  }
  
}

void DisplayDevice::setChannelName(const __FlashStringHelper* name, bool is_mute)
{

  if (is_mute == true) {

    sprintf(channel_name, "%s - %s", (const char*)name, (const char*)F2("Mute"));

  }
  else {

    sprintf(channel_name, "%s",  (const char*)name );

  }

  printText("");
  
}

void DisplayDevice::printText(const char* row2)
{

  if (cursor_x>-1 && cursor_y>-1) {
    cursor();
    blink();
  }
  else {
    noCursor();
    noBlink();
  }

  if (current_mode == MODE_MENU) {

    if (active_menu>-1) {
      sprintf(line0, "%16s", F2(main_menu[active_menu]));
    }
    else {
      sprintf(line0, "%16s", F2("Menu"));

    }
    if (strlen(row2)>0) {
      sprintf(line1, "%-16s", row2);
    }
    setCursor(0,0);
    print(line0);
  
    setCursor(0,1);
    print(line1);
  }
  else if (current_mode == MODE_SELECTOR) {

    //if (COUNTERS.sleep_signal>0) {
    //  unsigned long timeout_in = ( CONFIG.values().signal_sleep_timeout - (millis() - COUNTERS.sleep_signal) ) / 1000;
    //  sprintf(line0, "%4lu %11s", timeout_in, clock_str);
    //}
    //else {
      //sprintf(line0, "%s%8s", "        ", clock_str);
    //}
    
    setCursor(11,0);
    print(clock_str);
    
    if (COUNTERS.message_update>0) {
        sprintf(line1, "%-16s",  message);
    }
    else {
      if (strlen(row2)>0) {
        sprintf(line1, "%-16s", row2);
      }
      else {
        sprintf(line1, "%16s", channel_name);
      }
    }
    
    setCursor(0,1);
    print(line1);
    
  }
  else if (current_mode == MODE_SIGNAL) {
    sprintf(line0, "%-16s", signal_line0);
    sprintf(line1, "%-16s", signal_line1);
    
    setCursor(0,0);
    print(line0);
  
    setCursor(0,1);
    print(line1);
    
  }
 
  if (cursor_x>-1 && cursor_y>-1) {

    setCursor(cursor_x,cursor_y);
  }
  
}




void DisplayDevice::processSettingsMenu(PANEL_BUTTON menu_button)
{
  
    long unsigned sleep_timeout = CONFIG.values().signal_sleep_timeout;
    long unsigned timeout_minutes = sleep_timeout / 1000 / 60;
    
    
    if (menu_button == BUTTON_UP) {
      
        if (edit_setting == false) {
            sleep_index++;
            if (sleep_index>4) {
              sleep_index = 0;
            }
        }
        else {
          
            if (sleep_index == 0) {
                bool val = CONFIG.values().signal_sleep_enabled;
                val = !val;
                CONFIG.values().signal_sleep_enabled = val;
            }
            else if (sleep_index == 1) {
                timeout_minutes++;
                if (timeout_minutes>60)timeout_minutes=0;
                CONFIG.values().signal_sleep_timeout = (timeout_minutes * 60 * 1000);
            }
            else if (sleep_index == 2) {              
                CONFIG.values().sleep_toggle_dbV++;
                if (CONFIG.values().sleep_toggle_dbV > -30) {
                    CONFIG.values().sleep_toggle_dbV = -50;
                }
            }
            else if (sleep_index == 3) {              
                CONFIG.values().sample_window++;
                if (CONFIG.values().sample_window > 100) {
                    CONFIG.values().sample_window = 10;
                }
            }
            else if (sleep_index == 4) {              
                CONFIG.values().fft_refresh++;
                if (CONFIG.values().fft_refresh > 50) {
                    CONFIG.values().fft_refresh = 1;
                }
            }
        }
    }
    else if (menu_button == BUTTON_DOWN) {
      
        if (edit_setting == false) {
            sleep_index--;
            if (sleep_index<0) {
              sleep_index = 4;
            }
        }
        else {
          
            if (sleep_index == 0) {
                bool val = CONFIG.values().signal_sleep_enabled;
                val = !val;
                CONFIG.values().signal_sleep_enabled = val;
            }
            else if (sleep_index == 1) {
                
                timeout_minutes--;
                if (timeout_minutes<0)timeout_minutes=60;
                CONFIG.values().signal_sleep_timeout = (timeout_minutes * 60 * 1000);
            }
            else if (sleep_index == 2) {              
                CONFIG.values().sleep_toggle_dbV--;
                if (CONFIG.values().sleep_toggle_dbV < -50) {
                    CONFIG.values().sleep_toggle_dbV = -30;
                }
            }
            else if (sleep_index == 3) {              
                CONFIG.values().sample_window--;
                if (CONFIG.values().sample_window < 10) {
                    CONFIG.values().sample_window = 100;
                }
            }
            else if (sleep_index == 4) {              
                CONFIG.values().fft_refresh--;
                if (CONFIG.values().fft_refresh < 1) {
                    CONFIG.values().fft_refresh = 50;
                }
            }
        }
    }
    else if (menu_button == BUTTON_ENTER) {
      
        if (edit_setting == false) {
            edit_setting = true;
            cursor_x = 15;
            cursor_y = 1;
        }
        else if (edit_setting == true) {
            edit_setting = false;
            cursor_x = -1;
            cursor_y = -1;
        }
        
    }
    else if (menu_button == BUTTON_CANCEL) {
        
        CONFIG.store();
        menu_index = active_menu;
        active_menu = -1;
        edit_setting = false;
        cursor_x = -1;
        cursor_y = -1;
        sleep_index = 0;    
        printText((const char*)F2("Config Stored"));
        delay(1000);
        printText((const char*)main_menu[menu_index]);
        return;
    }
    
    if (sleep_index == 0) {
        sprintf(menuline_buf, "%s: %s", (const char*)sleep_menu[sleep_index], ((CONFIG.values().signal_sleep_enabled) ? (const char*)yes : (const char*)no));      
    }
    else if (sleep_index == 1) {
        sprintf(menuline_buf, "%s: %2lu", (const char*)sleep_menu[sleep_index], timeout_minutes);      
    }
    else if (sleep_index == 2) {
        sprintf(menuline_buf, "%s:%2d", (const char*)sleep_menu[sleep_index], CONFIG.values().sleep_toggle_dbV);      
    }
    else if (sleep_index == 3) {
        sprintf(menuline_buf, "%s:%2d", (const char*)sleep_menu[sleep_index], CONFIG.values().sample_window);      
    }
    else if (sleep_index == 4) {
        sprintf(menuline_buf, "%s:%2d", (const char*)sleep_menu[sleep_index], CONFIG.values().fft_refresh);      
    }
    printText(menuline_buf);
}

void DisplayDevice::processLedMenu(PANEL_BUTTON menu_button)
{

    
    
    if (menu_button == BUTTON_UP) {
      
        if (edit_setting == false) {
            led_index++;
            if (led_index>2) {
              led_index = 0;
            }
        }
        else {
          
            if (led_index == 0) { //Monitor
                bool val = CONFIG.values().pulse_led_signal_enabled;
                val = !val;
                CONFIG.values().pulse_led_signal_enabled = val;
            }
            else if (led_index == 1) { //Scale
                int scale_val = CONFIG.values().pulse_signal_scale;
                scale_val++;
                if (scale_val>10)scale_val = 1;
                CONFIG.values().pulse_signal_scale = scale_val;
            }
            else if (led_index == 2) { //IR
                bool val = CONFIG.values().pulse_led_ir_enabled;
                val = !val;
                CONFIG.values().pulse_led_ir_enabled = val;
            }
        }
    }
    else if (menu_button == BUTTON_DOWN) {
        if (edit_setting == false) {
          led_index--;
          if (led_index<0) {
            led_index = 2;
          }
        }
        else {
          
            if (led_index == 0) { //Monitor
                bool val = CONFIG.values().pulse_led_signal_enabled;
                val = !val;
                CONFIG.values().pulse_led_signal_enabled = val;
            }
            else if (led_index == 1) { //Scale
                int scale_val = CONFIG.values().pulse_signal_scale;
                scale_val--;
                if (scale_val<0)scale_val = 10;
                CONFIG.values().pulse_signal_scale = scale_val;
            }
            else if (led_index == 2) { //IR
                bool val = CONFIG.values().pulse_led_ir_enabled;
                val = !val;
                CONFIG.values().pulse_led_ir_enabled = val;
            }
            
            
        }
    }
    
    else if (menu_button == BUTTON_ENTER) {
      
        if (edit_setting == false) {
            edit_setting = true;
            cursor_x = 15;
            cursor_y = 1;
        }
        else if (edit_setting == true) {
            edit_setting = false;
            cursor_x = -1;
            cursor_y = -1;
        }
    }
    else if (menu_button == BUTTON_CANCEL) {
        
        CONFIG.store();
        menu_index = active_menu;
        active_menu = -1;
        edit_setting = false;
        cursor_x = -1;
        cursor_y = -1;
        led_index = 0;
        printText("Config Stored");
        delay(1000);
        printText((const char*)main_menu[menu_index]);
        return;
    }
  
    if (led_index == 0) {
        sprintf(menuline_buf, "%s: %s", (const char*)led_menu[led_index], (CONFIG.values().pulse_led_signal_enabled) ? (const char*)yes : (const char*)no);      
    }
    else if (led_index == 1) {
        sprintf(menuline_buf, "%s: %4d", (const char*)led_menu[led_index], CONFIG.values().pulse_signal_scale );      
    }
    else if (led_index == 2) {
        sprintf(menuline_buf, "%s: %s", (const char*)led_menu[led_index], (CONFIG.values().pulse_led_ir_enabled) ? (const char*)yes : (const char*)no);      
    }

    printText(menuline_buf);
}

void DisplayDevice::processClockMenu(PANEL_BUTTON menu_button)
{

  char clock_buf[6];

  if (clock_adjust == CLOCK_INIT) {

    cursor_x = 0;
    cursor_y = 1;     
    adj_clock_hour = tm.Hour;
    adj_clock_minute = tm.Minute;
    sprintf(clock_buf, "%02d:%02d", adj_clock_hour, adj_clock_minute);
    printText(clock_buf);
    clock_adjust = CLOCK_HOUR;
    return;
  }

  if (menu_button == BUTTON_ENTER) {

    if (clock_adjust == CLOCK_HOUR) {
      clock_adjust = CLOCK_MINUTE;
      cursor_x = 3;
      cursor_y = 1;
      sprintf(clock_buf, "%02d:%02d", adj_clock_hour, adj_clock_minute);
      printText(clock_buf);
      return;
    }

    if (clock_adjust == CLOCK_MINUTE) {
      //store clock to RTC

      tmElements_t tm1;
      tm1.Hour = adj_clock_hour;
      tm1.Minute = adj_clock_minute;
      tm1.Second = 0;

      if (RTC.write(tm1)) {
        printText("Clock Stored");
      }
      else {
        printText("Clock Error");
      }

      menu_button = BUTTON_CANCEL;
      updateClock();

    }

  }

  if (menu_button == BUTTON_CANCEL) {
    menu_index = active_menu;
    active_menu = -1;

    cursor_x = -1;
    cursor_y = -1;
    clock_adjust = CLOCK_INIT;
    printText((const char*)main_menu[menu_index]);

  }

  else if (menu_button == BUTTON_UP) {

    if (clock_adjust == CLOCK_HOUR) {
      adj_clock_hour ++;
      if (adj_clock_hour>23) {
        adj_clock_hour = 0;
      }
    }
    else if (clock_adjust == CLOCK_MINUTE) {
      adj_clock_minute ++;
      if (adj_clock_minute>59) {
        adj_clock_minute = 0;
      }
    }
    sprintf(clock_buf, "%02d:%02d", adj_clock_hour, adj_clock_minute);
    printText(clock_buf);


  }
  else if (menu_button == BUTTON_DOWN) {
    if (clock_adjust == CLOCK_HOUR) {
      adj_clock_hour --;
      if (adj_clock_hour<0) {
        adj_clock_hour = 23;
      }
    }
    else if (clock_adjust == CLOCK_MINUTE) {
      adj_clock_minute --;
      if (adj_clock_minute<0) {
        adj_clock_minute = 59;
      }
    }
    sprintf(clock_buf, "%02d:%02d", adj_clock_hour, adj_clock_minute);
    printText(clock_buf);

  }


}
void DisplayDevice::processOtherMenu(PANEL_BUTTON menu_button)
{
  if (menu_button == BUTTON_CANCEL) {

    menu_index = active_menu;
    active_menu = -1;
    printText((const char*)main_menu[menu_index]);

  }
  else {
    printText("N/A");
  }
}
/**
 * void debugMenu(int menu_button)
 * {
 * 
 * Serial.print("Active Menu: ");
 * Serial.print(active_menu);
 * Serial.print("|");
 * Serial.print("MenuIndex: ");
 * Serial.print(menu_index);
 * Serial.print("|");
 * Serial.print(button_names[menu_button]);  
 * Serial.print("|");
 * Serial.println();
 * 
 * }
 */

void DisplayDevice::processButton(PANEL_BUTTON menu_button)
{


  Serial.print(F("Button: "));
  Serial.println(menu_button);

  //main_menu
  if (active_menu == -1) {

    if (menu_button == BUTTON_CANCEL) {

      changeDisplayMode(MODE_SELECTOR);
      return;
    }
    else if (menu_button == BUTTON_ENTER) {

      active_menu = menu_index;
      menu_button = BUTTON_NONE;
      

    }
    else if (menu_button == BUTTON_UP) {
      menu_index+=1;
      if (menu_index>MENU_LAST) {
        menu_index = MENU_FIRST;
      }
      //

      printText((const char*)main_menu[menu_index]);
      //debugMenu(menu_button);
      return;
    }
    else if (menu_button == BUTTON_DOWN) {
      menu_index-=1;

      if (menu_index < MENU_FIRST) {
        menu_index = MENU_LAST;
      }     

      printText((const char*)main_menu[menu_index]);
      //debugMenu(menu_button);
      return;
    }

    

  }

  if (active_menu == 0) {

    processClockMenu(menu_button);

  } //active_menu == 0 // clock

  else if (active_menu == 1) {

    processLedMenu(menu_button);

  }

  else if (active_menu == 2) {

    processSettingsMenu(menu_button);
  }


}



void DisplayDevice::updateClock()
{


  if (RTC.read(tm)) {

    //Serial.print(", Date (D/M/Y) = ");
    //Serial.print(tm.Day);
    //Serial.write('/');
    //Serial.print(tm.Month);
    //Serial.write('/');
    //Serial.print(tmYearToCalendar(tm.Year));
    //Serial.println();
    sprintf(clock_str, "%02d:%02d", tm.Hour, tm.Minute);

  } 
  else {
    sprintf(clock_str, "%02d:%02d", 0, 0);

    if (RTC.chipPresent()) {
      //Serial.println("The DS1307 is stopped.  Please run the SetTime");
      //Serial.println("example to initialize the time and begin running.");
      //Serial.println();
    } 
    else {
      //Serial.println("DS1307 read error!  Please check the circuitry.");
      //Serial.println();

    }

  }

  printText("");
}


DisplayDevice MDISPLAY = DisplayDevice();


