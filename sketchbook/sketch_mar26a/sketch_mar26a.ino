


#include <avr/io.h>        //This is our usual include
#include <util/delay.h>        //The delay functions/rou

void setup()
{

}
void loop()
{
  DDRC|= (1<<5); // set PC5 as output

  while(1){
    PORTC ^= (1<<5); // toggle the pin

  }
}

