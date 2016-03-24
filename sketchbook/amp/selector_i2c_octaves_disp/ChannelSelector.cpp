#include "ChannelSelector.h"
#include <Wire.h>


#include <Arduino.h>

//Channel selector
const int PIN_CH1  = 0; // chip 5
const int PIN_CH2  = 1; // chip 6
const int PIN_CH3  = 2; // chip 7
const int PIN_CH4  = 3; // chip 8

const int PIN_MUTE  = A2;

int channel_pins[] = { 
  PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4 
};

const __FlashStringHelper *channel_names[] = { 
  (const __FlashStringHelper *)"Channel 1", (const __FlashStringHelper *)"Channel 2", (const __FlashStringHelper *)"Optical", (const __FlashStringHelper *)"Bluetooth"
};



ChannelSelector::ChannelSelector()
{
  bt_power = false;
  is_mute = false;

  current_channel = CHANNEL_NONE;
  
  pinMode(PIN_CH1,OUTPUT);
  pinMode(PIN_CH2,OUTPUT);
  pinMode(PIN_CH3,OUTPUT);
  pinMode(PIN_CH4,OUTPUT);
  
  channelsOFF();

  pinMode(PIN_MUTE, OUTPUT);
  //mute => off
  digitalWrite(PIN_MUTE, HIGH);
  
  //m_display = 0;

  
}
ChannelSelector::~ChannelSelector()
{

}
void ChannelSelector::togglePower(bool state)
{
    if (state == false) {
            
      setMute(true);
      
      channelsOFF();
      
      bt_power = false;
     
      
    }
    else if (state == true) {
      
      setMute(false); 
      
    }
    
}
CHANNEL ChannelSelector::getActiveChannel()
{
  return current_channel;
}
void ChannelSelector::setActiveChannel(CHANNEL channel)
{ 

  Serial.print(F("setActiveChannel=>"));
  Serial.println(channel);
  
  CHANNEL new_channel = channel;

  if (channel<CH1) new_channel = BLUETOOTH;
  if (channel>BLUETOOTH) new_channel = CH1;


  if (current_channel != new_channel) {

    channelsOFF();

    current_channel = new_channel;

    int pin_channel = channel_pins[(int)current_channel];
    digitalWrite(pin_channel, LOW);

    if (current_channel == BLUETOOTH) {
      if (bt_power == false) {

        sendCommand(CMD_BT_POWER);

        
        MDISPLAY.showMessage(F2("Processing ..."));
       
        delay(4000);

        bt_power = true;
        
        MDISPLAY.showMessage(F2("BT Enabled ..."));
      }

    }       
    else {



    }

  }

  MDISPLAY.setChannelName(getChannelName(), is_mute);

}
const __FlashStringHelper* ChannelSelector::getChannelName()
{
  if (current_channel>CHANNEL_NONE && current_channel<CHANNEL_LAST) {
    return channel_names[(int)current_channel];
  }
  else {
    return F("N/A");
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

  digitalWrite(PIN_CH1, HIGH);
  digitalWrite(PIN_CH2, HIGH);
  digitalWrite(PIN_CH3, HIGH);
  digitalWrite(PIN_CH4, HIGH);  
  current_channel = CHANNEL_NONE;

}

int ChannelSelector::sendCommand(unsigned char command, bool need_response, int address)
{
  //power is off - do nothing
  //if (!power_enabled)return 0;

  int ret = 0;

  Wire.beginTransmission(address);
  Wire.write(command);
  Wire.endTransmission(true);

  if (need_response) {
    delay(10);
    Wire.requestFrom(address, 1,true);
    ret = Wire.read();
  }

  return ret;

}
void ChannelSelector::setMute(bool mode)
{
  if (mode == true) {
    digitalWrite(PIN_MUTE, LOW);
  }
  else if (mode == false) {
    digitalWrite(PIN_MUTE, HIGH);
  }
  is_mute = mode;
  
}
bool ChannelSelector::isMute()
{
    return is_mute;
}
void ChannelSelector::toggleMute()
{
  is_mute = !is_mute;

  if (is_mute == true) {
    digitalWrite(PIN_MUTE, LOW);
  }
  else if (is_mute == false) {
    digitalWrite(PIN_MUTE, HIGH);
  }
  
  
  MDISPLAY.setChannelName(getChannelName(), is_mute);
  
}

ChannelSelector SELECTOR = ChannelSelector();
//void ChannelSelector::setDisplay(DisplayDevice *device)
//{
//  m_display = device;
//}
//void ChannelSelector::setConfig(SelectorConfig *pConfig)
//{
//  cfg = pConfig;
//}

