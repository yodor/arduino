#include <Wire.h>          // *** I2C Mode 
#define OLED_Address 0x3c
#define OLED_Command_Mode 0x80
#define OLED_Data_Mode 0x40

int band_levels[] = {0,127,255,383,511,639,767,895};

void setup()
{
  randomSeed(analogRead(0));
  Wire.begin();
  initDisplay();
}

void initDisplay()
{
 // *** I2C initial *** //
 delay(100);
 sendCommand(0x2A);	// **** Set "RE"=1	00101010B
 sendCommand(0x71);
 sendCommand(0x5C);
 sendCommand(0x28);

 sendCommand(0x08);	// **** Set Sleep Mode On
 sendCommand(0x2A);	// **** Set "RE"=1	00101010B
 sendCommand(0x79);	// **** Set "SD"=1	01111001B

 sendCommand(0xD5);
 sendCommand(0x70);
 sendCommand(0x78);	// **** Set "SD"=0

 sendCommand(0x08);	// **** Set 5-dot, 3 or 4 line(0x09), 1 or 2 line(0x08)

 sendCommand(0x06);	// **** Set Com31-->Com0  Seg0-->Seg99

 // **** Set OLED Characterization *** //
 sendCommand(0x2A);  	// **** Set "RE"=1 
 sendCommand(0x79);  	// **** Set "SD"=1

 // **** CGROM/CGRAM Management *** //
 sendCommand(0x72);  	// **** Set ROM
 sendCommand(0x00);  	// **** Set ROM A and 8 CGRAM


 sendCommand(0xDA); 	// **** Set Seg Pins HW Config
 sendCommand(0x10);   

 sendCommand(0x81);  	// **** Set Contrast
 sendCommand(0x55); 

 sendCommand(0xDB);  	// **** Set VCOM deselect level
 sendCommand(0x30);  	// **** VCC x 0.83

 sendCommand(0xDC);  	// **** Set gpio - turn EN for 15V generator on.
 sendCommand(0x03);

 sendCommand(0x78);  	// **** Exiting Set OLED Characterization
 sendCommand(0x28); 
 sendCommand(0x2A); 
 //sendCommand(0x05); 	// **** Set Entry Mode
 sendCommand(0x06); 	// **** Set Entry Mode
 sendCommand(0x08);  
 sendCommand(0x28); 	// **** Set "IS"=0 , "RE" =0 //28
 sendCommand(0x01); 
 sendCommand(0x80); 	// **** Set DDRAM Address to 0x80 (line 1 start)

 delay(100);
 
 
 
 sendCommand(0x0C);  	// **** Turn on Display
 
 
 
}

char buf[16];
int c=0;
void loop()
{
 // ********************************************************************//
 // **** Show Data Value *** //
 
 
 
 sendCommand(0x01);	// **** Clear display
 
 //sendCommand(0x0F);  	// **** cursor + blink

  
 
 send_string("0123456789ABCDEF");
 sendCommand(0xC0);  	// **** New Line
 send_string("---WIDE.HK---");
 
 delay(2000);
 
 
 
 
 sendCommand(0x01);	// **** Clear display
 
 
 uint8_t levels[8];
 
 while (1) {
 //prepare levels
  for( int band = 0; band < 7; band++ )
  {
     long fft_data = analogRead(A0) + random(1023);  
     int level = 0; 
     for (int level_index=0;level_index<7;level_index++) {
         if (fft_data>=band_levels[level_index]) {
           level = level_index;
         }
     }
     levels[band] = level;
  }
  renderFFT(levels);
  
   c++;
   snprintf(buf, 15, "%d", c);
   setCursor(0,1);
   send_string(buf);

  //delay(10);
  
 }
        
// while (1)
// {
//   c++;
//   snprintf(buf, 15, "%d", c);
//   setCursor(0,0);
//   send_string(buf);
// }

 // **** Show Data Value *** //
 // ********************************************************************//

}

const int row_offsets[] = { 0x00, 0x40 };
void setCursor(uint8_t col, uint8_t row)
{
   
   sendCommand(0x80 | (col + row_offsets[row]));
}

void sendData(unsigned char data)
{
    Wire.beginTransmission(OLED_Address);  	// **** Start I2C 
    Wire.write((uint8_t)OLED_Data_Mode);     		// **** Set OLED Data mode
    Wire.write((uint8_t)data);
    Wire.endTransmission(true);                     // **** End I2C 
}

void sendCommand(unsigned char command)
{
    Wire.beginTransmission(OLED_Address); 	 // **** Start I2C 
    Wire.write((uint8_t)OLED_Command_Mode);     		 // **** Set OLED Command mode
    Wire.write((uint8_t)command);
    Wire.endTransmission(true);                 	 // **** End I2C 
    //delay(10);
}

void send_string(const char *String)
{
    unsigned char i=0;
    while(String[i])
    {
        sendData(String[i]);      // *** Show String to OLED
        i++;
    }
}
void createChar(uint8_t location, uint8_t charmap[]) 
{
   location &= 0x7;            // we only have 8 locations 0-7
   
   sendCommand(0x40 | (location << 3));
   delayMicroseconds(30);//30
   
   for (int i=0; i<8; i++) 
   {
      sendData(charmap[i]);      // call the virtual write method
      delayMicroseconds(40);//40
   }
}
void loadChars()
{

  //custom display character holder
  byte bar[8];

  for (int a=7;a>=0;a--) {

    bar[a] = 0b00000;
  }

  for (int a=7;a>=0;a--) {
    bar[a] = 0b11111;
    createChar((7-a), bar);

  }

}
void renderFFT(uint8_t* levels)
{
  

  setCursor(0,0);
  for (int a=0;a<7;a++) {
    byte val = levels[a];
    sendData(val);
  }
  //sendData(0x20);
    //sendData(0x20);
      //sendData(0x20);
        //sendData(0x20);

}
