#ifndef MENUPROCESSOR_H
#define MENUPROCESSOR_H

#include "SharedDefs.h"
#include "Menu.h"
#include <inttypes.h>

class MenuProcessor
{
  
public:
  MenuProcessor();
  ~MenuProcessor();
  
  void renderScreen(char *line1, char* line2);

  
  virtual void processButton(PANEL_BUTTON button);

protected:
  virtual void renderLine1(char *line1);
  virtual void renderLine2(char *line2);
  virtual void processCancelButton();

  
};

class ClockMenuProcessor : public MenuProcessor
{

public:

  enum CLOCK_MODE {
    CLOCK_INIT = 0,
    CLOCK_HOUR = 1,
    CLOCK_MINUTE = 2
  };

  ClockMenuProcessor();
  ~ClockMenuProcessor();
  
  virtual void processButton(PANEL_BUTTON button);

protected:
  CLOCK_MODE clock_adjust;
  uint8_t adj_hour;
  uint8_t adj_minute;
  virtual void renderLine2(char *line2);
  virtual void processCancelButton();

};

class SettingsMenuProcessor : public MenuProcessor
{

public:
  SettingsMenuProcessor();
  ~SettingsMenuProcessor();
  virtual void processButton(PANEL_BUTTON button);
  
protected:
  bool edit_setting;
  char menu_buf[17];
  
  void renderLine2(char *line2);
  
  virtual void processCancelButton();
  virtual void processEditDown();
  virtual void processEditUp();
  
  virtual void renderLine2Impl(MenuItem *item);
};

class BluetoothMenuProcessor : public SettingsMenuProcessor
{

public:
  BluetoothMenuProcessor();
  ~BluetoothMenuProcessor();
  
protected:
  virtual void renderLine2Impl(MenuItem *item);
  virtual void processEditDown();
  virtual void processEditUp();
};

class CubieboardMenuProcessor : public SettingsMenuProcessor
{

public:
  CubieboardMenuProcessor();
  ~CubieboardMenuProcessor();
  
protected:
  virtual void renderLine2Impl(MenuItem *item);
  virtual void processEditDown();
  virtual void processEditUp();
};
#endif


