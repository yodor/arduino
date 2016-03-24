#ifndef ALARM_PROCESSOR_H
#define ALARM_PROCESSOR_H


#define ADDR_CONFIG_VALID  10
#define ADDR_ALARM_ARRAY   20
#define chk_config         1234

typedef struct Alarm_T {
  bool enabled;
  int Hour;
  int Minute; 
  
  //milliseconds after timeout
  unsigned long Timeout;
  //start millis of the alarm
  unsigned long Started;
  //alarm is stopping (dont start in same interval minute)
  bool Stopping;
  //
  int Pin;
  
} Alarm;


class AlarmProcessor {
  
public:
  AlarmProcessor();
  ~AlarmProcessor();
  
  void processLoop();
  void processISR();
  void enableAlarm(int num, int Hour, int Minute, int Timeout, int Pin);
  void disableAlarm(int num);
  void listAlarms();
  void listValidPins();
  bool isValidPin(int alarm_pin);
  int haveAlarm(int Hour, int Minute);
  
  Alarm alarms[10];
  
};
extern AlarmProcessor ALARMS;
#endif
