
#include "Wire.h"
#define EXPANDER_ADDRESS 0x20
void setup()
{
 Wire.begin(); // wake up I2C bus
// set I/O pins to outputs
 Wire.beginTransmission(EXPANDER_ADDRESS);
 Wire.write(0x00); // IODIRA register
 Wire.write(0x00); // set all of port A to outputs
 Wire.endTransmission();
 Wire.beginTransmission(EXPANDER_ADDRESS);
 Wire.write(0x01); // IODIRB register
 Wire.write(0x00); // set all of port B to outputs
 Wire.endTransmission();
}
void binaryCount()
{
 for (byte a=0; a<256; a++)
 {
   Wire.beginTransmission(EXPANDER_ADDRESS);
   Wire.write(0x12); // GPIOA
   Wire.write(a); // port A
   Wire.endTransmission();
   Wire.beginTransmission(EXPANDER_ADDRESS);
   Wire.write(0x13); // GPIOB
   Wire.write(a); // port B
   Wire.endTransmission();
 }
}
void loop()
{
 binaryCount();
 delay(500);
}
