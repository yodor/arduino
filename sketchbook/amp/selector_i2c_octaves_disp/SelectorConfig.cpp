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
    config_t.stored_channel = CH1;
    config_t.pulse_led_ir_enabled = true;
    config_t.pulse_led_signal_enabled = true;
    config_t.pulse_signal_scale = 2;
    config_t.signal_sleep_enabled = true;
    config_t.signal_sleep_timeout = 600000;
    config_t.flash_clock_dots = false;
    config_t.sleep_toggle_dbV = -35;
    config_t.sample_window = 30;
    config_t.fft_refresh = 20;
    
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
    if (config_t.is_config != CHK_CONFIG_VAL) {
        initConfig();
        store();
    }
   
  
          Serial.print(F("is_config = "));
     Serial.println(config_t.is_config, DEC);
          Serial.print(F("stored_channel = "));
     Serial.println(config_t.stored_channel, DEC);
          Serial.print(F("pulse_led_ir_enabled = "));
     Serial.println(config_t.pulse_led_ir_enabled, DEC);
          Serial.print(F("pulse_led_signal_enabled = "));
     Serial.println(config_t.pulse_led_signal_enabled, DEC);
          Serial.print(F("pulse_signal_scale = "));
     Serial.println(config_t.pulse_signal_scale, DEC);
          Serial.print(F("signal_sleep_enabled = "));
     Serial.println(config_t.signal_sleep_enabled, DEC);
          Serial.print(F("signal_sleep_timeout = "));
     Serial.println(config_t.signal_sleep_timeout, DEC);
          Serial.print(F("flash_clock_dots = "));
     Serial.println(config_t.flash_clock_dots, DEC);
          Serial.print(F("sleep_toggle_dbV = "));
     Serial.println(config_t.sleep_toggle_dbV, DEC);
   
     
}
void SelectorConfig::store()
{
    config_t.is_config = CHK_CONFIG_VAL;
    EEPROM_write(ADDR_CONFIG_STORE, config_t);  
}
SelectorConfig CONFIG = SelectorConfig();
Counters COUNTERS = Counters();
