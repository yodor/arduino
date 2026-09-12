#ifndef DISPLAY_DEVICE_H
#define DISPLAY_DEVICE_H

#include <Arduino.h>


class DisplayDevice {

public:

    DisplayDevice();
    ~DisplayDevice();
    void init();
    void clear();

    void print();

    void printData(const char* data);

    void setContrast(uint8_t contrast);
    void setCursor(uint8_t col, uint8_t row);
    void createChar(uint8_t location, uint8_t charmap[]);


    char* getLine0();
    char* getLine1();




protected:
  char *line0;
  char *line1;

};

#endif
