#include <SoftwareSerial.h>

SoftwareSerial mySerial(10, 9); // RX-green, TX-white
void setup() {
  // put your setup code here, to run once:
  mySerial.begin(9600);
  mySerial.println("SETUP");
  
}

void loop() {
  // put your main code here, to run repeatedly:
  mySerial.println("Hello World");
  delay(1000);
}
