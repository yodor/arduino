#include <Wire.h>
#include "Adafruit_MCP23017.h"

// Basic pin reading and pullup test for the MCP23017 I/O expander
// public domain!

// Connect pin #12 of the expander to Analog 5 (i2c clock)
// Connect pin #13 of the expander to Analog 4 (i2c data)
// Connect pins #15, 16 and 17 of the expander to ground (address selection)
// Connect pin #9 of the expander to 5V (power)
// Connect pin #10 of the expander to ground (common ground)

// Input #0 is on pin 21 so connect a button or switch from there to ground

Adafruit_MCP23017 mcp;
  
void setup() { 
 
  Wire.begin();
  
  mcp.begin();      // use default address 0

  mcp.pinMode(8, INPUT);
  mcp.pullUp(8, HIGH);  // turn on a 100K pullup internally

  mcp.pinMode(9, INPUT);
  mcp.pullUp(9, HIGH);  // turn on a 100K pullup internally

  mcp.pinMode(10, INPUT);
  mcp.pullUp(10, HIGH);  // turn on a 100K pullup internally

  mcp.pinMode(11, INPUT);
  mcp.pullUp(11, HIGH);  // turn on a 100K pullup internally

  mcp.pinMode(2, OUTPUT);
  mcp.digitalWrite(2, LOW);
  
  mcp.pinMode(3, OUTPUT);
  mcp.digitalWrite(3, LOW);
  
  mcp.pinMode(4, OUTPUT);
  mcp.digitalWrite(4, LOW);
  
  mcp.pinMode(5, OUTPUT);
  mcp.digitalWrite(5, LOW);
  
  mcp.pinMode(6, OUTPUT);
  mcp.digitalWrite(6, LOW);
  
  mcp.pinMode(7, OUTPUT);
  mcp.digitalWrite(7, LOW);
  
  pinMode(2, OUTPUT);  // use the p13 LED as debugging
}



void loop() {
  // The LED will 'echo' the button
  //
  int button_value = mcp.digitalRead(8);
  if (button_value == LOW) {
    mcp.digitalWrite(7,HIGH);
  }
  else {
    mcp.digitalWrite(7,LOW);
  }
  //mcp.digitalWrite(7,HIGH);
  //delay(4000);
  //mcp.digitalWrite(7,LOW);
  //delay(4000);
}
