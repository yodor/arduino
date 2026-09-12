#include "ClockServer.h"
#include "RTClib.h"


RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

extern const char* wifiConfigFile;

ClockServer::ClockServer(ESP8266WebServer& server) : m_server(server), current_temp(0.0)
{

}

ClockServer::~ClockServer()
{

}

void ClockServer::begin()
{
    if (! rtc.begin()) {
      Serial.println("Couldn't find RTC");
      while (1);
    }

    if (rtc.lostPower()) {
      Serial.println("RTC lost power, lets set the time!");
      // following line sets the RTC to the date & time this sketch was compiled
      current_time = DateTime(F(__DATE__), F(__TIME__));
      rtc.adjust(current_time);

      // This line sets the RTC with an explicit date & time, for example to set
      // January 21, 2014 at 3am you would call:
      // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));

    }

    rtc.writeSqwPinMode(DS3231_SquareWave1Hz);

    update();

    m_server.on("/", std::bind(&ClockServer::handleIndex,this));
    m_server.on("/getTime" , std::bind(&ClockServer::handleGetTime, this));
    m_server.on("/setTime" , std::bind(&ClockServer::handleSetTime, this));
    m_server.on("/getDate" , std::bind(&ClockServer::handleGetDate, this));
    m_server.on("/setDate" , std::bind(&ClockServer::handleSetDate, this));
    m_server.on("/getTemp" , std::bind(&ClockServer::handleGetTemp, this));

    m_server.on("/wificonfig", std::bind(&ClockServer::handleWifiConfig,this));

    m_server.onNotFound(std::bind(&ClockServer::handleNotFound, this));

    m_server.begin();



}


void ClockServer::loop(bool update_flag)
{
  m_server.handleClient();
  if (update_flag)update();
}

void ClockServer::update()
{
  current_time = rtc.now();
  current_temp = rtc.getTemperature() - 4.5;
}


DateTime ClockServer::now()
{
    return current_time;
}
double ClockServer::temperature()
{
    return current_temp;
}

void ClockServer::sendResult(const String& response, bool restart)
{

    m_server.send(200, "text/plain", response.c_str());

    if (restart) {
        LittleFS.end();

        ESP.restart();
        while(true) {
          delay(1000);
        }
    }
}

void ClockServer::handleWifiConfig()
{


  if (m_server.args()==3) {

    String ssid=m_server.arg("ssid");
    String pass=m_server.arg("pass");
    String mode=m_server.arg("mode");

    mode.trim();
    ssid.trim();
    pass.trim();

    if (ssid.length()<1 || pass.length()<1 || mode.length()<1) {
        sendResult("requires non empty parameters mode, ssid and pass");
        return;
    }
    //LittleFS.remove(wifiConfigFile);
    File file = LittleFS.open(wifiConfigFile, "w");
    if (!file) {
      sendResult("Unable to write to wificonfig");
      return;
    }

    if (mode.equalsIgnoreCase("CLI")) {
      file.print("CLI");
    }
    else {
      file.print("STA");
    }
    file.print('\n');
    file.print(ssid);
    file.print('\n');
    file.print(pass);
    file.print('\n');
    file.close();

    sendResult("Wifi configuration updated. Restarting ... ", true);

  } //args==2
  else if (m_server.args()==1) {
      String argName = m_server.argName(0);

      if (argName.equalsIgnoreCase("reset")) {
          LittleFS.remove(wifiConfigFile);

          sendResult("Wifi configuration erased. Restarting ...", true);
          return;
      }

  }
  else {
      //show current config
      File file = LittleFS.open(wifiConfigFile, "r");
      if (!file) {
          sendResult("Unable to open wificonfig for reading");
          return;
      }
      String mode = file.readStringUntil('\n');
      String ssid = file.readStringUntil('\n');
      String pass = file.readStringUntil('\n');
      String result = String("Current wifi client settings:\n");
      result+= String("\nMode: " + mode);
      result+= String("\nSSID: " + ssid);
      result+= String("\nPassword: " + pass);
      sendResult(result);
      file.close();
  }
}

void ClockServer::handleIndex()
{
    File indexFile = LittleFS.open("/index.html", "r");
    if (!indexFile) {
      sendResult("Unable to read the index file");
      return;
    }
    
    m_server.send(200, "text/html", indexFile.readString().c_str());
}

void ClockServer::handleGetTime()
{
    String result = String("Current Time: ") + current_time.hour() + ":" + current_time.minute() +":" + current_time.second();
    sendResult(result);
}

void ClockServer::handleGetTemp()
{
    String result = String("Current Temperature: ") + current_temp;
    sendResult(result);
}

void ClockServer::handleSetTime()
{
    String hour=m_server.arg("hour");
    String minute=m_server.arg("minute");
    String second=m_server.arg("second");

    DateTime adjust = DateTime(current_time.year(), current_time.month(), current_time.day(),
                  hour.toInt(), minute.toInt(), second.toInt());

    rtc.adjust(adjust);
    this->update();

    sendResult("Time set successfully.");

    Serial.println(F("RTC adjusted: "));
    this->print(rtc.now());

}

void ClockServer::handleGetDate()
{
    String result = String("Current Date: ") + current_time.year() + "/" + current_time.month() + "/" + current_time.day();
    sendResult(result);

}

void ClockServer::handleSetDate()
{
    String year=m_server.arg("year");
    String month=m_server.arg("month");
    String day=m_server.arg("day");

    DateTime adjust = DateTime(year.toInt(), month.toInt(), day.toInt(),
                  current_time.hour(), current_time.minute(), current_time.second());
    rtc.adjust(adjust);
    this->update();

    sendResult("Date set successfully.");

    Serial.println(F("RTC adjusted: "));
    this->print(rtc.now());
}

void ClockServer::handleNotFound()
{


  String message = "File Not Found\n\n";
  message += "URI: ";
  message += m_server.uri();
  message += "\nMethod: ";
  message += (m_server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += m_server.args();
  message += "\n";
  for (uint8_t i=0; i<m_server.args(); i++){
    message += " " + m_server.argName(i) + ": " + m_server.arg(i) + "\n";
  }
  m_server.send(404, "text/plain", message);

}
void ClockServer::print(const DateTime& dateTime)
{
    Serial.print(dateTime.year(), DEC);
    Serial.print('/');
    Serial.print(dateTime.month(), DEC);
    Serial.print('/');
    Serial.print(dateTime.day(), DEC);
    Serial.print(" (");
    Serial.print(daysOfTheWeek[dateTime.dayOfTheWeek()]);
    Serial.print(") ");
    Serial.print(dateTime.hour(), DEC);
    Serial.print(':');
    Serial.print(dateTime.minute(), DEC);
    Serial.print(':');
    Serial.print(dateTime.second(), DEC);
    Serial.println();
}
