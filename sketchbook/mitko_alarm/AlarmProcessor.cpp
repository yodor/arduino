#include "AlarmProcessor.h"
#include "EEPROMAnything.h"
#include <Time.h>

//const int ALARM_PIN = 9;
int valid_pins[] = {2,3, 6,7,8, 9,10,11,12,13};
bool pin_state = false;

AlarmProcessor::AlarmProcessor()
{
  int read_chk = 0;
  EEPROM_read(ADDR_CONFIG_VALID, read_chk);
  if (read_chk == chk_config) {
    EEPROM_read(ADDR_ALARM_ARRAY, alarms);
    //Serial.println("Alarms read from EEPROM");
  }
  else {
      //initialize
      EEPROM_write(ADDR_CONFIG_VALID, chk_config);
      for (int a=0;a<10;a++){
        Alarm *alarm = &alarms[a];
        alarm->enabled = false;
        alarm->Hour = -1;
        alarm->Minute = -1;
        alarm->Timeout = 0;
        alarm->Started = 0;
        alarm->Stopping = false;
        alarm->Pin = -1;
      }
      EEPROM_write(ADDR_ALARM_ARRAY, alarms);
      //Serial.println("Alarms written to EEPROM");
  }
  for (int a=0;a<10;a++) {
    pinMode(valid_pins[a], OUTPUT);
    digitalWrite(valid_pins[a], LOW);
  }

}
AlarmProcessor::~AlarmProcessor()
{}
void AlarmProcessor::listValidPins()
{
  Serial.print("OK");
  for (int a=0;a<10;a++) {
    Serial.print("|");
    Serial.print(valid_pins[a],DEC);
  }
  Serial.println();
}
bool AlarmProcessor::isValidPin(int alarm_pin)
{
  bool is_valid = false;
  for (int a=0;a<10;a++) {
      int valid_pin = valid_pins[a];
      if (alarm_pin == valid_pin) {
          is_valid = true;
          break;
      }
  }
  return is_valid;
}
int AlarmProcessor::haveAlarm(int Hour, int Minute)
{
  int found = -1;
  for (int a=0;a<10;a++){
    Alarm *alarm = &alarms[a];
    if (alarm->Hour == Hour && alarm->Minute==Minute) {
      found = a;
      break;
    }
  }
  return found;
}
void AlarmProcessor::enableAlarm(int num, int Hour, int Minute, int Timeout, int Pin)
{
  Alarm *alarm = &alarms[num-1];
  alarm->enabled = true;
  alarm->Hour = Hour;
  alarm->Minute = Minute;
  alarm->Timeout = Timeout;
  alarm->Started = 0;
  alarm->Stopping = false;
  alarm->Pin = Pin;
  EEPROM_write(ADDR_ALARM_ARRAY, alarms);
}
void AlarmProcessor::disableAlarm(int num)
{
  Alarm *alarm = &alarms[num-1];
  alarm->enabled = false;
  alarm->Hour = -1;
  alarm->Minute = -1;
  if (alarm->Started>0 ) {
      digitalWrite(alarm->Pin, LOW);
  }
  alarm->Timeout = 0;
  alarm->Stopping = false;
  alarm->Started = 0;
  alarm->Pin = -1;
  EEPROM_write(ADDR_ALARM_ARRAY, alarms);
}
void AlarmProcessor::listAlarms()
{
  char line_buf[30];
  for (int a=0;a<10;a++) {
    Alarm *alarm = &alarms[a];
    sprintf(line_buf, "%d|%s|%d:%d|%ld|%d|%ld|%d", a, (alarm->enabled ? "YES" : "NO"), alarm->Hour, alarm->Minute, alarm->Timeout, alarm->Pin, alarm->Started, alarm->Stopping);
    Serial.println(line_buf);
  }
}
void AlarmProcessor::processISR()
{
  
  pin_state = !pin_state;
  
  digitalWrite(9, (pin_state ? HIGH : LOW));
  
  for (int a=0;a<10;a++){
    Alarm *alarm = &alarms[a];

    if (alarm->Started>0 && ( (millis() - alarm->Started ) >= alarm->Timeout) ) {
       
        digitalWrite(alarm->Pin, LOW);
        alarm->Started = 0;
        alarm->Stopping = true;
        continue;
    }
    
    if (alarm->Hour == hour() && alarm->Minute == minute() ) {
        if (alarm->Started == 0 && alarm->Stopping == false && alarm->enabled == true) {
          digitalWrite(alarm->Pin, HIGH);
          alarm->Started = millis();
          alarm->Stopping = false;
          //Serial.println(cmillis);
          continue;
        }
    }
    else {
        if (alarm->Stopping == true) {
          alarm->Stopping = false;
        }
    }

  }
}
AlarmProcessor ALARMS = AlarmProcessor();


