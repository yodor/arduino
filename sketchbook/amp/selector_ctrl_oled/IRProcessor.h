#ifndef IR_PROCESSOR
#define IR_PROCESSOR
#include <IRremote.h>
#include "IRButtons.h"


class IRProcessor : public IRrecv {

public:

  IRProcessor(int pin);
  ~IRProcessor();

  void process();
  unsigned long buttonPressed();
  unsigned long lastButton();
  
protected:
  IRProcessor();

  unsigned long buttonCode;
  int buttonState;
  decode_results results;
};

#endif IR_PROCESSOR
