#include <DS3231RTC.h>
#include <Time.h>
#include <Wire.h>

tmElements_t tm;

const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

bool getTime(const char *str)
{
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec;
  return true;
}

bool getDate(const char *str)
{
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++) {
    if (strcmp(Month, monthName[monthIndex]) == 0) break;
  }
  if (monthIndex >= 12) return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}

void setCompilerDateTime()
{
  if (getDate(__DATE__) && getTime(__TIME__)) {
    
    
    // and configure the RTC with this info
    time_t time_compiler = makeTime(tm);
    RTC.set(time_compiler);
    
    
  }
}
void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Wire.begin();
  // get the date and time the compiler was run
  //setCompilerDateTime();
  
  setSyncProvider(RTC.get);   // the function to get the time from the RTC
  if(timeStatus()!= timeSet) 
     Serial.println("Unable to sync with the RTC");
  else
     Serial.println("RTC has set the system time");
}

void loop() {
  // put your main code here, to run repeatedly: 
  float temp = RTC.getTemp();

  Serial.print("Temp: ");
  Serial.println(temp);
  
  char temp_str[9];
  sprintf(temp_str, "%s", temp);
  
     
  
  if (timeStatus() == timeSet) {
    digitalClockDisplay();
  } else {
    Serial.println("The time has not been set.  Please run the Time");
    Serial.println("TimeRTCSet example, or DS1307RTC SetTime example.");
    Serial.println();
    delay(4000);
  }
  delay(1000);
}

void digitalClockDisplay(){
  char clock_str[9];
  sprintf(clock_str, "%02d:%02d:%02d", hour(), minute(), second());
  Serial.println(clock_str);
  
  
  // digital clock display of the time
//  Serial.print(hour());
//  printDigits(minute());
//  printDigits(second());
//  Serial.print(" ");
//  Serial.print(day());
//  Serial.print(" ");
//  Serial.print(month());
//  Serial.print(" ");
//  Serial.print(year()); 
//  Serial.println(); 
}

void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}


