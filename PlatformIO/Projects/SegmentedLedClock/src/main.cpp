#include <FS.h>
#include <Wire.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SegmentDisplay.h>
#include <ArduinoOTA.h>

#include <ClockServer.h>

double version = 1.0;







ESP8266WebServer server(80);

ClockServer handler(server);

SegmentDisplay disp;

#define LED D0
#define INTERVAL_LED_UPDATE 1000
unsigned long led_update = 0;
bool led_state = true;

volatile boolean tick_flag = false;



void tickISR()
{
    tick_flag=true;
}

void wifiSoftAP()
{
    IPAddress local_IP(10,10,10,1);
    IPAddress gateway(10,10,10,1);
    IPAddress subnet(255,255,255,0);

    Serial.print("Configuring Soft-AP: ");
    Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");

    Serial.print("Initializing Soft-AP: ");
    Serial.println(WiFi.softAP("ESP-Clock") ? "Ready" : "Failed!");

    Serial.print("Soft-AP IP: ");
    Serial.println(WiFi.softAPIP());

}

void wifiSTA(const String& ssid, const String& pass)
{
    Serial.println("Initializing wifi client");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("PASS: ");
    Serial.println(pass);


    disp.startProgress();

    WiFi.mode(WIFI_STA);

    WiFi.begin(ssid.c_str(), pass.c_str());

    digitalWrite(LED, LOW); // turn the LED on.

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {

      Serial.print(".");
      delay(100);
      disp.stepProgress();

    }

    disp.endProgress();

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}
void setup ()
{

  #ifndef ESP8266
    while (!Serial); // for Leonardo/Micro/Zero
  #endif

  Serial.begin(115200);

  delay(3000); // wait for console opening
  //Initialize file system.


  Wire.begin(4,5);


  pinMode(LED, OUTPUT);

  pinMode(0, INPUT_PULLUP);
  attachInterrupt(0, tickISR, RISING);

  disp.begin();



  Serial.print("ESPClock Version: ");
  Serial.println(version,DEC);

  Serial.println("Loading wifi settings ... ");

  SPIFFS.begin();

  File file = SPIFFS.open("/wificonfig.txt", "r");
  if (!file) {
      Serial.println("Unable to open wificonfig.txt");
      wifiSoftAP();
  }
  else {

      Serial.print("config size: ");
      Serial.println(file.size(),DEC);

      String ssid = file.readStringUntil('\n');
      ssid.trim();
      String pass = file.readStringUntil('\n');
      ssid.trim();
      file.close();

      wifiSTA(ssid, pass);

  }

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  ArduinoOTA.setPassword("123456");
  ArduinoOTA.onStart([](){
      Serial.println("OTA Process start ...");
      SPIFFS.end();
  });

  ArduinoOTA.onEnd([](){
      Serial.println("OTA Process end ...");
      SPIFFS.begin();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error){
      Serial.printf("Error: %u", error);
      if (error == OTA_AUTH_ERROR) Serial.println("OTA AUTH Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("OTA BEGIN Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("OTA CONNECT Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA RECEIVE Failed");
      else if (error == OTA_END_ERROR) Serial.println("OTA END Failed");
  });

  ArduinoOTA.begin();
  Serial.println("ArduinoOTA started");

  handler.begin();

  Serial.println("HTTP server started");

  Serial.println("RTC DateTime: ");
  handler.print(handler.now());




}

DateTime now;
double temperature = 0.0;

void loop ()
{

  now = handler.now();
  temperature = handler.temperature();

  disp.loop(now.hour(), now.minute(), temperature);

  ArduinoOTA.handle();
  handler.loop();

  if (tick_flag) {
      handler.update();
      tick_flag=false;
  }

}
