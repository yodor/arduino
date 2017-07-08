void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println(F("Setup Finished"));
}

void loop() {
  // put your main code here, to run repeatedly:
  int spd = 0;
  for (int a=0;a<1024;a++) {
      int val = (a / 100) * 100;
      Serial.println(val);
  }
}
