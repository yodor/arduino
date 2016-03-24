#ifndef SELECTOR_CONFIG_H
#define SELECTOR_CONFIG_H

#include "SharedDefs.h"

typedef struct Globals_T
{
  bool cubie_power_enabled;
  bool bluetooth_power_enabled;
  bool amp_power_enabled;
  
  long vcc_volume;
  long vcc_selector;
  long vcc_power;
  
  float temp_volume;
  float temp_selector;
  float temp_power;
  
} Globals;

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
    
    int stored_channel;
   
    bool signal_sleep_enabled;
    long unsigned signal_sleep_timeout;
    int sleep_toggle_dbV;
    bool flash_clock_dots;
    unsigned long sample_window;
    unsigned long fft_refresh;

    bool cubie_power_auto;
    long unsigned display_message_timeout;


} ConfigStruct;

class SelectorConfig {

  
  public:
  
      SelectorConfig();
      ~SelectorConfig();
      
      ConfigStruct& values();
      
      void load();
      void store();
      
      void resetCounters();
      
 private:
      ConfigStruct config_t; 
      void initConfig();
};

extern SelectorConfig CONFIG;
extern Counters COUNTERS;
extern Globals GLOBALS;
#endif
