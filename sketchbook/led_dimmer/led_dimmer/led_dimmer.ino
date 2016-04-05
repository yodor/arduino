#include "EEPROMAnything.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "SegmentDisplay.h"

SegmentDisplay disp;

#include <IRremote.h>

#include <Adafruit_NeoPixel.h>

//to RGB
const uint8_t PIN_RGB  = 10;

//to IR
const uint8_t PIN_IR   = 8;

//to bc547 base
const uint8_t PIN_FADE = 5;

// Data wire is plugged into port 2 on the Arduino
const uint8_t PIN_TEMP = A3;

#define ADDR_CONFIG    0xff
#define CHK_CONFIG_VAL 0x3c

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

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(PIN_TEMP);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

//IR receiver pin
IRrecv irrecv(PIN_IR);

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.


unsigned long ir_last = 0; // code of last pressed IR button

decode_results results;

unsigned long last_update = 0;

//make reading of temperatures non blocking
unsigned long interval_update = 5000;

const uint8_t FADE_STEP = 15;

boolean power_state = false;


typedef struct ConfigStruct
{
  uint8_t ver;
  uint8_t fade_value;
  uint8_t rgb_bright;
  uint8_t red_value;
  uint8_t green_value;
  uint8_t blue_value;
  boolean flash_enabled;

} ConfigStruct;

ConfigStruct cfg;


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

void setup() {

  Serial.begin(9600);

  pinMode(PIN_FADE, OUTPUT);
  analogWrite(PIN_FADE, 0);


  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  irrecv.enableIRIn(); // Start the receiver
  irrecv.blink13(false);

  sensors.begin();

  EEPROM_read(ADDR_CONFIG, cfg);
  if (cfg.ver != CHK_CONFIG_VAL) {
    Serial.println(F("Config Init"));
    cfg.ver = CHK_CONFIG_VAL;
    cfg.fade_value = 255;
    cfg.rgb_bright = 255;
    cfg.red_value = 255;
    cfg.green_value = 255;
    cfg.blue_value = 255;
    cfg.flash_enabled = true;
    EEPROM_write(ADDR_CONFIG, cfg);
  }
  else {
    Serial.println(F("Config Read"));
  }

  setPowerState(true);

  updateRGB();
  updateBrightness();

  digitalWrite(SDA, 1);
  digitalWrite(SCL, 1);
  Wire.setClock(50000L);
  
  disp.start();
  
  Serial.println(F("Setup Done"));
}
volatile float tempValue = 0.0f;
volatile boolean tempVisible = false;

void loop() {

  if (millis() - last_update > interval_update ) {


    sensors.requestTemperatures();

    tempValue = sensors.getTempCByIndex(0);


    //Serial.println(tempValue,1);

    last_update = millis();

  }

  if (tempVisible) {
    disp.showFloat(tempValue);  
  }
  else {
    disp.displayOff();  
  }
  boolean ir_repeat = false;

  if (irrecv.decode(&results)) {

    //Serial.println(results.value,HEX);

    if (results.value == 0xFFFFFFFF) {
      results.value = ir_last;
      ir_repeat = true;
    }
    if (results.value == IR_ON) {
      setPowerState(true);
    }
    else if (results.value == IR_OFF) {
      setPowerState(false);
    }
    else if (results.value == IR_FLASH) {
      toggleFlashState();
    }
    else if (results.value == IR_STROBE && ir_repeat == false) {
      if (tempVisible) {
        
        tempVisible=false;
      }
      else {
        
        tempVisible=true;
      }
      
    }
    else if (results.value == IR_WHI) {
      cfg.red_value = 255;
      cfg.green_value = 255;
      cfg.blue_value = 255;
      updateRGB();
    }
    else if (results.value == IR_RED) {
      cfg.red_value += FADE_STEP;
      updateRGB();
    }
    else if (results.value == IR_GRN) {
      cfg.green_value += FADE_STEP;
      updateRGB();
    }
    else if (results.value == IR_BLU) {
      cfg.blue_value += FADE_STEP;
      updateRGB();
    }
    else if (results.value == IR_LUP) {
      if (power_state == true) {
        cfg.fade_value += FADE_STEP;
        updateBrightness();
      }
      else {
        cfg.rgb_bright += FADE_STEP;
        updateRGB();
      }
    }
    else if (results.value == IR_LDN) {
      if (power_state == true) {
        cfg.fade_value -= FADE_STEP;
        updateBrightness();
      }
      else {
        cfg.rgb_bright -= FADE_STEP;
        updateRGB();
      }
    }
    else {
      processColorButton(results.value);
    }

    ir_last = results.value;

    irrecv.resume();

  }


}

void setPowerState(boolean mode)
{
  if (mode != power_state) {

    if (mode == true)
    {
      analogWrite(PIN_FADE, cfg.fade_value);
      power_state = true;
    }
    else {
      analogWrite(PIN_FADE, 0);
      power_state = false;
    }

  }
  Serial.print("Power: ");
  Serial.println(power_state);
  delay(1000);
}

void updateBrightness()
{
  analogWrite(PIN_FADE, cfg.fade_value);
  Serial.print(F("Strip:"));
  Serial.println(cfg.fade_value);
  EEPROM_write(ADDR_CONFIG, cfg);
}
void updateRGB()
{
  if (cfg.flash_enabled == true) {
    // Some example procedures showing how to display to the pixels:
    strip.setPixelColor(0, strip.Color(cfg.red_value, cfg.green_value, cfg.blue_value));
    strip.setBrightness(cfg.rgb_bright);

    Serial.print(F("Flash: Fade->"));
    Serial.print(cfg.rgb_bright);
    Serial.print(F(" RGB("));
    Serial.print(cfg.red_value);
    Serial.print(F("|"));
    Serial.print(cfg.green_value);
    Serial.print(F("|"));
    Serial.print(cfg.blue_value);
    Serial.println(F(")"));

  }
  else {
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    Serial.println("Flash OFF");
  }

  strip.show();
  EEPROM_write(ADDR_CONFIG, cfg);
}
void toggleFlashState()
{
  cfg.flash_enabled = !cfg.flash_enabled;

  updateRGB();

  delay(1000);
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
    cfg.red_value = r;
    cfg.green_value = g;
    cfg.blue_value = b;
    updateRGB();
  }
}
//
//// Fill the dots one after the other with a color
//void colorWipe(uint32_t c, uint8_t wait) {
//  for(uint16_t i=0; i<strip.numPixels(); i++) {
//      strip.setPixelColor(i, c);
//      strip.show();
//      delay(wait);
//  }
//}
//
//void rainbow(uint8_t wait) {
//  uint16_t i, j;
//
//  for(j=0; j<256; j++) {
//    for(i=0; i<strip.numPixels(); i++) {
//      strip.setPixelColor(i, Wheel((i+j) & 255));
//    }
//    strip.show();
//    delay(wait);
//  }
//}
//
//// Slightly different, this makes the rainbow equally distributed throughout
//void rainbowCycle(uint8_t wait) {
//  uint16_t i, j;
//
//  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
//    for(i=0; i< strip.numPixels(); i++) {
//      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
//    }
//    strip.show();
//    delay(wait);
//  }
//}
//
////Theatre-style crawling lights.
//void theaterChase(uint32_t c, uint8_t wait) {
//  for (int j=0; j<10; j++) {  //do 10 cycles of chasing
//    for (int q=0; q < 3; q++) {
//      for (int i=0; i < strip.numPixels(); i=i+3) {
//        strip.setPixelColor(i+q, c);    //turn every third pixel on
//      }
//      strip.show();
//
//      delay(wait);
//
//      for (int i=0; i < strip.numPixels(); i=i+3) {
//        strip.setPixelColor(i+q, 0);        //turn every third pixel off
//      }
//    }
//  }
//}
//
////Theatre-style crawling lights with rainbow effect
//void theaterChaseRainbow(uint8_t wait) {
//  for (int j=0; j < 256; j++) {     // cycle all 256 colors in the wheel
//    for (int q=0; q < 3; q++) {
//        for (int i=0; i < strip.numPixels(); i=i+3) {
//          strip.setPixelColor(i+q, Wheel( (i+j) % 255));    //turn every third pixel on
//        }
//        strip.show();
//
//        delay(wait);
//
//        for (int i=0; i < strip.numPixels(); i=i+3) {
//          strip.setPixelColor(i+q, 0);        //turn every third pixel off
//        }
//    }
//  }
//}
//
//// Input a value 0 to 255 to get a color value.
//// The colours are a transition r - g - b - back to r.
//uint32_t Wheel(byte WheelPos) {
//  if(WheelPos < 85) {
//   return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
//  } else if(WheelPos < 170) {
//   WheelPos -= 85;
//   return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
//  } else {
//   WheelPos -= 170;
//   return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
//  }
//}

