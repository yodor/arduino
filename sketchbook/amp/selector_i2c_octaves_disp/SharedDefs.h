#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#define  CONTROLLER_ADDRESS             0x9
#define  BTMODULE_ADDRESS               0x11

enum BT_CMD {
  CMD_NONE = -1,

  CMD_BT_POWER = 20,
  CMD_BT_FWD = 21,
  CMD_BT_BACK = 22,

  CMD_BT_VOLUP = 23,
  CMD_BT_VOLDN = 24,

  CMD_BT_PLAY = 25

};


//corresponds to BTN_PINs
enum PANEL_BUTTON {
  BUTTON_NONE = -1,
  BUTTON_UP = 0,
  BUTTON_DOWN = 1,
  BUTTON_ENTER = 2,
  BUTTON_CANCEL = 3,
  BUTTON_LAST = 4
};
enum CHANNEL {
  CHANNEL_NONE = -1,
  CH1 = 0,
  CH2 = 1,
  OPTICAL = 2,
  BLUETOOTH = 3, 
  CHANNEL_LAST = 4
};

#define RESPONSE_UNAVAILABLE        128
#define RESPONSE_CHANGING_CHANNELS  127

#define PIN_AUDIO_MONITOR    A0
#define PIN_ONBOARD_LED      A3

//config

#define F2(progmem_ptr) (const __FlashStringHelper *)progmem_ptr




#endif

