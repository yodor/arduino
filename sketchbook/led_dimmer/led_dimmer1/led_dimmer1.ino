#include <IRButtons.h>
#include <IRProcessor.h>

#include "EEPROMAnything.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "SegmentDisplay.h"

#include <Adafruit_NeoPixel.h>

//to RGB
#define PIN_RGB  10
//to IR
#define PIN_IR   8
//to bc547 base
#define PIN_FADE 5
// Data wire is plugged into port 2 on the Arduino
#define PIN_TEMP A3

const uint8_t unused_pins[] = {1,2,3,4,6,7,9,11,12,13,A0,A1,A2};

#define ADDR_CONFIG    1234
#define CHK_CONFIG_VAL 1

#define IR_ON       0xFFB04F
#define IR_OFF      0xFFF807
#define IR_LUP      0xFF906F
#define IR_LDN      0xFFB847
#define IR_RED      0xFF9867
#define IR_GRN      0xFFD827
#define IR_BLU      0xFF8877
#define IR_WHI      0xFFA857

#define IR_SMOOTH   0xFF30CF
#define IR_FADE     0xFF58A7
#define IR_STROBE   0xFF00FF
#define IR_FLASH    0xFFB24D

#define C_YELLOW        0xFF38C7
#define C_ORANGE        0xFF50AF
#define C_DARK_ORANGE   0xFF02FD
#define C_RED           0xFFE817
#define C_GREN          0xFF48B7
#define C_BLUE          0xFF8877
#define C_PURPLE        0xFF708F
#define C_HOTPINK       0xFFF00F
#define C_TEAL          0xFF28D7
#define C_LIGHTSEAGREEN 0xFF7887
#define C_OLIVEDRAB     0xFF32CD
#define C_SLATEBLUE     0xFF6897

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
IRProcessor irrecv(PIN_IR);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(PIN_TEMP);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

SegmentDisplay disp;

const unsigned long interval_update = 2000;
unsigned long last_update = 0;

const uint8_t FADE_STEP = 15;
bool power_state = true;



struct config_t
{
  uint8_t ver;
  uint8_t fade_value;
  uint8_t rgb_bright;
  uint8_t red_value;
  uint8_t green_value;
  uint8_t blue_value;
  bool flash_enabled;

} CFG;


float tempValue = 0.0f;
bool tempVisible = false;

DeviceAddress tempDeviceAddress;



void setup() {
  Serial.begin(9600);
  Serial.println(F("SETUP"));

  //enable i2c pullups
  digitalWrite(SDA, 1);
  digitalWrite(SCL, 1);
  Wire.setClock(200000L);
  
  //lights on
  pinMode(PIN_FADE, OUTPUT);
  analogWrite(PIN_FADE, 0);

  for (int a=0;a<13;a++){
    pinMode(unused_pins[a], INPUT_PULLUP);
  }
  
  EEPROM_readA(ADDR_CONFIG, CFG);
  if (CFG.ver != CHK_CONFIG_VAL) {
    Serial.println(F("Config Init"));
    CFG.ver = CHK_CONFIG_VAL;
    CFG.fade_value = 255;
    CFG.rgb_bright = 255;
    CFG.red_value = 255;
    CFG.green_value = 255;
    CFG.blue_value = 255;
    CFG.flash_enabled = true;
    EEPROM_writeA(ADDR_CONFIG, CFG);
  }
  else {
    Serial.println(F("Config Read"));
    
  }

  //force lights on at power on
  CFG.fade_value = 255;
 
  //setup RGB led
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  //setup IR
  irrecv.enableIRIn(); // Start the receiver
  irrecv.blink13(false);

  sensors.begin();

  sensors.getAddress(tempDeviceAddress, 0);
  sensors.setResolution(tempDeviceAddress, 12);
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  setPowerState(true);

  updateRGB();
  updateBrightness();

  disp.start();
  
  Serial.println(F("SETUP DONE"));
}

void loop() {
  // put your main code here, to run repeatedly:

  irrecv.process();
  
  if (irrecv.buttonPressed() == BTN_NOBUTTON) {
      processIRButton(irrecv.lastButton());
  }

  if (tempVisible) {

    disp.updateSegments();
    
    if (millis() - last_update > interval_update) {
        if (sensors.isConversionAvailable(tempDeviceAddress)) {
          tempValue = sensors.getTempC(tempDeviceAddress);
          sensors.requestTemperaturesByAddress(tempDeviceAddress);
          disp.showFloat(tempValue);  
        }
        last_update = millis();
    }
    
    
    
  }
  
}

void processIRButton( long  unsigned int current )
{
     switch (current) {

        case IR_FLASH:
          toggleRGBLed();  
          break;
        case IR_ON:
          setPowerState(true);
          break;
        case IR_OFF:
          setPowerState(false);
          break;
        case IR_STROBE:
          tempVisible = !tempVisible;
          if (!tempVisible) {
            disp.displayOff();  
          }
          break;
        case IR_WHI:
          CFG.red_value = 255;
          CFG.green_value = 255;
          CFG.blue_value = 255;
          updateRGB();
          break;
        case IR_RED:
          CFG.red_value += FADE_STEP;
          updateRGB();
          break;
        case IR_GRN:
          CFG.green_value += FADE_STEP;
          updateRGB();
          break;
        case IR_BLU:
          CFG.blue_value += FADE_STEP;
          updateRGB();
          break;
        case IR_LUP:
          if (power_state == true) {
            CFG.fade_value += FADE_STEP;
            updateBrightness();
          }
          else {
            CFG.rgb_bright += FADE_STEP;
            updateRGB();
          }
          break;
        case IR_LDN:
          if (power_state == true) {
            CFG.fade_value -= FADE_STEP;
            updateBrightness();
          }
          else {
            CFG.rgb_bright -= FADE_STEP;
            updateRGB();
          }
          break;
         
        default:
          processColorButton(current);
          break;
     }
}
void updateRGB()
{
  if (CFG.flash_enabled == true) {
    // Some example procedures showing how to display to the pixels:
    strip.setPixelColor(0, strip.Color(CFG.red_value, CFG.green_value, CFG.blue_value));
    strip.setBrightness(CFG.rgb_bright);
    
    
    Serial.print(F("Flash:" ));
    Serial.print(CFG.rgb_bright,DEC);
    Serial.print(F(" RGB("));
    Serial.print(CFG.red_value,DEC);
    Serial.print(F("|"));
    Serial.print(CFG.green_value, DEC);
    Serial.print(F("|"));
    Serial.print(CFG.blue_value, DEC);
    Serial.println(F(")"));

  }
  else {
    strip.setPixelColor(0, 0);
    Serial.println(F("Flash OFF"));
  }

  strip.show();
  
  EEPROM_writeA(ADDR_CONFIG, CFG);
  
  
}
void updateBrightness()
{
  analogWrite(PIN_FADE, CFG.fade_value);
  Serial.print(F("Lights:"));
  Serial.println(CFG.fade_value, DEC);
  EEPROM_writeA(ADDR_CONFIG, CFG);
}
void toggleRGBLed()
{
  CFG.flash_enabled = !CFG.flash_enabled;
  updateRGB();
}

void setPowerState(boolean mode)
{
  if (mode != power_state) {

    if (mode == true)
    {
      analogWrite(PIN_FADE, CFG.fade_value);
      power_state = true;
    }
    else {
      analogWrite(PIN_FADE, 0);
      power_state = false;
    }

  }
  Serial.print(F("Power: "));
  Serial.println(power_state);
  
}

void processColorButton(unsigned long value)
{
  uint8_t r=0;
  uint8_t g=0;
  uint8_t b=0;

  boolean have_color = true;

  switch (value) {
    
    case C_YELLOW:
      r = 255;
      g = 255;
      b = 0;
      break;
      
    case C_ORANGE:
      r = 255;
      g = 165;
      b = 0;
      break;
      
    case C_DARK_ORANGE:
      r = 255;
      g = 140;
      b = 0;
      break;
      
    case C_RED:
      r = 255;
      g = 0;
      b = 0;
      break;
      
    case C_GREN:
      r = 0;
      g = 255;
      b = 0;
      break;
      
    case C_BLUE:
      r = 0;
      g = 0;
      b = 255;
      break;
      
    case C_PURPLE:
      r = 128;
      g = 0;
      b = 128;
      break;
      
    case C_HOTPINK:
      r = 255;
      g = 105;
      b = 180;
      break;
      
    case C_TEAL:
      r = 0;
      g = 128;
      b = 128;
      break;
      
    case C_LIGHTSEAGREEN:
      r = 32;
      g = 178;
      b = 170;
      break;
      
    case C_OLIVEDRAB:
      r = 107;
      g = 142;
      b = 35;
      break;
      
    case C_SLATEBLUE:
      r = 72;
      g = 61;
      b = 139;
      break;
      
    default:
      have_color = false;
      break;
  }

  if (have_color) {
    CFG.red_value = r;
    CFG.green_value = g;
    CFG.blue_value = b;
    updateRGB();
  }
}
