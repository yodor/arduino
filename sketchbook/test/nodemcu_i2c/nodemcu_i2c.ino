//Board PIN - Internal Pin 
// D0       - GPIO 16
// D1       - GPIO 5
// D2       - GPIO 4
// D3       - GPIO 0
// D4       - GPIO 2
// D5       - GPIO 14
// D6       - GPIO 12
// D7       - GPIO 13
// D8       - GPIO 15

const int ONBOARD_LED = 2;
const int SDA_PIN = 4;
const int SCL_PIN = 5;

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  Serial.begin(115200);
  Wire.begin(SDA_PIN,SCL_PIN);
  
  pinMode(ONBOARD_LED, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  digitalWrite(ONBOARD_LED, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);                       // wait for a second
  digitalWrite(ONBOARD_LED, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second
}
