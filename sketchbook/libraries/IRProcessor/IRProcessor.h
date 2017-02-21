#ifndef IR_PROCESSOR_H
#define IR_PROCESSOR_H

#include "Arduino.h"
#include <IRremote.h>

class IRProcessor : public IRrecv {

public:

  IRProcessor(int PIN, int MAX_TIME=150);

  void process();
  unsigned long buttonPressed();
  unsigned long lastButton();
  unsigned long buttonPressTime();
  
protected:
  IRProcessor();

  unsigned long buttonCode;
  int buttonState;
  decode_results results;
  int max_time;
  unsigned long pressTime;
};

#endif //IR_PROCESSOR_H
