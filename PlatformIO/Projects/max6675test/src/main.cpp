#include <Arduino.h>
#include "max6675.h"

int thermoDO = 4;
int thermoCS = 5;
int thermoCLK = 6;

MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println("Hello World");
  delay(500);
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("C = "); 
  Serial.println(thermocouple.readCelsius());
  Serial.print("F = ");
  Serial.println(thermocouple.readFahrenheit());

  // For the MAX6675 to update, you must delay AT LEAST 250ms between reads!
  delay(1000);
}