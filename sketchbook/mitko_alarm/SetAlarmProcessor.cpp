#include "SetAlarmProcessor.h"
#include "AlarmProcessor.h"



SetAlarmProcessor::SetAlarmProcessor()
{
  Hour = -1;
  Minute = -1;
  Timeout = -1;
  is_enable = false;
  alarm_num = 0;
  Pin = -1;
  
}
SetAlarmProcessor::~SetAlarmProcessor()
{
}
void SetAlarmProcessor::processLineGroup(int group, const String& group_str)
{
  if (group == 1) {
    return;
  }

  else if (group == 2) {
    //int alarm_num = atoi(group_str);
    group_str.toCharArray(cmd_buf, 6);
    alarm_num = atoi(cmd_buf);
    //Serial.println("Alram Number:");
    //Serial.println(alarm_num);
  }
  else if (group == 3) {
    //parse E or D
    if ( group_str.startsWith("E") || group_str.startsWith("e") ) is_enable = true;

  }
  else if (group == 4) {
    //parse TIME HH:MM
    group_str.toCharArray(cmd_buf, 6);

    if (sscanf(cmd_buf, "%d:%d", &Hour, &Minute) != 2) {
      //error
      Hour=-1;
      Minute=-1;
    }
    else {
      if (Hour < 0 || Hour>23) Hour = -1;
      if (Minute < 0 || Minute>59) Minute = -1;
    }

  }  
  else if (group == 5) {
    group_str.toCharArray(cmd_buf, 6);
    Timeout = atoi(cmd_buf);
  }
  else if (group == 6) {
    group_str.toCharArray(cmd_buf, 6);
    Pin = atoi(cmd_buf);
  }
}
void SetAlarmProcessor::finishLine()
{
  if (alarm_num > 0 && alarm_num < 10) {
    if (is_enable) {
      
      //check pin
      if (!ALARMS.isValidPin(Pin)) {
        sprintf(result, "%s", "Incorrect pin");
        return;
      }
      
      if (Hour<0 || Minute<0) {
        sprintf(result, "%s", "Incorrect time format");
        return;
      }
      if (Timeout<1) {
        sprintf(result, "%s", "Incorrect timeout format");
        return;
      }
      int existing = ALARMS.haveAlarm(Hour, Minute);
      if (existing!=-1) {
        sprintf(result, "%s", "Alarm already exists"); 
      }
      else {
        ALARMS.enableAlarm(alarm_num, Hour, Minute, Timeout, Pin);
        sprintf(result, "%d|%d:%d|%d|%d", alarm_num, Hour, Minute, Timeout, Pin); 
        have_error = false;
      }
    }
    else {
      ALARMS.disableAlarm(alarm_num);
      sprintf(result, "%d|Disabled", alarm_num);
      have_error = false;
    }
  }
  else {
    sprintf(result, "%s", "Incorrect alarm number");
  }
}

