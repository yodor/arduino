

#include <Time.h>  

#include <DS3231RTC.h>  // a basic DS1307 library that returns time as a time_t
#include <EEPROM.h>
#include <Wire.h>  
#include "EEPROMAnything.h"


#include "AlarmProcessor.h"
#include "CommandProcessor.h"
#include "SetTimeProcessor.h"
#include "SetAlarmProcessor.h"





void setup()
{
  Serial.begin(9600);
  Wire.begin();
  
  setSyncProvider(RTC.get); 
  
  if(timeStatus()!= timeSet) {
     //Serial.println("Unable to sync with the RTC");
  }
  else {
     //Serial.println("RTC has set the system time");
  }
  //Serial.println(sizeof(Alarm));
  
  
  
  
  
  
  printOK("Expecting commands");
}

void printOK(const char* str)
{
  Serial.print("OK|");
  Serial.println(str);
}
void printError(const char* str)
{
  Serial.print("ERR|");
  Serial.println(str);
}

String command;
char line_buf[80];
boolean have_line = false;

void serialEvent() {

  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
   
    if (inChar == '\r' || inChar == '\n') {
      have_line = true;
      return;
    }
    command+=inChar;
  }
}

long previousMillis = 0;        // will store last time LED was updated

// the follow variables is a long because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long interval = 1000;           // interval at which to blink (milliseconds)

void loop()
{
  
   unsigned long currentMillis = millis();
 
   if(currentMillis - previousMillis > interval) {
       previousMillis = currentMillis;   
       ALARMS.processISR();
   }
   

  CommandProcessor *proc = 0;
  
  if (have_line) {
   
    
    if (command.startsWith("AT+STATUS")) {
      sprintf(line_buf, "%s", "Running");
      printOK(line_buf);
    }
    else if (command.startsWith("AT+LIST_ALARMS")) {
      //list alarms   
      ALARMS.listAlarms();
      printOK("List Finished");
    }
    else if (command.startsWith("AT+SET_ALARM")){
      //AT+SET_ALARM|1|E|12:30|10|9 - set alarm 1 to enabled , each day 12:30 for 10 seconds, pin 9
      //AT+SET_ALARM|1|D - set alarm 1 to disabled
      proc = new SetAlarmProcessor();

    }
    else if (command.startsWith("AT+LIST_PINS")) {
      ALARMS.listValidPins();
    }
    else if (command.startsWith("AT+GET_TIME")) {
      sprintf(line_buf, "%d:%d:%d|%ld", hour(), minute(), second(), millis());
      printOK(line_buf);
    }
    else if (command.startsWith("AT+SET_TIME")) {
      proc = new SetTimeProcessor();
    }
    
    else {
      
      printError("Unrecognized Command");
      
    }
    if (proc) {
      proc->processLine(command);
      if (proc->have_error) {
        printError(proc->result);
      }
      else {
        printOK(proc->result);
      }
      delete proc;
      proc=0;
    }
    have_line = false;
    command = "";
  }
  
  
}

