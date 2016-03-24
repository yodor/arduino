/**
* https://code.google.com/p/tinkerit/wiki/SecretThermometer
*/
uint8_t buf[2];

long readTemp() {
  long result;
  // Read temperature sensor against 1.1V reference
  ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX3);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = (result - 125) * 1075;
  //
  
  buf[0] = (uint8_t) result;
  buf[1] = (uint8_t) result >> 8;

  return result;
}

void setup() {
  Serial.begin(9600);
}

void loop() {
  Serial.println( readTemp(), DEC );

  delay(1000);
}
