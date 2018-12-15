#ifndef CLOCKSERVER_H
#define CLOCKSERVER_H

#include <ESP8266WebServer.h>
#include "RTClib.h"

class ClockServer {

public:
    ClockServer(ESP8266WebServer& server);
    ~ClockServer();

    void begin();
    void loop();
    DateTime now();
    void print(const DateTime& dateTime);
    double temperature();
    void update();



protected:



    ESP8266WebServer& m_server;
    double current_temp;
    DateTime current_time;

    void handleGetTime();
    void handleSetTime();
    void handleGetDate();
    void handleSetDate();

    void handleGetTemperature();
    void handleWifiConfig();

    void handleNotFound();

    void sendResult(const String& response, bool restart=false);

};

#endif //CLOCKSERVER_H
