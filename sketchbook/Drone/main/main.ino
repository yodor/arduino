

#include <RF24.h>
#include <printf.h>

#include <TimerOne.h>
#include <Wire.h>

byte addresses[][6] = {"ctrlr","drone"};

/****************** User Config ***************************/
/***      Set this radio as radio number 0 or 1         ***/
bool radioNumber = 1;

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8);
/**********************************************************/


typedef struct  {
  unsigned long _micros;
  char cmd;
  uint8_t fl;
  uint8_t fr;
  uint8_t bl;
  uint8_t br;
} DroneData;

DroneData ctrlData;
DroneData pingData;




#define ADDR_RIGHT 0x12

#define BL_PIN 10
#define FL_PIN 9



unsigned long last_cmd = 0;
unsigned long auto_stop_timeout = 2000;
unsigned long last_ping = 0;

int duty_fl = 0;
int duty_bl = 0;
volatile uint8_t fl_spd = 0;
volatile uint8_t bl_spd = 0;

void setup(void)
{
  pinMode(BL_PIN, OUTPUT);
  analogWrite(BL_PIN,LOW);
  pinMode(FL_PIN, OUTPUT);
  analogWrite(BL_PIN,LOW);
  
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  printf_begin();

  //Timer1.initialize(200);
  
  Wire.begin();

  
  
  radio.begin();

  // Set the PA Level low to prevent power supply related issues since this is a
  // getting_started sketch, and the likelihood of close proximity of the devices. RF24_PA_MAX is default.
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  
  radio.openWritingPipe(addresses[1]);
  radio.openReadingPipe(1,addresses[0]);

  radio.startListening();
  
  
  // 800 us = 1.25 kHz
  // 400 us = 2.5 kHz
  // 200 us = 5.0 kHz
  // 100 us = 10.0 kHz

  
  radio.printDetails();

  

  stopDrone();

  //
}


void loop(void)
{

  if (radio.available())
  {
    // While there is data ready
    
    radio.read( &ctrlData, sizeof(ctrlData) );        
    
    
    
    if (ctrlData.cmd == 'm') {
      setSpeedLeft(ctrlData.fl, ctrlData.bl);
      setSpeedRight(ctrlData.fr, ctrlData.br);
    }
    
    
    
    
  }

  delay(100);
  
  if ( millis() - last_ping > 1500) {
      
      pingData.cmd='p';
      pingData._micros = millis();
 
      radio.stopListening();
      radio.write( &pingData, sizeof(pingData) );
      radio.startListening();
      
      last_ping = millis();
      Serial.println(F("Ping Sent"));
  }
  
  delay(100);

}



void stopDrone()
{
  setSpeedRight(0,0);
  setSpeedLeft(0,0);
  
 
}

void setSpeedRight(uint8_t fr, uint8_t br)
{
  Wire.beginTransmission(ADDR_RIGHT);
  Wire.write(fr);
  Wire.write(br);
  Wire.endTransmission(true);
  delay(10);
}
void setSpeedLeft(uint8_t fl, uint8_t bl)
{

    //spd = abs(val);
    if (fl>9)fl = 9;
    if (bl>9)bl = 9;
    
    if (fl == 0) {
      duty_fl = 0;
    }
    else {
      duty_fl = map(fl, 1, 9, 10,100);
    }
    
    if (bl == 0) {
      duty_bl = 0;
    }
    else {
      duty_bl = map(bl, 1, 9, 10,100);
    }
    

    //Timer1.pwm(FL_PIN, (duty_fl / 100.0) * 1024);
    //Timer1.pwm(BL_PIN, (duty_bl / 100.0) * 1024);
    analogWrite(FL_PIN, (duty_fl / 100.0) * 1024);
    //analogWrite(BL_PIN, (duty_bl / 100.0) * 1024);
    
}




int readDigit(char a)
{
  int c = (int)a - 48;
  if (c < 0) c = 0;
  if (c > 9) c = 9;
  return c;

}



