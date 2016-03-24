#ifndef DS18B20_h
#define DS18B20_h
#include <Arduino.h>

class DS18B20 {

  public:
    DS18B20();
    ~DS18B20();
    
    bool haveDevice();
    uint8_t deviceType();
    void prepareRead();
    bool readTemp(float& result);
    
  protected:
    
    uint8_t device_type;
    unsigned long read_start;
    

};

#endif
