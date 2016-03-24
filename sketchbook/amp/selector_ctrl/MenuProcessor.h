#ifndef MENUPROCESSOR_H
#define MENUPROCESSOR_H

#include "SharedDefs.h"
#include "Menu.h"
#include <inttypes.h>

class MenuProcessor
{
  
public:
  MenuProcessor();

  void renderScreen(char *line1, char* line2);

  
  virtual void processButton(PANEL_BUTTON button);

protected:
  virtual void renderLine1(char *line1);
  virtual void renderLine2(char *line2);
  virtual void processCancelButton(const char* msg=0);

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
  virtual void processButton(PANEL_BUTTON button);

protected:
  CLOCK_MODE clock_adjust;
  uint8_t adj_hour;
  uint8_t adj_minute;
  virtual void renderLine2(char *line2);
  virtual void processCancelButton(const char* msg=0);

};

class SettingsMenuProcessor : public MenuProcessor
{

public:
  SettingsMenuProcessor();
  virtual void processButton(PANEL_BUTTON button);
  
protected:
  bool edit_setting;
  void renderLine2(char *line2);
  
  virtual void processCancelButton(const char *msg = 0);
  virtual void processEditDown();
  virtual void processEditUp();
  char menu_buf[17];
  virtual void renderLine2Impl(MenuItem *item);
};

class BluetoothMenuProcessor : public SettingsMenuProcessor
{

public:
  BluetoothMenuProcessor();
  
protected:
  virtual void renderLine2Impl(MenuItem *item);
  virtual void processEditDown();
  virtual void processEditUp();
};

class CubieboardMenuProcessor : public SettingsMenuProcessor
{

public:
  CubieboardMenuProcessor();
  
protected:
  virtual void renderLine2Impl(MenuItem *item);
  virtual void processEditDown();
  virtual void processEditUp();
};
#endif


