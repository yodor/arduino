#define PIN_OPTIN 7

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(PIN_OPTIN,INPUT);
  digitalWrite(PIN_OPTIN, HIGH);
  
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("V=");
  Serial.print(digitalRead(PIN_OPTIN));
  Serial.println("\n");
  delay(100);
}
