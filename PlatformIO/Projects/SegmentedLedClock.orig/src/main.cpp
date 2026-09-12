#include <LittleFS.h>
#include <Wire.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SegmentDisplay.h>
#include <ArduinoOTA.h>


#include <ClockServer.h>

double version = 1.1;

#define NET_NAME "espclock"
#define NET_PASS "12345678"

#define OTP_PASS "123456"

const char *wifiConfigFile = "/wificonfig.txt";
const char *ntpConfigFile = "/ntpconfig.txt";

ESP8266WebServer server(80);

ClockServer handler(server);

SegmentDisplay disp;

volatile boolean tick_flag = false;

//WiFiUDP ntpUDP;
// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionally you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
//NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

//each second as configured 
IRAM_ATTR void tickISR()
{
    tick_flag=true;
}

void wifiSoftAP()
{
    IPAddress local_IP(10,10,10,1);
    IPAddress gateway(10,10,10,1);
    IPAddress subnet(255,255,255,0);

    Serial.print(F("Configuring Soft-AP: "));
    Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");

    Serial.print(F("Initializing Soft-AP: "));
    Serial.println(WiFi.softAP(NET_NAME, NET_PASS) ? "Ready" : "Failed!");

    Serial.print(F("Soft-AP IP: "));
    Serial.println(WiFi.softAPIP());

}

void wifiSTA(const String& ssid, const String& pass)
{
    Serial.println(F("Initializing wifi client"));
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("PASS: ");
    Serial.println(pass);


    disp.startProgress();

    WiFi.mode(WIFI_STA);

    //Get Current Hostname
    Serial.printf("Default hostname: %s\n", WiFi.hostname().c_str());

    //Set new hostname
    WiFi.hostname(NET_NAME);

    //Get Current Hostname
    Serial.printf("New hostname: %s\n", WiFi.hostname().c_str());
    
   
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait for connection
    int wait = 0;
    boolean connect_failed = false;

    while (WiFi.status() != WL_CONNECTED) {

      Serial.print(".");
      delay(100);
      disp.stepProgress();
      wait+=100;
      if (wait>10000) {
          connect_failed = true;
          break;
      }
    }

    disp.endProgress();

    if (connect_failed) {
        Serial.println(F("Failed to connect to wifi network"));
        wifiSoftAP();
        return;
    }

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.println(F("Starting network time client"));
    
}

void wifiConfig()
{

  

  File file = LittleFS.open(wifiConfigFile, "r");
  if (!file) {
      Serial.println("Unable to open wifi config");
      wifiSoftAP();
  }
  else {

      Serial.print("config size: ");
      Serial.println(file.size(),DEC);
      String mode = file.readStringUntil('\n');
      String ssid = file.readStringUntil('\n');
      String pass = file.readStringUntil('\n');
      file.close();

      mode.trim();
      ssid.trim();
      pass.trim();

      if (mode.equalsIgnoreCase("CLI")) {
        wifiSTA(ssid, pass);
      }
      else {
        wifiSoftAP();
      }
  }

}

void setup ()
{

  #ifndef ESP8266
    while (!Serial); // for Leonardo/Micro/Zero
  #endif

  Serial.begin(115200);

  Serial.println("Configuring LittleFS ... ");

  LittleFSConfig cfg;
  cfg.setAutoFormat(true);
  LittleFS.setConfig(cfg);

  if (LittleFS.begin()) {
    Serial.println("LittleFS mounted  ... ");
  }
  else {
    Serial.println("LittleFS not mounted - trying format ... ");
    LittleFS.end();
    if (LittleFS.format()) {
      Serial.println("LittleFS format success ... ");
    }
    else {
      Serial.println("LittleFS format failure ... ");
    }

      
  }
  //delay(3000); // wait for console opening

  //SDA,SCL
  Wire.begin(0,2);

//square wave from ds3231
  pinMode(14, INPUT_PULLUP);
  attachInterrupt(14, tickISR, RISING);

  disp.begin();


  Serial.print( String(NET_NAME) + " Version: ");
  Serial.println(version,DEC);

  Serial.println("Loading wifi settings ... ");

  wifiConfig();

  if (MDNS.begin(NET_NAME)) {
    Serial.println("mDNS responder started ...");
    MDNS.addService("Web", "tcp", 80);
  }

  

  ArduinoOTA.setHostname(NET_NAME);
  ArduinoOTA.setPassword(OTP_PASS);
  ArduinoOTA.onStart([](){
      Serial.println("OTA Process start ...");
      LittleFS.end();
  });

  ArduinoOTA.onEnd([](){
      Serial.println("OTA Process end ...");
      LittleFS.begin();
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


void loop ()
{

  MDNS.update();

  disp.loop(handler.now().hour(), handler.now().minute(), handler.temperature());

  ArduinoOTA.handle();
  
  handler.loop(tick_flag);
  
  if (tick_flag) {
      tick_flag=false;
  }

}
