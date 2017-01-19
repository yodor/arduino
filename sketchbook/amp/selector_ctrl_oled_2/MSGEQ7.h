#ifndef MSGEQ7_H
#define MSGEQ7_H

#include <Arduino.h>

class MSGEQ7 {

  public: 
    MSGEQ7();
    ~MSGEQ7();
   
    void process();
    const uint8_t* getLevels();
    
 protected:
   
    int fft[7]; // fft raw values from MSGEQ7
    uint8_t levels[7]; // scaled levels for display
  
};
extern MSGEQ7 EQ7;
#endif
