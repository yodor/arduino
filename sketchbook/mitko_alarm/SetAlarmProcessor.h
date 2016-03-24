#ifndef SET_ALARM_PROCESSOR_H
#define SET_ALARM_PROCESSOR_H

#include "CommandProcessor.h"

class SetAlarmProcessor : public CommandProcessor
{
  
public:

  SetAlarmProcessor();
  ~SetAlarmProcessor();
  
protected:

  virtual void processLineGroup(int group, const String& group_str);
  virtual void finishLine();
  
  int Hour;
  int Minute;
  int Timeout;
  int alarm_num;
  boolean is_enable;
  int Pin;
};

#endif

