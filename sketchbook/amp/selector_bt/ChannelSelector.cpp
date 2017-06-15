#include "ChannelSelector.h"
#include <Wire.h>


#include <Arduino.h>

#include "DisplayDevice.h"

const int PIN_MUTE_LOCAL = 10;

#include "Expander.h"


ChannelSelector::ChannelSelector()
{

  is_mute = false;
  
  current_channel = CHANNEL_NONE;
  
  pinMode(PIN_MUTE_LOCAL, OUTPUT);
  digitalWrite(PIN_MUTE_LOCAL, LOW); 
  


}

ChannelSelector::~ChannelSelector()
{

}

void ChannelSelector::setMute(bool mode)
{
  if (mode != is_mute) {
    is_mute = mode;
    
    digitalWrite(PIN_MUTE_LOCAL, is_mute); 

    MDISPLAY.updateScreen();
    
  }
}

bool ChannelSelector::isMute()
{
  return is_mute;
}

CHANNEL ChannelSelector::getActiveChannel()
{
  return current_channel;
}

void ChannelSelector::setActiveChannel(CHANNEL channel)
{ 

  if (GLOBALS.bluetooth_power_enabled == false) {

    //sendCommand(CMD_BT_POWER);
    MDISPLAY.showMessage(F("Processing ..."));
            
    EXPANDER.bluetoothPress(BT_POWER);
    
    MDISPLAY.showMessage(F("BT Enabled ..."));
  }
 
    
  current_channel = BLUETOOTH;
  
  CONFIG.values().stored_channel = BLUETOOTH;

  MDISPLAY.updateScreen();
  
  CONFIG.store();

}





ChannelSelector SELECTOR = ChannelSelector();

