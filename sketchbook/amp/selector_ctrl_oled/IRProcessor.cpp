
#include "IRProcessor.h"
#include <IRremote.h>
#include "IRButtons.h"

#define MAX_TIME 150

long lastPressTime = 0;

IRProcessor::IRProcessor(int PIN_NUM) : IRrecv(PIN_NUM), buttonCode(0), buttonState(LOW)
{

}
IRProcessor::~IRProcessor()
{

}
void IRProcessor::process()
{
  
  if (decode(&results)) {
    unsigned long code = results.value;
    
    if (results.value == BTN_REPEAT) {
      code = buttonCode;
    }
    
    if (buttonState == LOW) {
      buttonState = HIGH;  // Button pressed, so set state to HIGH
      buttonCode = code;
    }
    lastPressTime = millis();

    resume(); // Receive the next value
  }

  if (buttonState == HIGH && millis() - lastPressTime > MAX_TIME) {
    buttonState = LOW; // Haven't heard from the button for a while, so not pressed
    
  }
  
}
unsigned long IRProcessor::buttonPressed()
{  
  return ( (buttonState == HIGH) ? buttonCode : 0x0 );
}
unsigned long IRProcessor::lastButton()
{
  unsigned long code = buttonCode;
  if (buttonState == LOW) {
    buttonCode = 0;
  }
  return code;
}
