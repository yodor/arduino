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

    DateTime current_time;
    double current_temp;

    void handleGetTime();
    void handleSetTime();
    void handleGetDate();
    void handleSetDate();

    void handleGetTemperature();

    void handleNotFound();

    ESP8266WebServer& m_server;
};

#endif //CLOCKSERVER_H
