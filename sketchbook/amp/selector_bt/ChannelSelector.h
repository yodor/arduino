#ifndef CHANNEL_SELECTOR_H
#define CHANNEL_SELECTOR_H

#include "AmpShared.h"

#include "SelectorConfig.h"

class ChannelSelector 
{

    public:
      ChannelSelector();
      ~ChannelSelector();

      void setActiveChannel(CHANNEL channel);
      CHANNEL getActiveChannel();


      
      
      CHANNEL current_channel;

      void setMute(bool mode);
      bool isMute();


      
    private:
      bool is_mute;
      //bool bt_power;

};
extern ChannelSelector SELECTOR;
#endif
