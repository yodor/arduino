#ifndef CHANNEL_SELECTOR_H
#define CHANNEL_SELECTOR_H

#include "SharedDefs.h"
#include "DisplayDevice.h"
#include "SelectorConfig.h"

class ChannelSelector 
{

    public:
      ChannelSelector();
      ~ChannelSelector();
      void setActiveChannel(CHANNEL channel);
      CHANNEL getActiveChannel();
      void setNextChannel();
      void setPreviousChannel();
      const __FlashStringHelper* getChannelName();

      void channelsOFF();
      
      CHANNEL current_channel;

      int sendCommand(unsigned char command, bool need_response=false, int address=BTMODULE_ADDRESS);

      void toggleMute();
      bool isMute();
      void setMute(bool mode);

      void togglePower(bool state);

    private:
      bool is_mute;
      bool bt_power;

};
extern ChannelSelector SELECTOR;
#endif
