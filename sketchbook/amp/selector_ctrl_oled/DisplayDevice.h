#ifndef MENU_PROCESSOR_H
#define MENU_PROCESSOR_H

#include "SharedDefs.h"
#include "SelectorConfig.h"

#include <DS3231RTC.h>  // a basic DS1307 library that returns time as a time_t
#include <Arduino.h>
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
    Menu *main;
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
    char clock_str[10];
    bool clock_dot;
};
class SignalScreen : public DisplayScreen
{

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
class DisplayDevice 
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
  void showMessage(const __FlashStringHelper *ptr);

  void processLoop();
  void renderFFT(const uint8_t* levels);
  void renderClock(char* clock_str);
  
  void loadChars();
  //void testChars();


  void updateScreen();

  void moveCursor(int x, int y);
  
  void setContrast(unsigned char contrast);
  
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
  
  void createChar(uint8_t location, uint8_t charmap[]);
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  
  void print(const char* data);
  void setCursor(uint8_t col, uint8_t row);
  void init();
  void clear();
  void invertDisplay(bool mode);
  void enableCursor(bool mode);
  bool cursor_enabled;

};
extern DisplayDevice MDISPLAY;

#endif

