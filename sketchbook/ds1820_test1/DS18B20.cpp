#include "DS18B20.h"
#include <OneWire.h>


OneWire ds(10);  // on pin 10

#define READ_TIME 750

byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];
  
DS18B20::DS18B20() : device_type(0x00), read_start(0)
{
  

  if ( !ds.search(addr)) {
      //Serial.print("No more addresses.\n");
      ds.reset_search();
      return;
  }

  //Serial.print("R=");
  //for( i = 0; i < 8; i++) {
  //  Serial.print(addr[i], HEX);
  //  Serial.print(" ");
  //}

  //if ( OneWire::crc8( addr, 7) != addr[7]) {
  //    Serial.print("CRC is not valid!\n");
  //    return;
  //}

  if ( addr[0] == 0x10) {
      //Serial.print("Device is a DS18S20 family device.\n");
      device_type = 0x10;
  }
  else if ( addr[0] == 0x28) {
      //Serial.print("Device is a DS18B20 family device.\n");
      device_type = 0x28;
  }
  else {
      //Serial.print("Device family is not recognized: 0x");
      //Serial.println(addr[0],HEX);
      return;
  }

}

DS18B20::~DS18B20()
{

}
bool DS18B20::haveDevice()
{
  return (device_type != 0x00);
}
uint8_t DS18B20::deviceType()
{
  return device_type;
}
void DS18B20::prepareRead()
{

  if (read_start!=0)return;
  
  ds.reset();
  ds.select(addr);
  ds.write(0x44);         // start conversion, with parasite power on at the end
  read_start = millis();
}

bool DS18B20::readTemp(float& result)
{  
  if (read_start==0) return false;
  
  if (millis() - read_start < READ_TIME) return false;
  
  //delay(750);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  

  //Serial.println("P=");
  //Serial.print(present,HEX);
  //Serial.println(" ");
  
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    //Serial.print(data[i], HEX);
    //Serial.print(" ");
  }
  //Serial.print(" CRC=");
  //Serial.print( OneWire::crc8( data, 8), HEX);
  //Serial.println();

  
  int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
  LowByte = data[0];
  HighByte = data[1];
  TReading = (HighByte << 8) + LowByte;
  SignBit = TReading & 0x8000;  // test most sig bit
  if (SignBit) // negative
  {
    TReading = (TReading ^ 0xffff) + 1; // 2's comp
  }
  Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25

  Whole = Tc_100 / 100;  // separate off the whole and fractional portions
  Fract = Tc_100 % 100;


  int sign = (SignBit) ? -1 : 1;
  
//  if (SignBit) // If its negative
 // {
   //  Serial.print("-");
  //}
  
  result = (Whole + (Fract / 100.0)) * sign;
  //Serial.print(Whole);
  //Serial.print(".");
//  if (Fract < 10)
 // {
  //   Serial.print("0");
  //}
  //Serial.print(Fract);

  //Serial.print("\n");
    //Serial.println(result);
    read_start = 0;
    return true;
  
}
