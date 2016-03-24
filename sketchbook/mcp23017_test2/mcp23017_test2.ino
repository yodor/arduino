#include <Wire.h>
#include "Adafruit_MCP23017.h"

// Basic pin reading and pullup test for the MCP23017 I/O expander
// public domain!

// Connect pin #12 of the expander to Analog 5 (i2c clock)
// Connect pin #13 of the expander to Analog 4 (i2c data)
// Connect pins #15, 16 and 17 of the expander to ground (address selection) adr 0
// Connect pin #9 of the expander to 5V (power)
// Connect pin #10 of the expander to ground (common ground)
// Connect pin #18 through a ~10kohm resistor to 5V (reset pin, active low)

// needs 4.7K pullups on i2c

Adafruit_MCP23017 mcp;
  
void setup() {  
  
  mcp.begin(0);      // use default address 0

  mcp.pinMode(8, OUTPUT); // pin 1 on mcp

  
}



void loop() {
  // The LED will 'echo' the button
  mcp.digitalWrite(8, HIGH);
  
  delay(1000);

  mcp.digitalWrite(8, LOW);

  delay(1000);

}
