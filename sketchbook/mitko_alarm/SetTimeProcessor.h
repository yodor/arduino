#ifndef SET_TIME_PROCESSOR_H
#define SET_TIME_PROCESSOR_H

#include "CommandProcessor.h"

class SetTimeProcessor : public CommandProcessor
{

public:
  int Hour;
  int Minute;

  SetTimeProcessor();
  ~SetTimeProcessor();
  
protected:

  virtual void processLineGroup(int group, const String& group_str);
  virtual void finishLine();

};
#endif

