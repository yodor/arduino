#include "Arduino.h"
#include "IRProcessor.h"

#include "IRButtons.h"


long lastPressTime = 0;

IRProcessor::IRProcessor(int PIN_NUM, int MAX_TIME) : 
    IRrecv(PIN_NUM), 
    buttonCode(0), 
    buttonState(LOW), 
    max_time(MAX_TIME),
    pressTime(0)
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
      pressTime = millis();
    }
    lastPressTime = millis();

    resume(); // Receive the next value
  }

  if (buttonState == HIGH && millis() - lastPressTime > max_time) {
    buttonState = LOW; // Haven't heard from the button for a while, so not pressed
    pressTime = 0;
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
unsigned long IRProcessor::buttonPressTime()
{
    return pressTime;
    
}
