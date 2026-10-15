#pragma once
// The two DS18B20 sensors that sit next to the LDR boards' cells, on one 1-Wire bus (GP21,
// 4.7k pull-up). Which sensor is L and which is R is stored in flash by ROM id
// (TEMP ID L/R assigns it: warm that board's sensor with a fingertip).
#include <Arduino.h>

namespace CellTemps {
void begin();                       // set up the pin, find the sensors, load the L/R assignment
uint8_t count();                    // sensors found on the bus (0 until started)
bool begun();                       // started yet (first TEMP)
bool assigned();                    // both L and R known
// One conversion on all sensors (up to ~750 ms), then read them. out[0] = L, out[1] = R in C,
// NAN where a side is unassigned or did not answer. Returns how many sides were read.
uint8_t read(float out[2]);
void printSensors(Stream &s);       // every sensor found: id, side, temperature
bool identify(Stream &s, uint8_t side); // 0 = L, 1 = R; the other sensor gets the other side
} // namespace CellTemps
