#!/bin/bash

/opt/arduino/hardware/tools/avr/bin/avrdude -p atmega328p -b 9600 -P /dev/ttyS0 -c ponyser -U flash:r:flash.bin:r -C /opt/arduino/hardware/tools/avr/etc/avrdude.conf

