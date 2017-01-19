


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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  
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

void loop()
{ 
    
    
    while (Serial.available() > 0)
    {
        char recieved = Serial.read();
        inData += recieved; 

        // Process message when new line character is recieved
        if (recieved == '\n' || recieved == '\r')
        {
            have_line = true;
            break;
                    
        }
    }

    if (have_line) {
      if (inData.startsWith("AT+")) {
        processCommand(inData);     
        
      }
      else {
        printError(F("AT Command only"));
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
  //AT+:
  String cmd = inData.substring(3);
  Serial.print("SubCMD: ");
  Serial.println(cmd);
  
  if (cmd.startsWith("MOVE")) {

      cmd = cmd.substring(4);
      
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
  else if (cmd.startsWith("STOP")) {
      driveStop();
  }
  else if (cmd.startsWith("AUTOSTOP_ENABLE")) {
      auto_off_timeout = 1000;
      
  }
  else if (cmd.startsWith("AUTOSTOP_DISABLE")) {
    auto_off_timeout = 0;  
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

  if (auto_off_timeout>0) {
    delay(auto_off_timeout);
    driveStop();
  }
 
  
}
//AT+MOVE:255|255
void motor_ctrl(int num, int spd)
{
    
      analogWrite(mot[num][MOT_EN], abs(spd));
      if (spd>0) {
        digitalWrite(mot[num][MOT_L], 0);
        digitalWrite(mot[num][MOT_R], 1);  
      }
      else if (spd<0) {
        digitalWrite(mot[num][MOT_L], 1);
        digitalWrite(mot[num][MOT_R], 0);    
      }
      else {
        digitalWrite(mot[num][MOT_L], 0);
        digitalWrite(mot[num][MOT_R], 0);    
      }

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
