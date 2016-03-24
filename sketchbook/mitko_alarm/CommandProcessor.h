#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <Arduino.h>

class CommandProcessor
{

  public:
  
    bool have_error;
    char result[80];

    CommandProcessor();
    virtual ~CommandProcessor();
    
    void processLine(const String& line);
    
protected:    
    virtual void finishLine() = 0;
    virtual void processLineGroup(int group_num, const String& group_str) = 0;
    
    
    char cmd_buf[6];
};





#endif
