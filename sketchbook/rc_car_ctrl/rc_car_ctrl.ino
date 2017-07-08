#include <SoftwareSerial.h>
#include <Wire.h>

SoftwareSerial dbg(A2,A1); // RX-green, TX-white

//static uint8_t nunchuck_buf[6];   // array to store nunchuck data,
unsigned int start_setup = 0;
const int SPD_MAX = 7;
const int SPD_MIN = 3;
const int SPD_TBO = 9;

int max_speed = SPD_MAX;
int min_speed = SPD_MIN;

void setup()
{
  //bluetooth module
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  dbg.begin(9600);
  

  Wire.begin();                      // join i2c bus as master
  //Wire.setClock(100000L);
  
  //digitalWrite(SDA, 1);
 // digitalWrite(SCL, 1);
  
  nunchuck_init(); // send the initilization handshake
  

  
  start_setup = millis();

  dbg.println(F("Finished setup"));
}
bool stop_sent = true;
int mot_spdL = 0;
int mot_spdR = 0;

void loop()
{

 
  while (Serial.available()) {
    dbg.write(Serial.read());
  }
  while (dbg.available()) {
    Serial.write(dbg.read());
  }

  uint8_t nunchuck_buf[6];
  
  if (nunchuck_get_data(&nunchuck_buf[0])) {

//    if (millis() - start_setup < 6000) {
//      delay(1000);  
//      return;
//    }
    //int x_axis = map(nunchuck_buf[0], 23, 222, 180, 0);
   // int y_axis = map(nunchuck_buf[1], 32, 231, 0, 180);
    //int x_axis = nunchuck_buf[0];
    //int x_axis = nunchuck_buf[0];

    int z_button = 0;
    int c_button = 0;

    if ((nunchuck_buf[5] >> 0) & 1) z_button = 1;
    if ((nunchuck_buf[5] >> 1) & 1) c_button = 1;

    //button z is pressed
    if (z_button == 0) {
      max_speed = SPD_TBO;  
    }    
    else {
      max_speed = SPD_MAX;
    }
    
    int y_axis = 0;
    int x_axis = 0;

    
    if (nunchuck_buf[1]>144) {
      y_axis = map(nunchuck_buf[1], 144,  218, 1, 5);
    }
    else if (nunchuck_buf[1]<117) {
      y_axis = map(nunchuck_buf[1], 43, 117, -5, 0);
    }

    if (nunchuck_buf[0]>175) {
      x_axis = map(nunchuck_buf[0], 175,  230, 1, 5);
    }
    else if (nunchuck_buf[0]<95) {
      x_axis = map(nunchuck_buf[0], 37, 95, -5, 0);
    }
    
    //dbg.print("X=");
    //dbg.print(x_axis);
    //dbg.print(" Y=");
    //dbg.println(y_axis);
    if (x_axis == 0 && y_axis == 0) {
      if (!stop_sent) {
        moveMotor(0,0);
        
        mot_spdL = 0;
        mot_spdR = 0;

        stop_sent = true;
      }  
    }
    int spd_y = map(abs(y_axis), 0,5, min_speed, max_speed);
    int spd_x = map(abs(x_axis), 0,5, min_speed, max_speed);
    
    if (y_axis != 0 && x_axis == 0) {    
      
      mot_spdL = spd_y;
      if (y_axis<0) mot_spdL*=-1;
      mot_spdR = mot_spdL;
      
    }

    else if (x_axis!=0) {
      
        //turning
        if (x_axis>0) {

          //foreward right
          if (y_axis>0) {
            mot_spdL = spd_x;
            mot_spdR = 0;
          }
          //backward right
          else if (y_axis<0) {
            mot_spdL = -spd_x;
            mot_spdR = 0;  
          }
          //rotate on center
          else {
             mot_spdL = spd_x;
             mot_spdR = -spd_x;  
          }
        }
        else if (x_axis<0) {
          //backward left
          if (y_axis<0) {
            mot_spdL=0;
            mot_spdR=-spd_x;
          }
          //forward left
          else if (y_axis>0) {
            mot_spdL=0;
            mot_spdR=spd_x;
          }
          else {
             mot_spdL = -spd_x; 
             mot_spdR = spd_x;
          }
        } 
    }

    if (mot_spdL!=0 || mot_spdR!=0) {
      moveMotor(mot_spdL,mot_spdR);
      stop_sent = false;
    }

   

    if (c_button == 0) {
      Serial.print(F("l9999"));
    }
    
  }

  
}




void moveMotor(int mot_spdL, int mot_spdR)
{
    //count_commands++;
    //if (count_commands == 1) return;
    if (mot_spdL>9)mot_spdL=9;
    if (mot_spdL<-9)mot_spdL=-9;
    if (mot_spdR>9)mot_spdR=9;
    if (mot_spdR<-9)mot_spdR=-9;
    Serial.print(F("m"));
    Serial.print(abs(mot_spdL));
    if (mot_spdL<0) {
      Serial.print(F("1")); 
    }
    else {
      Serial.print(F("0"));  
    }
    Serial.print(abs(mot_spdR));
    if (mot_spdR<0) {
      Serial.print(F("1")); 
    }
    else {
      Serial.print(F("0"));  
    }
    
    
}

//
// Nunchuck functions
//
// initialize the I2C system, join the I2C bus,
// and tell the nunchuck we're talking to it
void nunchuck_init()
{ 
  
  Wire.beginTransmission(0x52);     // transmit to device 0x52
  Wire.write(0x40);            // sends memory address
  Wire.write(0x00);            // sends sent a zero.  
  Wire.endTransmission();     // stop transmitting
}

// Send a request for data to the nunchuck
// was "send_zero()"
void nunchuck_send_request()
{
  Wire.beginTransmission(0x52);     // transmit to device 0x52
  Wire.write(0x00);            // sends one byte
  Wire.endTransmission();     // stop transmitting
}

// Receive data back from the nunchuck, 
int nunchuck_get_data(uint8_t *nunchuck_buf)
{
    int cnt=0;
    Wire.requestFrom (0x52, 6);     // request data from nunchuck
    while (Wire.available()) {
      // receive byte as an integer
      nunchuck_buf[cnt] = nunchuk_decode_byte(Wire.read());
      cnt++;
    }
    nunchuck_send_request();  // send request for next data payload
    // If we recieved the 6 bytes, then go print them
    if (cnt >= 5) {
      delay(20);
      return 1;   // success
     
    }
    return 0; //failure
    
}

// Print the input data we have recieved
// accel data is 10 bits long
// so we read 8 bits, then we have to add
// on the last 2 bits.  That is why I
// multiply them by 2 * 2
void nunchuck_print_data(uint8_t *nunchuck_buf)
{ 
  static int i=0;
  int joy_x_axis = nunchuck_buf[0];
  int joy_y_axis = nunchuck_buf[1];

  int accel_x_axis = nunchuck_buf[2]; // * 2 * 2; 
  int accel_y_axis = nunchuck_buf[3]; // * 2 * 2;
  int accel_z_axis = nunchuck_buf[4]; // * 2 * 2;


  int z_button = 0;
  int c_button = 0;

  // byte nunchuck_buf[5] contains bits for z and c buttons
  // it also contains the least significant bits for the accelerometer data
  // so we have to check each bit of byte outbuf[5]
  if ((nunchuck_buf[5] >> 0) & 1) 
    z_button = 1;
  if ((nunchuck_buf[5] >> 1) & 1)
    c_button = 1;

  if ((nunchuck_buf[5] >> 2) & 1) 
    accel_x_axis += 2;
  if ((nunchuck_buf[5] >> 3) & 1)
    accel_x_axis += 1;

  if ((nunchuck_buf[5] >> 4) & 1)
    accel_y_axis += 2;
  if ((nunchuck_buf[5] >> 5) & 1)
    accel_y_axis += 1;

  if ((nunchuck_buf[5] >> 6) & 1)
    accel_z_axis += 2;
  if ((nunchuck_buf[5] >> 7) & 1)
    accel_z_axis += 1;

  Serial.print(i,DEC);
  Serial.print("\t");
  
  Serial.print("joy:");
  Serial.print(joy_x_axis,DEC);
  Serial.print(",");
  Serial.print(joy_y_axis, DEC);
  Serial.print("  \t");

  Serial.print("acc:");
  Serial.print(accel_x_axis, DEC);
  Serial.print(",");
  Serial.print(accel_y_axis, DEC);
  Serial.print(",");
  Serial.print(accel_z_axis, DEC);
  Serial.print("\t");

  Serial.print("but:");
  Serial.print(z_button, DEC);
  Serial.print(",");
  Serial.print(c_button, DEC);

  Serial.print("\r\n");  // newline
  i++;
}

// Encode data to format that most wiimote drivers except
// only needed if you use one of the regular wiimote drivers
char nunchuk_decode_byte (char x)
{
  x = (x ^ 0x17) + 0x17;
  return x;
}
