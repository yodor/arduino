#include "SelectorConfig.h"
#include "EEPROMAnything.h"

#define ADDR_CONFIG_STORE  10

#define CHK_CONFIG_VAL 7801

SelectorConfig::SelectorConfig()
{

    
}
void SelectorConfig::initConfig()
{
    config_t.is_config = CHK_CONFIG_VAL;
    config_t.stored_channel = AUX;
    config_t.signal_sleep_enabled = true;
    config_t.signal_sleep_timeout = 600000;
    config_t.flash_clock_dots = false;
    config_t.sleep_toggle_dbV = -35;
    config_t.display_message_timeout = 1000;
    //auto power
    config_t.cubie_power_auto = true;


}
SelectorConfig::~SelectorConfig()
{

}
ConfigStruct& SelectorConfig::values()
{
  return config_t;
}
void SelectorConfig::load()
{
    EEPROM_read(ADDR_CONFIG_STORE, config_t);
    //if (config_t.is_config != CHK_CONFIG_VAL) {
        initConfig();
        store();
    //}
   
  /**
          Serial.print(F("is_config = "));
     Serial.println(config_t.is_config, DEC);
          Serial.print(F("stored_channel = "));
     Serial.println(config_t.stored_channel, DEC);
    
          Serial.print(F("signal_sleep_enabled = "));
     Serial.println(config_t.signal_sleep_enabled, DEC);
          Serial.print(F("signal_sleep_timeout = "));
     Serial.println(config_t.signal_sleep_timeout, DEC);
          Serial.print(F("flash_clock_dots = "));
     Serial.println(config_t.flash_clock_dots, DEC);
          Serial.print(F("sleep_toggle_dbV = "));
     Serial.println(config_t.sleep_toggle_dbV, DEC);
          Serial.print(F("cubie_power_auto = "));
     Serial.println(config_t.cubie_power_auto, DEC);
          Serial.print(F("display_message_timeout = "));
     Serial.println(config_t.display_message_timeout, DEC);
*/

     resetCounters();
     
}
void SelectorConfig::resetCounters()
{
  COUNTERS.power_up = 0;
  COUNTERS.display_update = 0;
  COUNTERS.motor_state = 0;
  COUNTERS.signal_update = 0;
  COUNTERS.sleep_signal = 0;
  COUNTERS.clock_update = 0;
  COUNTERS.fft_update = 0;
  COUNTERS.message_update = 0;
  //
  GLOBALS.cubie_power_enabled = true;
  GLOBALS.bluetooth_power_enabled = false;
  GLOBALS.amp_power_enabled = true;
  GLOBALS.temp_volume = 0;
  GLOBALS.temp_selector = 0;
  GLOBALS.temp_power = 0;
  GLOBALS.vcc_volume = 0;
  GLOBALS.vcc_selector = 0;
  
}
void SelectorConfig::store()
{
    config_t.is_config = CHK_CONFIG_VAL;
    EEPROM_write(ADDR_CONFIG_STORE, config_t);  
}
SelectorConfig CONFIG = SelectorConfig();
Counters COUNTERS = Counters();
Globals GLOBALS = Globals();

