// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
#include <Wire.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SegmentDisplay.h>
#include <ArduinoOTA.h>

#include <ClockServer.h>

const char* ssid = "saturno";
const char* password = "cafebabe";

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

void setup ()
{

  #ifndef ESP8266
    while (!Serial); // for Leonardo/Micro/Zero
  #endif

  Serial.begin(9600);

  //delay(3000); // wait for console opening

  Wire.begin(4,5);


  pinMode(LED, OUTPUT);

  pinMode(0, INPUT_PULLUP);
  attachInterrupt(0, tickISR, RISING);

  disp.begin();

  disp.startProgress();

  Serial.print("Version: ");
  Serial.println(version,DEC);


  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //Serial.println("");

  digitalWrite(LED, LOW);          // turn the LED on.
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

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  ArduinoOTA.setPassword("123456");
  ArduinoOTA.onStart([](){
      Serial.println("OTA Process start ...");
  });

  ArduinoOTA.onEnd([](){
      Serial.println("OTA Process end ...");
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
