//Setup the output PWM pin
const int outputPin = 10;

// The number of Steps between the output being on and off
const int pwmIntervals = 300;

int led_values[] = {255, 180, 128, 90, 64, 45, 32, 23, 16, 12, 8, 6, 4, 3, 2, 1,0};

// The R value in the graph equation
float R;

void setup() {
  // set the pin connected to the LED as an output
  pinMode(outputPin, OUTPUT);

  // Calculate the R variable (only needs to be done once at setup)
  R = (pwmIntervals * log10(2))/(log10(255));

}
int index = 0;
int stp = 1;

void loop() {
  int brightness  = led_values[index];

  analogWrite(outputPin,brightness);
  delay(40);

  index+=stp;
  if (index>16 || index<0) {
    stp = -stp;
    index+=stp;
  }
}
