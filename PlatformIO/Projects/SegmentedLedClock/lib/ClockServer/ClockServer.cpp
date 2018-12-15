#include "ClockServer.h"
#include "RTClib.h"
#include <FS.h>

RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};



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

    current_time = rtc.now();
    current_temp = rtc.getTemp();

    m_server.on("/getTime" , std::bind(&ClockServer::handleGetTime, this));
    m_server.on("/setTime" , std::bind(&ClockServer::handleSetTime, this));
    m_server.on("/getDate" , std::bind(&ClockServer::handleGetDate, this));
    m_server.on("/setDate" , std::bind(&ClockServer::handleSetDate, this));
    m_server.on("/getTemperature" , std::bind(&ClockServer::handleGetTemperature, this));

    m_server.on("/wificonfig", std::bind(&ClockServer::handleWifiConfig,this));

    m_server.onNotFound(std::bind(&ClockServer::handleNotFound, this));

    m_server.begin();



}


void ClockServer::loop()
{
  m_server.handleClient();
}

void ClockServer::update()
{
  current_time = rtc.now();
  current_temp = rtc.getTemp();
}


DateTime ClockServer::now()
{
    return current_time; //rtc.now();
}
double ClockServer::temperature()
{
    return current_temp; //rtc.getTemp();
}

void ClockServer::sendResult(const String& response, bool restart)
{

    m_server.send(200, "text/plain", response.c_str());

    if (restart) {
        //SPIFFS.end();

        ESP.restart();
        while(true) {
          delay(1000);
        }
    }
}

void ClockServer::handleWifiConfig()
{


  if (m_server.args()==2) {

    String ssid=m_server.arg("ssid");
    String pass=m_server.arg("pass");
    ssid.trim();
    pass.trim();

    if (ssid.length()<1 || pass.length()<1) {
        sendResult("ssid and pass parameters are empty");
        return;
    }
    SPIFFS.remove("/wificonfig.txt");
    File file = SPIFFS.open("/wificonfig.txt", "w");
    if (!file) {
      sendResult("Unable to write to wificonfig.txt");
      return;
    }

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
          SPIFFS.remove("/wificonfig.txt");

          sendResult("Wifi configuration erased. Restarting ...", true);
          return;
      }

  }
  else {
      //show current config
      File file = SPIFFS.open("/wificonfig.txt", "r");
      if (!file) {
          sendResult("Unable to open wificonfig.txt for reading");
          return;
      }
      String ssid = file.readStringUntil('\n');
      String pass = file.readStringUntil('\n');
      String result = String("Current wifi client settings:\n");
      result+= String("SSID: " + ssid);
      result+= String("\nPassword: " + pass);
      sendResult(result);
      file.close();
  }
}


void ClockServer::handleGetTime()
{
    String result = String("Current Time: ") + current_time.hour() + ":" + current_time.minute() +":" + current_time.second();
    sendResult(result);
}

void ClockServer::handleGetTemperature()
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
