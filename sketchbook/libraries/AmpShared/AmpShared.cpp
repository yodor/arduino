#include "Arduino.h"
#include "AmpShared.h"
#include "Wire.h"

AmpShared::AmpShared()
{}
void AmpShared::readDevice(int DEVICE_ADDR, uint8_t *data)
{
  Wire.requestFrom(DEVICE_ADDR, 10, true);

  int num_read = 0;
  while (Wire.available()) {

    data[num_read] = Wire.read();
    if (num_read == 9) break;
    num_read++;
  }
}
long AmpShared::readFloat(uint8_t *data, int start)
{
  long result = 0;

  for (int i = 0; i < 4; i++) {
    result <<= 8;
    result ^= (long)data[start + i] & 0xFF;
  }
  return result;
}
void AmpShared::writeFloat(unsigned long value, uint8_t buf[4])
{
    for (int i = 0; i < 4; i++) {
        buf[3 - i] = (uint8_t)(value >> (i * 8));
    }
    
}
long AmpShared::readVoltageMCU() {
  long result = 0;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  result = ADCL;
  result |= ADCH << 8;
  result = 1126400L / result; // Back-calculate AVcc in mV

  return result;

}
long AmpShared::readTempMCU() {
  long result = 0;
  // Read temperature sensor against 1.1V reference
  ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX3);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  result = ADCL;
  result |= ADCH << 8;
  result = (result - 125) * 1075;
  result += 50000;
  
  return result;
}

AmpShared AMPSHARED = AmpShared();
