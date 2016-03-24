#ifndef MENU_PROCESSOR_H
#define MENU_PROCESSOR_H
#include <LiquidCrystal_I2C.h>
#include "SharedDefs.h"
#include "SelectorConfig.h"

#include <DS3231RTC.h>  // a basic DS1307 library that returns time as a time_t

#include "Menu.h"
#include "MenuProcessor.h"

class DisplayScreen 
{
  public:
    DisplayScreen();
    virtual ~DisplayScreen();
    
    virtual void processLoop(long unsigned current_millis);
    virtual void processButton(PANEL_BUTTON button);
    
    //render contents 
    virtual void render(char *line0, char* line1) = 0;
    virtual int getXLine0();
    
  protected:
//    char screen_line0[17];
//    char screen_line1[17];
    int line0_x;
    
};

class MenuScreen : public DisplayScreen
{
  public:
    MenuScreen();
    virtual ~MenuScreen();
    
    virtual void render(char *line0, char* line1);
    virtual void processButton(PANEL_BUTTON button);
    
  private:
    static Menu *main;
    void initMainMenu();
    
};
class SelectorScreen : public DisplayScreen
{

  public:
    SelectorScreen();
    virtual ~SelectorScreen();
    virtual void render(char *line0, char* line1);
    virtual void processLoop(long unsigned current_millis);
    virtual void processButton(PANEL_BUTTON button);
    
  protected:
    void updateClock();
    //char channel_name[17];
    char clock_str[6];
    bool clock_dot;
};
class SignalScreen : public DisplayScreen
{
  enum SIGNAL_MODE {
    
    SM_POWER = 1,
    SM_VOLUME = 2,
    SM_SELECTOR = 3
    

  };
  public:
    SignalScreen();
    virtual ~SignalScreen();
    virtual void render(char *line0, char* line1);
    virtual void processLoop(long unsigned current_millis);
    virtual void processButton(PANEL_BUTTON button);
    
    float roundFloat(float float_in);
    
  protected:
    SIGNAL_MODE current_mode;
    void updateData();
    
    char buf_vcc[7];
    char buf_temp[7];
  //  char signal_line0[17];
  //  char signal_line1[17];
};
class DisplayDevice : private LiquidCrystal_I2C
{
  
public:

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

  void changeDisplayMode(DISPLAY_MODE mode);
  DISPLAY_MODE currentMode();
  
  void powerOn();
  void powerOff();

  //void setChannelName(const __FlashStringHelper* channel, bool is_mute);


  void showMessage(const char* text);
  void showMessage(const __FlashStringHelper* text);

  void processLoop();
  void renderFFT(uint8_t* levels);
  void renderClock(char* clock_str);
  
  void loadChars();
  //void testChars();


  void updateScreen();

  void moveCursor(int x, int y);
  
private:

  DisplayScreen *current_screen;
  
  char line0[17];
  char line1[17];

  int cursor_x;
  int cursor_y;

  DISPLAY_MODE current_mode;
  
  
  
  //showMessage buffer
  char message[17];


  
  void updateSignal();
  void updateClock();

};
extern DisplayDevice MDISPLAY;

#endif

