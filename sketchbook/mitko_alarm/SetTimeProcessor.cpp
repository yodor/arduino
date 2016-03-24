#include "SetTimeProcessor.h"
#include <Time.h>  
#include <DS3231RTC.h>  // a basic DS1307 library that returns time as a time_t

SetTimeProcessor::SetTimeProcessor()
{
  Hour = -1;
  Minute = -1;
}
SetTimeProcessor::~SetTimeProcessor()
{

}
void SetTimeProcessor::processLineGroup(int group, const String& group_str)
{
  if (group == 1) return;
  if (group == 2) {
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
}
void SetTimeProcessor::finishLine()
{  
  if (Hour<0 || Minute <0) {
    sprintf(result, "%s","Incorrect time format");
    return;
  }
  have_error = false;
  
  tmElements_t tm;
  breakTime(now(), tm);
  tm.Hour = Hour;
  tm.Minute = Minute;
  RTC.set(makeTime(tm));
  have_error = false;
  RTC.read(tm);
  setSyncProvider(RTC.get); 
  sprintf(result, "%d:%d", tm.Hour, tm.Minute);
}


