#include "ChannelSelector.h"
#include <Wire.h>


#include <Arduino.h>

#include "DisplayDevice.h"

//Channel selector
const int PIN_CH1  = 5; // chip 11
const int PIN_CH2  = 6; // chip 12
const int PIN_CH3  = 7; // chip 13
const int PIN_CH4  = 8; // chip 14


const int PIN_CUBIEPOWER = 11;


#include "Expander.h"

int channel_pins[] = { 
  PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4 
};

const __FlashStringHelper *channel_names[] = { 
  (const __FlashStringHelper *)"Channel 1", (const __FlashStringHelper *)"Cubieboard", (const __FlashStringHelper *)"Optical", (const __FlashStringHelper *)"Bluetooth"
};



ChannelSelector::ChannelSelector()
{

  is_mute = false;
  
  current_channel = CHANNEL_NONE;
  
  pinMode(PIN_CH1,OUTPUT);
  digitalWrite(PIN_CH1, LOW);
  pinMode(PIN_CH2,OUTPUT);
  digitalWrite(PIN_CH2, LOW);
  pinMode(PIN_CH3,OUTPUT);
  digitalWrite(PIN_CH3, LOW);
  pinMode(PIN_CH4,OUTPUT);
  digitalWrite(PIN_CH4, LOW); 
  
  pinMode(PIN_CUBIEPOWER, OUTPUT);
  digitalWrite(PIN_CUBIEPOWER, LOW);
  GLOBALS.cubie_power_enabled = false;
  
  channelsOFF();
  
}
ChannelSelector::~ChannelSelector()
{

}
void ChannelSelector::setMute(bool mode)
{
  if (mode != is_mute) {
    is_mute = mode;
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

  
  CHANNEL new_channel = channel;

  if (channel<AUX) new_channel = BLUETOOTH;
  if (channel>BLUETOOTH) new_channel = AUX;

  //Serial.print(F("setActiveChannel=>"));
  //Serial.println(new_channel);
  

  if (current_channel != new_channel) {

    digitalWrite(channel_pins[(int)new_channel], HIGH);
    delay(10);
    digitalWrite(channel_pins[(int)current_channel], LOW);

    if (new_channel == BLUETOOTH) {
      if (GLOBALS.bluetooth_power_enabled == false) {

        //sendCommand(CMD_BT_POWER);
        MDISPLAY.showMessage(F2("Processing ..."));
                
        EXPANDER.bluetoothPress(BT_POWER);
        
        MDISPLAY.showMessage(F2("BT Enabled ..."));
      }
    }       
    
    
    current_channel = new_channel;
    
    CONFIG.values().stored_channel = current_channel;
  
    MDISPLAY.updateScreen();
    
    CONFIG.store();
  }

  

}


const char* ChannelSelector::getChannelName()
{
  if (current_channel>CHANNEL_NONE && current_channel<CHANNEL_LAST) {
    return (const char*)channel_names[(int)current_channel];
  }
  else {
    return (const char*)F2("N/A");
  }
}
void ChannelSelector::setNextChannel()
{
  int new_channel = current_channel+1;
  setActiveChannel((CHANNEL)new_channel);
}

void ChannelSelector::setPreviousChannel()
{
  int new_channel = current_channel-1;
  setActiveChannel((CHANNEL)new_channel);
}
void ChannelSelector::channelsOFF()
{

  digitalWrite(PIN_CH1, LOW);
  digitalWrite(PIN_CH2, LOW);
  digitalWrite(PIN_CH3, LOW);
  digitalWrite(PIN_CH4, LOW);  
  current_channel = CHANNEL_NONE;

}
void ChannelSelector::setCubiePower(bool mode)
{
  if (mode == false) {
      digitalWrite(PIN_CUBIEPOWER, LOW);
      GLOBALS.cubie_power_enabled = false;
      MDISPLAY.showMessage(F2("Power Off"));
  }
  else {
      digitalWrite(PIN_CUBIEPOWER, HIGH);
      GLOBALS.cubie_power_enabled = true;
      MDISPLAY.showMessage(F2("Power On"));
  }


}
void ChannelSelector::setAmpPower(bool mode)
{
  MDISPLAY.showMessage(F2("Processing ..."));

  uint8_t cmd = 0;

  if (mode) {
    // power on amp  
    cmd = (uint8_t)(CMD_PWR_AMP_ON);
    Wire.beginTransmission(ADDR_PWR);
    Wire.write(cmd);
    Wire.endTransmission(true);  
    delay(2000);
    // set mute off
    cmd = (uint8_t)(CMD_VOL_MUTE_OFF);
    Wire.beginTransmission(ADDR_VOLUME);
    Wire.write(cmd);
    Wire.endTransmission(true);  
    delay(1000);
  }
  else {
    // set mute on
    cmd = (uint8_t)(CMD_VOL_MUTE_ON);
    Wire.beginTransmission(ADDR_VOLUME);
    Wire.write(cmd);
    Wire.endTransmission(true);  
    delay(1000);
    // power off amp
    cmd = (uint8_t)(CMD_PWR_AMP_OFF);
    Wire.beginTransmission(ADDR_PWR);
    Wire.write(cmd);
    Wire.endTransmission(true);  
    delay(2000);
    
  }
  
  GLOBALS.amp_power_enabled = mode;
  
}

ChannelSelector SELECTOR = ChannelSelector();

