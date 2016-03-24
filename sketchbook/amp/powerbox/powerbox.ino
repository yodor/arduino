#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println(F("Dallas Temperature IC Control Library Demo"));

  // Start up the library
  sensors.begin();
}

void loop() {
  
  sensors.requestTemperatures();
  // put your main code here, to run repeatedly:
  float tempC = sensors.getTempCByIndex(0);
  if (tempC != DEVICE_DISCONNECTED)
  {
    Serial.print("Current Temp C: ");
    Serial.print(tempC);
  }
  else Serial.print("DEVICE DISCONNECTED");
  Serial.print(" ");
}
