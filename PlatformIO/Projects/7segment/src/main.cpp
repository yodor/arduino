#include <Arduino.h>

#include <SegmentDisplay.h>

#include <max6675.h>

const int thermoDO = 12;
const int thermoCS = 11;
const int thermoCLK = 13;
const unsigned long UPDATE_INTERVAL = 500; // ms

SegmentDisplay display;
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

void setup()
{
  
  display.setValue(1000);
  delay(500);
}

void loop()
{
  
  static unsigned long lastUpdateTime = 0;
  unsigned long currentTime = millis();

  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
    float temperature = thermocouple.readCelsius();
    display.setValue(temperature);
    lastUpdateTime = currentTime;
  }

  display.refresh();
}