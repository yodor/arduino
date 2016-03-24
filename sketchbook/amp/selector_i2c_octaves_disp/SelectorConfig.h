#ifndef SELECTOR_CONFIG_H
#define SELECTOR_CONFIG_H

#include "SharedDefs.h"



typedef struct Counters_T
{
  unsigned long power_up;
  unsigned long display_update;
  unsigned long motor_state;
  unsigned long signal_update;
  unsigned long sleep_signal;
  unsigned long clock_update;
  unsigned long fft_update;
  unsigned long message_update;
  
} Counters;


typedef struct ConfigStruct_T
{
    int is_config;
    bool pulse_led_ir;
    int stored_channel;
    bool pulse_led_signal_enabled;
    int  pulse_signal_scale;
    bool pulse_led_ir_enabled;
    bool signal_sleep_enabled;
    long unsigned signal_sleep_timeout;
    int sleep_toggle_dbV;
    bool flash_clock_dots;
    unsigned long sample_window;
    unsigned long fft_refresh;

} ConfigStruct;

class SelectorConfig {

  
  public:
  
      SelectorConfig();
      ~SelectorConfig();
      
      ConfigStruct& values();
      
      void load();
      void store();
      
 private:
      ConfigStruct config_t; 
      void initConfig();
};

extern SelectorConfig CONFIG;
extern Counters COUNTERS;
#endif
