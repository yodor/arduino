#include <TimerOne.h>
#include <Wire.h>

#define VSENSE_PIN A3

#define ADDR_RIGHT 0x12

#define NUM_SAMPLES 10

#define BR_PIN 10
#define FR_PIN 9

// This example creates a PWM signal with 25 kHz carrier.
//
// Arduino's analogWrite() gives you PWM output, but no control over the
// carrier frequency.  The default frequency is low, typically 490 or
// 3920 Hz.  Sometimes you may need a faster carrier frequency.
//
// The specification for 4-wire PWM fans recommends a 25 kHz frequency
// and allows 21 to 28 kHz.  The default from analogWrite() might work
// with some fans, but to follow the specification we need 25 kHz.
//
// http://www.formfactors.org/developer/specs/REV1_2_Public.pdf
//
// Connect the PWM pin to the fan's control wire (usually blue).  The
// board's ground must be connected to the fan's ground, and the fan
// needs +12 volt power from the computer or a separate power supply.


// number of analog samples to take per reading


int sum = 0;                    // sum of samples taken
unsigned char sample_count = 0; // current sample number
float voltage = 0.0;            // calculated voltage
int duty_fr = 0;
int duty_br = 0;
volatile uint8_t fr_spd = 0;
volatile uint8_t br_spd = 0;
volatile bool have_command = false;

void setup(void)
{
  Serial.begin(9600);
  
  pinMode(BR_PIN, OUTPUT);
  digitalWrite(BR_PIN,LOW);
  pinMode(FR_PIN, OUTPUT);
  digitalWrite(BR_PIN,LOW);

  
  pinMode(VSENSE_PIN, INPUT);
  
  
  // 800 us = 1.25 kHz
  // 400 us = 2.5 kHz
  // 200 us = 5.0 kHz
  // 100 us = 10.0 kHz
  
  


  Wire.begin(ADDR_RIGHT);
  
  
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveEvent); // other device writes to us using beginTransmission (not used yet)

  Serial.println(F("Main_Right Setup Finished"));


  Timer1.initialize(40);

}

void loop(void)
{

  calculateVoltage();
  if (have_command) {
    setSpeed(fr_spd, br_spd);
    have_command = false;  
  }
  delay(10);
  
}


void setSpeed(uint8_t fr, uint8_t br)
{

    //spd = abs(val);
    if (fr>9)fr = 9;
    if (br>9)br = 9;
    
    if (fr == 0) {
      duty_fr = 0;
    }
    else {
      duty_fr = map(fr, 1, 9, 10, 100);
    }
    
    if (br == 0) {
      duty_br = 0;
    }
    else {
      duty_br = map(br, 1, 9, 10, 100);
    }
    

    Timer1.pwm(FR_PIN, (duty_fr / 100.0) * 1023);
    Timer1.pwm(BR_PIN, (duty_br / 100.0) * 1023);
    Serial.print(F("SetSpeed: "));
    Serial.print(fr);
    Serial.print(", ");
    Serial.println(br);
}

// respond to commands from master
// master use 
// Wire.beginTransmission(ADDR_PWR)
// Wire.write(I2C_COMMAND);
// Wire.endTransmission();


void receiveEvent(int howMany)
{

  if (howMany == 2 && have_command == false) {
    
    fr_spd = Wire.read();  
    br_spd = Wire.read();
    have_command = true;
  }
  
  
}


//return temperature and vcc and power state data
//master device should use Wire.requestFrom(ADDR_PWR, 10);
void requestEvent()
{

  static uint8_t data[2];
  data[0] = 7;
  data[1] = 8;

//  //temp
//  uint8_t buf[4];
//  memset(&buf, 0, 4);
//  AMPSHARED.writeFloat(current_temp, buf);
//  memcpy(&data[2], &buf, 4);
//
//  //vcc
//  memset(&buf, 0, 4);
//  AMPSHARED.writeFloat(current_vcc, buf);
//  memcpy(&data[6], &buf, 4);

  Wire.write(data, 2);

}



void calculateVoltage()
{
    // take a number of analog samples and add them up
  if (sample_count < NUM_SAMPLES) {
      sum += analogRead(VSENSE_PIN);
      sample_count++;
  }
  else {
      // calculate the voltage
      // use 5.0 for a 5.0V ADC reference voltage
      // 5.015V is the calibrated reference voltage
      voltage = ((float)sum / (float)NUM_SAMPLES * 5.03) / 1024.0;
      // send voltage for display on Serial Monitor
      // voltage multiplied by 11 when using voltage divider that
      // divides by 11. 11.132 is the calibrated voltage divide
      // value
      sample_count = 0;
      sum = 0;
  }
  
}

