void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:
      tmElements_t tm;
      RTC.read(tm);
      
//      tm.Hour = adj_hour;
//      tm.Minute = adj_minute;
//      tm.Second = (uint8_t)0;
//
//      time_t current_time = makeTime(tm);
//      setTime(adj_hour, adj_minute, 0, day(), month(), year());
//      
//      RTC.set(current_time);
}
