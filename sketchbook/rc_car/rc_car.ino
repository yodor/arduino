
#include <Wire.h>

#define ADDR_I2C_VORTEX 0x55


//pin numbers
#define M1_EN   3
#define M1_L    2
#define M1_R   10

#define M2_EN   9
#define M2_L    7
#define M2_R    8


//indexes in side array mot
#define MOT_EN  0
#define MOT_L   1
#define MOT_R   2

/**
////            ////
////----  ------////
////   |  |  |  ////
       |  |  |
       |  |  |
       |  |  |
       |  |  |
////   |  |  |  ////
////----  ------////
////            ////
*/

int mot[2][3] = {
                { M1_EN, M1_L, M1_R }, //Motor 0 static side
                { M2_EN, M2_L, M2_R }, //Motor 1 rotating side
};

String version_string = "vortex ctrl v1.0";

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

//---------------------------------------------- Set PWM frequency for D9 & D10 ------------------------------
TCCR1B = TCCR1B & B11111000 | B00000001;    // set timer 1 divisor to     1 for PWM frequency of 31372.55 Hz
//TCCR1B = TCCR1B & B11111000 | B00000010;    // set timer 1 divisor to     8 for PWM frequency of  3921.16 Hz
//TCCR1B = TCCR1B & B11111000 | B00000011;    // set timer 1 divisor to    64 for PWM frequency of   490.20 Hz (The DEFAULT)
//TCCR1B = TCCR1B & B11111000 | B00000100;    // set timer 1 divisor to   256 for PWM frequency of   122.55 Hz
//TCCR1B = TCCR1B & B11111000 | B00000101;    // set timer 1 divisor to  1024 for PWM frequency of    30.64 Hz

//---------------------------------------------- Set PWM frequency for D3 & D11 ------------------------------
TCCR2B = TCCR2B & B11111000 | B00000001;    // set timer 2 divisor to     1 for PWM frequency of 31372.55 Hz
//TCCR2B = TCCR2B & B11111000 | B00000010;    // set timer 2 divisor to     8 for PWM frequency of  3921.16 Hz
//TCCR2B = TCCR2B & B11111000 | B00000011;    // set timer 2 divisor to    32 for PWM frequency of   980.39 Hz
//TCCR2B = TCCR2B & B11111000 | B00000100;    // set timer 2 divisor to    64 for PWM frequency of   490.20 Hz (The DEFAULT)
//TCCR2B = TCCR2B & B11111000 | B00000101;    // set timer 2 divisor to   128 for PWM frequency of   245.10 Hz
//TCCR2B = TCCR2B & B11111000 | B00000110;    // set timer 2 divisor to   256 for PWM frequency of   122.55 Hz
//TCCR2B = TCCR2B & B11111000 | B00000111;    // set timer 2 divisor to  1024 for PWM frequency of    30.64 Hz

 
  // activate internal pullups for twi.
  digitalWrite(SDA, 1);
  digitalWrite(SCL, 1);
  
  pinMode(M1_EN, OUTPUT);
  analogWrite(M1_EN, 0);

  pinMode(M2_EN, OUTPUT);
  analogWrite(M2_EN, 0);

  pinMode(M1_L, OUTPUT);
  digitalWrite(M1_L, 0);
  pinMode(M1_R, OUTPUT);
  digitalWrite(M1_R, 0);

  pinMode(M2_L, OUTPUT);
  digitalWrite(M2_L, 0);
  pinMode(M2_R, OUTPUT);
  digitalWrite(M2_R, 0);


  Serial.println(F("SETUP"));
}


// Buffer to store incoming commands from serial port
String inData = "";
bool have_line = false;
unsigned long auto_off_timeout = 1000;
unsigned long last_motor_command = 0;

void serialEvent()
{
    while (Serial.available())
    {
        // get the new byte
        char inChar = Serial.read();
        
        //0-9 || a-z || A-Z
        if ( (inChar >= 43 && inChar <= 59) 
              || (inChar >= 65 && inChar <= 70) 
              || (inChar >= 97 && inChar <= 122)
              || (inChar == 124) 
              || (inChar == 61) ) 
        {
          inData += inChar;  
        }
        
        // Process message when new line character is recieved
        else if (inChar == '\n' || inChar == '\r')
        {
            inData.toUpperCase();
            have_line = true;
            break;
        }
         
    }
}

void i2c_on()
{

  
  
  Wire.begin(ADDR_I2C_VORTEX);
  Wire.setClock(50000L);

  
  Wire.onRequest(requestEvent); // other device writes to us using requestFrom
  Wire.onReceive(receiveEvent); // other device writes to us using beginTransmission (not used yet)

}
void i2c_off()
{
  Wire.end();

  
}

/**
  * This function returns a single string separated by a predefined character at a given index. 
  * For example:
  * String split = "hi this is a split test";
  * String word_index_3 = getValue(split, ' ', 2); // = "is"
*/

String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void loop()
{ 
   
    if (last_motor_command>0 && auto_off_timeout>0 && (millis() - last_motor_command > auto_off_timeout)) {
      Serial.println(F("AUTOSTOP"));
      driveStop();
      last_motor_command = 0;

    }
    
    if (have_line) {
      if (inData.equals("AT+V")) {
        
        Serial.println(version_string);
          
      }
      else if (inData.startsWith("AT+RGBLED")) {
          inData = inData.substring(9);
          int r_val = getValue(inData,',',0).toInt();
          int g_val = getValue(inData,',',1).toInt();
          int b_val = getValue(inData,',',2).toInt();
          
      }
      else if (inData.startsWith("AT")) {
        processCommand(inData);     
        
      }
      else {
        printError(F("Unrecognized Command"));
      }  
      
      have_line = false;
      inData = "";
    }
    


  
}

void printError(String errstr)
{
  Serial.print(F("ERR: "));
  Serial.println(errstr);
}

void processCommand(String inData)
{
  //AT:
  String cmd = inData.substring(2);
  Serial.print("CMD: ");
  Serial.println(cmd);
  
  if (cmd.startsWith("M")) {

      cmd = cmd.substring(1);
      
      int pipe = cmd.indexOf("|");
      if (pipe<0) {
        printError(F("Incorrect command"));
        return;
      }

      String mot_l = cmd.substring(0,pipe);
      String mot_r = cmd.substring(pipe+1);

      if (mot_r.length()<1 || mot_l.length()<1) {
        printError(F("Incorrect command"));    
        return;
      }

      motor_ctrl(0, mot_l.toInt());
      motor_ctrl(1, mot_r.toInt());
      
  }
  else if (cmd.startsWith("ST")) {
      driveStop();
  }
  else if (cmd.startsWith("ASE")) {
      auto_off_timeout = 1000;
      Serial.println(F("AUTOSTOP enabled"));
  }
  else if (cmd.startsWith("ASD")) {
      auto_off_timeout = 0;  
      Serial.println(F("AUTOSTOP disabled"));
  }
  else if (cmd.startsWith("F")) {
    driveForward();  
  }
  else if (cmd.startsWith("B")) {
    driveBackward();  
  }
  else if (cmd.startsWith("L")) {
    driveLeft();  
  }
  else if (cmd.startsWith("R")) {
    driveRight();  
  }
  else if (cmd.startsWith("SPD")) {
      
    String speed_value = cmd.substring(3); 
    setDriveSpeed(speed_value.toInt());
      
  }
  else {
      printError(F("Unknown subcommand"));  
  }

  //if (auto_off_timeout>0) {
  //  delay(auto_off_timeout);
  //  driveStop();
  //}
 
  
}
//ATM255|255
void motor_ctrl(int num, int spd)
{

      
    
      analogWrite(mot[num][MOT_EN], abs(spd)); //spd==0 pwm = high 255-abs(spd)
      
      if (spd>0) {
        digitalWrite(mot[num][MOT_L], 0);
        digitalWrite(mot[num][MOT_R], 1);  
      }
      else if (spd<0) {
        digitalWrite(mot[num][MOT_L], 1);
        digitalWrite(mot[num][MOT_R], 0);    
      }
      else {
        digitalWrite(mot[num][MOT_L], 0); //spd = 0 freewheeling
        digitalWrite(mot[num][MOT_R], 0); //spd = 0 freewheeling   
      }

     
      last_motor_command = millis();
      
      Serial.print("MOT: ");
      Serial.print(num);
      Serial.print(" | ");
      Serial.println(spd);
      
}
uint8_t current_speed = 255;

void setDriveSpeed(int spd)
{
  current_speed = (uint8_t)abs(spd);  
  Serial.print("SPD: ");
  Serial.println(current_speed);
}
void driveStop()
{
    motor_ctrl(0, 0);
    motor_ctrl(1, 0);
    
}
void driveForward()
{
    motor_ctrl(0, current_speed);
    motor_ctrl(1, current_speed);
}

void driveBackward()
{
    motor_ctrl(0, -current_speed);
    motor_ctrl(1, -current_speed);
}
void driveLeft()
{
    motor_ctrl(0, -current_speed);
    motor_ctrl(1, current_speed);
}
void driveRight() 
{
  
    motor_ctrl(0, current_speed);
    motor_ctrl(1, -current_speed);  
    
}

// respond to commands from master
// master use Wire.beginTransmission(ADDR_I2C_VORTEX)
// Wire.write(I2C_COMMAND);
// Wire.endTransmission();
void receiveEvent(int howMany)
{
/**
  uint8_t i2c_command = CMD_NONE;

  if (Wire.available()) {
    i2c_command = Wire.read();
  }


  if (i2c_command == CMD_PWR_FULL_ON) {
    if (power_enabled == false) {
      powerOn();
    }
  }
  else if (i2c_command == CMD_PWR_FULL_OFF) {
    if (power_enabled == true) {
      powerOff();
    }
  }
  else if (i2c_command == CMD_PWR_AMP_ON) {
    if (power_amp_enabled == false) {
      if (power_enabled == true) {
        powerOnAmp();
      }
      else {
        powerOn();
      }
    }
  }
  else if (i2c_command == CMD_PWR_AMP_OFF) {
    if (power_amp_enabled == true) {
      powerOffAmp();
    }
  }
 */
}


//return temperature and vcc and power state data
//master device should use Wire.requestFrom(ADDR_I2C_VORTEX, 10);
void requestEvent()
{

/**
  uint8_t data[10];
  data[0] = power_enabled;
  data[1] = power_amp_enabled;

  //temp
  uint8_t buf[4];
  memset(&buf, 0, 4);
  AMPSHARED.writeFloat(current_temp, buf);
  memcpy(&data[2], &buf, 4);

  //vcc
  memset(&buf, 0, 4);
  AMPSHARED.writeFloat(current_vcc, buf);
  memcpy(&data[6], &buf, 4);

  Wire.write(data, 10);
*/
}


void demo() 
{
  analogWrite(M1_EN, 0);
  digitalWrite(M1_L, 0);
  digitalWrite(M1_R, 0);

  analogWrite(M2_EN, 0);
  digitalWrite(M2_L, 0);
  digitalWrite(M2_R, 0);

  
  delay(1000);

  analogWrite(M1_EN, 255);
  digitalWrite(M1_L, 0);
  digitalWrite(M1_R, 1);

  delay(1000);

  analogWrite(M1_EN, 255);
  digitalWrite(M1_L, 1);
  digitalWrite(M1_R, 0);

  delay(1000);

  analogWrite(M1_EN, 0);
  digitalWrite(M1_L, 0);
  digitalWrite(M1_R, 0);

  analogWrite(M2_EN, 255);
  digitalWrite(M2_L, 0);
  digitalWrite(M2_R, 1);

  delay(1000);

  analogWrite(M2_EN, 255);
  digitalWrite(M2_L, 1);
  digitalWrite(M2_R, 0);

  delay(1000);
}
