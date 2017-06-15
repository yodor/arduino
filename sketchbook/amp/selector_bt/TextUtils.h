#ifndef TEXTUTILS_H
#define TEXTUTILS_H

#include <Arduino.h>
#include "AmpShared.h"

class TextUtils {
  
public:
  TextUtils();
  ~TextUtils();
  
  const char* getChannelName(CHANNEL idx);
  const char* getScreenName(SIGNAL_MODE idx);
  const char* getText(const __FlashStringHelper* ptr);

private:
  char buf[17];
  
};

extern TextUtils TEXTS;

#endif
