#!/bin/bash

avrdude -p atmega2560 -b 115200 -P /dev/ttyUSB0 -c stk500v2 -U flash:r:k200-flash.bin:r 
sleep 1
avrdude -p atmega2560 -b 115200 -P /dev/ttyUSB0 -c stk500v2 -U eeprom:r:k200-eeprom.bin:r 



