#!/bin/bash
/opt/avr/bin/avrdude -p atmega328p -b 9600 -P /dev/ttyS0 -c ponyser -U flash:w:flash.bin
