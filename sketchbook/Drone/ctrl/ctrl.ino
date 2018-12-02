#include <RF24.h>
#include <printf.h>

byte addresses[][6] = {"ctrlr", "drone"};

/****************** User Config ***************************/
/***      Set this radio as radio number 0 or 1         ***/
bool radioNumber = 0;

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

DroneData dronData;
DroneData pingData;


String cmd = "";
int pos = 0;
bool debug_enabled = true;

unsigned long last_cmd = 0;
unsigned long auto_stop_timeout = 2000;
unsigned long last_ping = 0;



void setup(void)
{

  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  printf_begin();
  
  radio.begin();

  // Set the PA Level low to prevent power supply related issues since this is a
  // getting_started sketch, and the likelihood of close proximity of the devices. RF24_PA_MAX is default.
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);

  radio.openWritingPipe(addresses[0]);
  radio.openReadingPipe(1,addresses[1]);
  
  
  
  radio.startListening();

  Serial.println(F("CTRL Setup Finished"));
  
  radio.printDetails();

}


void loop(void)
{

  if (Serial.available() > 0) {
    char a = Serial.read();

    if (cmd == "m") {
      processMove(a);

    }
    else if (cmd == "d") {
      processDebug(a);
    }
    else if (cmd == "") {
      //start character of a recognized command?
      if (a == 'm') {
        cmd = a;
      }
      else if (a == 'l') {
        cmd = a;
      }
      else if (a == 'd') {
        cmd = a;
      }
      if (cmd == "") {
        //processSimple(a);
      }

    }
  } // Serial.available()>0


  if (radio.available())
  {
    while(radio.available()) {
      radio.read(&pingData, sizeof(pingData));
    }
    Serial.print(F("Drone Sent: "));
    Serial.print(pingData._micros);
    Serial.print(F(" CMD:"));
    Serial.println(pingData.cmd);
    Serial.print(F(" FL:"));
    Serial.println(pingData.fl);
    Serial.print(F(" FR:"));
    Serial.println(pingData.fr);
    Serial.print(F(" BL:"));
    Serial.println(pingData.bl);
    Serial.print(F(" BR:"));
    Serial.println(pingData.br);
  }

 
}


void finishCommand()
{
  //clear cmd
  cmd = ""; // command name
  pos = 0;  // position in command buffer
  last_cmd = millis();  // for autostop

  
}




void processDebug(char a)
{
  int val = readDigit(a);
  if (val > 0) {
    debug_enabled = true;
    Serial.println(F("DEBUG ON"));
  }
  else {
    debug_enabled = false;
    Serial.println(F("DEBUG OFF"));
  }
}

void processMove(char a)
{
  static int buf[4] = {0, 0, 0, 0};

  //execute command
  if (pos < 4) {
    //0=> front left motor speed from 0 to 9
    //1=> front right motor speed from 0 to 9
    //2=> back left motor speed from 0 to 9
    //3=> back right motor speed from 0 to 9
    buf[pos] = readDigit(a);
    pos++;
  }

  if (pos == 4) {

    if (debug_enabled) Serial.print(F("MOVE: "));
    dronData.cmd = 'm';
    dronData._micros = millis();
    dronData.fl = (uint8_t)buf[0];
    dronData.fr = (uint8_t)buf[1];
    dronData.bl = (uint8_t)buf[2];
    dronData.br = (uint8_t)buf[3];
    
//    // First, stop listening so we can talk.
    radio.stopListening();                                    

    if (!radio.write( &dronData, sizeof(dronData) )){
        Serial.println(F("Sending failed"));
    }
    else {
        Serial.println(F("Sent move cmd"));  
    }
//    
    radio.startListening();
    delay(5);
    
    finishCommand();
    
  }

}
int readDigit(char a)
{
  int c = (int)a - 48;
  if (c < 0) c = 0;
  if (c > 9) c = 9;
  return c;

}



