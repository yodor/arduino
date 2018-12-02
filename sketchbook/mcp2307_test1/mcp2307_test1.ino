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
  Serial.begin(9600);
   
  pinMode(13,OUTPUT);
  digitalWrite(13,HIGH);
  mcp.begin(0);      // use default address 0

  mcp.pinMode(15, OUTPUT);
  Serial.println("setup");
}

bool ps=false;


void loop() {
  // The LED will 'echo' the button
  //digitalWrite(13, mcp.digitalRead(0));
  ps=!ps;
  if (ps) {
    digitalWrite(13,HIGH);
    mcp.digitalWrite(15,HIGH);
  }
  else {
    digitalWrite(13,LOW);
    mcp.digitalWrite(15,LOW);
  }
  
  Serial.println(ps);
  delay(1000);
  //

  
}
