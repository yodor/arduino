#ifndef MENU_PROCESSOR_H
#define MENU_PROCESSOR_H
#include <LiquidCrystal_I2C.h>
#include "SharedDefs.h"
#include "SelectorConfig.h"
#include "SignalSampler.h"

class DisplayDevice : public LiquidCrystal_I2C
{
  public:
  
  enum CLOCK_MODE {
    CLOCK_INIT = 0,
    CLOCK_HOUR = 1,
    CLOCK_MINUTE = 2
  };

  enum DISPLAY_MODE {
    MODE_FIRST = 0,
    MODE_SELECTOR = 1,
    MODE_MENU = 2,
    MODE_SIGNAL = 3,
    MODE_LAST = 4
  };




    DisplayDevice();
    ~DisplayDevice();
    

    void processButton(PANEL_BUTTON button);

    void updateClock();
    void changeDisplayMode(DISPLAY_MODE mode);
    DISPLAY_MODE currentMode();
    void updateSignal(double average, double fft_sum);

    
    void setChannelName(const __FlashStringHelper* channel, bool is_mute);
    //void setDisplay(LiquidCrystal_I2C *lcd);   
    
    void showMessage(const char* text);
    void showMessage(const __FlashStringHelper* text);
    
    void processLoop();

    
  private:
  
    void printText(const char* text);

    void processOtherMenu(PANEL_BUTTON menu_button);
    void processClockMenu(PANEL_BUTTON menu_button);
    void processLedMenu(PANEL_BUTTON menu_button);
    void processSettingsMenu(PANEL_BUTTON menu_button);
    
    char line0[17];
    char line1[17];
    
    int menu_index;
    int active_menu;
    
    int cursor_x;
    int cursor_y;
    
    CLOCK_MODE clock_adjust;
    DISPLAY_MODE current_mode;
    
    bool need_update;
    
    double i_dBV;
    
    char signal_line0[17];
    char signal_line1[17];
    
    char channel_name[17];
    
    char signal_buf[17];
    
    char temp_line[17];
    //LiquidCrystal_I2C *lcd;
    //void updateLCD();
    
    char message[17];

    char menuline_buf[17];
    
    int sleep_index;
    int led_index;
    bool edit_setting;

    char clock_str[6];
    
     unsigned long timeout_in;
     
};
extern DisplayDevice MDISPLAY;
#endif
