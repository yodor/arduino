#ifndef EXPANDER_CTRL 
#define ExPANDER_CTRL

#include "AmpShared.h"

#include <Arduino.h>

#include "Adafruit_MCP23017.h"


class Expander : public Adafruit_MCP23017 {

  public:
    Expander();
    ~Expander();
    void start();
    
    void readButtons();
    
    PANEL_BUTTON getPressedButton();
    PANEL_BUTTON getMenuToggleButton();
    
    void bluetoothPress(BT_BUTTON btn);
    
   
    
  protected:
    void clearButtonStates();
    
    unsigned long button_state[4];
    PANEL_BUTTON current_press;
    PANEL_BUTTON menu_toggle_button;

    
};
extern Expander EXPANDER;
#endif
