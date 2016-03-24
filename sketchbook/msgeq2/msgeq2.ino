// Example 48.1 - tronixstuff.com/tutorials > chapter 48 - 30 Jan 2013 
// MSGEQ7 spectrum analyser shield - basic demonstration
int strobe = 4; // strobe pins on digital 4
int res = 5; // reset pins on digital 5
int left[7]; // store band values in these arrays

int band;
void setup()
{
 Serial.begin(115200);
 pinMode(res, OUTPUT); // reset
 pinMode(strobe, OUTPUT); // strobe
 digitalWrite(res,LOW); // reset low
 digitalWrite(strobe,HIGH); //pin 5 is RESET on the shield
}
void readMSGEQ7()
// Function to read 7 band equalizers
{
 digitalWrite(res, HIGH);
 digitalWrite(res, LOW);
 for(band=0; band <7; band++)
 {
 digitalWrite(strobe,LOW); // strobe pin on the shield - kicks the IC up to the next band 
 delayMicroseconds(30); // 
 left[band] = analogRead(0); // store left band reading

 digitalWrite(strobe,HIGH); 
 }
}
void loop()
{
 readMSGEQ7();
 // display values of left channel on serial monitor
 for (band = 0; band < 7; band++)
 {
   int val = 0;
 //Serial.print(left[band]);
 //Serial.print(" ");
 if (left[band]>=895) { val=7; } else
 if (left[band]>=767) { val=6; } else
 if (left[band]>=639) { val=5; } else
 if (left[band]>=511) { val=4; } else
 if (left[band]>=383) { val=3; } else
 if (left[band]>=255) { val=2; } else
 if (left[band]>=127) { val=1; } else
 if (left[band]>=0) { val=0; }
   Serial.print(val);
   Serial.print(" ");
 }
 Serial.println();

}
