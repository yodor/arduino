#include <stdio.h>
void setup()
{

    Serial.begin(9600);
    Serial.println("Setup");
    
}
void loop()
{
  long v = 4202;
  unsigned long vcc = v;
  unsigned long temp = 258000;

  char buf[16];
  sprintf(buf, "[S] V:%ld T:%ld",  (unsigned long)(vcc/100), (unsigned long)(temp/1000));
  Serial.println(buf);

  
  delay(1000);
  
}
