#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#include "Arduino.h"
#include "Wire.h"

#define  ADDR_VOLUME                 0x9
#define  ADDR_SELECTOR               0x10
#define  ADDR_PWR                    0x12



//corresponds to BTN_PINs
enum PANEL_BUTTON {
  BUTTON_NONE = -1,
  BUTTON_UP = 0,
  BUTTON_DOWN = 1,
  BUTTON_ENTER = 2,
  BUTTON_CANCEL = 3,
  BUTTON_LAST = 4
};

enum BT_BUTTON {
  BT_NONE = -1,
  BT_POWER = 0,
  BT_FWD = 1,
  BT_BACK = 2,
  BT_VOLUP = 3,
  BT_VOLDN = 4,
  BT_PLAY = 5,
  BT_LAST = 6
};

enum CHANNEL {
  CHANNEL_NONE = -1,
  AUX = 0,
  CUBIEBOARD = 1,
  OPTICAL = 2,
  BLUETOOTH = 3,
  CHANNEL_LAST = 4
};

enum MOTOR_STATE {
  UP = 0xFF,
  DOWN = 0x01,
  STOPPING = 0x00,
  STOPPED = 0xFE
};

enum SIGNAL_MODE {
  SM_NONE = -1,
  SM_VOLUME = 0,
  SM_POWER = 1,
  SM_SELECTOR = 2,
  SM_LAST = 3
};


#define CMD_NONE         0

#define CMD_PWR_FULL_ON  101
#define CMD_PWR_FULL_OFF 102
#define CMD_PWR_AMP_ON   103
#define CMD_PWR_AMP_OFF  104

#define CMD_VOL_MUTE_ON  51
#define CMD_VOL_MUTE_OFF 52

class AmpShared {
  public:
    AmpShared();
    
    void readDevice(int DEVICE_ADDR, uint8_t *data);
   
    long readFloat(uint8_t *data, int start);
    
    long readVoltageMCU();
    long readTempMCU();
    
    void writeFloat(unsigned long value, uint8_t buf[4]);
    
};

extern AmpShared AMPSHARED;


//int freeRam () {
//  extern int __heap_start, *__brkval;
//  int v;
//  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
//}


#endif

