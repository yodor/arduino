#include "DisplayDevice.h"
#include <Arduino.h>
#include <Wire.h>

#define OLED_Address 0x3c
#define OLED_Command_Mode 0x80
#define OLED_Data_Mode 0x40
const int row_offsets[] = { 0x00, 0x40 };

void sendCommand(uint8_t command)
{
  Wire.beginTransmission(OLED_Address); 	 // **** Start I2C
  Wire.write((uint8_t)OLED_Command_Mode);     		 // **** Set OLED Command mode
  Wire.write(command);
  Wire.endTransmission();                 	 // **** End I2C
}

void sendData(uint8_t data)
{
  Wire.beginTransmission(OLED_Address);  	// **** Start I2C
  Wire.write((uint8_t)OLED_Data_Mode);     		// **** Set OLED Data mode
  Wire.write(data);
  Wire.endTransmission();                     // **** End I2C
}
//


DisplayDevice::DisplayDevice()
{
    line0 = (char*)malloc(17);
    line1 = (char*)malloc(17);

    memset(line0, 0, 17);
    memset(line1, 0, 17);
}

DisplayDevice::~DisplayDevice()
{
    free(line0);
    free(line1);

}

void DisplayDevice::init()
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
  sendCommand(0xFF);

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


  sendCommand(0x0C);  	// **** Turn on On
  

}


char* DisplayDevice::getLine0()
{
  return line0;
}

char* DisplayDevice::getLine1()
{
  return line1;
}



void DisplayDevice::setContrast(uint8_t contrast) // contrast as 0x00 to 0xFF
{
  // **** Set OLED Characterization *** //
  sendCommand(0x2A);
  sendCommand(0x79);
  //
  sendCommand(0x81);  	// Set Contrast
  sendCommand(contrast);	// contrast value

  // **** Exiting Set OLED Characterization
  sendCommand(0x78);
  sendCommand(0x28);
  sendCommand(0x2A);

  //sendCommand(0x05);   // **** Set Entry Mode
  sendCommand(0x06);   // **** Set Entry Mode
  sendCommand(0x08);
  sendCommand(0x28);   // **** Set "IS"=0 , "RE" =0 //28
  sendCommand(0x01);
  sendCommand(0x80);   // **** Set DDRAM Address to 0x80 (line 1 start)
  delay(100);


}

void DisplayDevice::clear()
{

  sendCommand(0x01);	// **** Clear display

}

void DisplayDevice::setCursor(uint8_t col, uint8_t row)
{
  sendCommand(0x80 | (col + row_offsets[row]));
}

void DisplayDevice::printData(const char* data)
{
  uint8_t i = 0;
  while (data[i])
  {
    sendData(data[i]);      // *** Show String to OLED
    i++;
    if (i > 15) break;
  }
}

void DisplayDevice::print()
{
    this->setCursor(0,0);
    this->printData(line0);
    this->setCursor(0,1);
    this->printData(line1);
}

void DisplayDevice::createChar(uint8_t location, uint8_t charmap[])
{
  location &= 0x7;            // we only have 8 locations 0-7

  sendCommand(0x40 | (location << 3));
  delayMicroseconds(30);

  for (int i = 0; i < 8; i++)
  {
    sendData(charmap[i]);      // call the virtual write method
    delayMicroseconds(40);
  }
}
