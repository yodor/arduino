int LedPin = 2;

void setup() { 
  pinMode(LedPin, OUTPUT);
  Serial.begin(115200);
  Serial.println("Setup Done");
}

void loop() {
  Serial.println("HIGH");
  digitalWrite(LedPin, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);              // wait for a second
  Serial.println("LOW");
  digitalWrite(LedPin, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);              // wait for a second  
  
}
