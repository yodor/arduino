
#include "DisplayDevice.h"
#include <Wire.h>


#include <Arduino.h>


#include "ChannelSelector.h"
#include "MSGEQ7.h"
#include "TextUtils.h"

const unsigned long interval_message_refresh = 1000;
const unsigned long interval_clock_refresh = 500;
const unsigned long interval_signal_refresh = 1000;




DisplayScreen::DisplayScreen()
{
  line0_x = 0;
}
DisplayScreen::~DisplayScreen()
{

}
void DisplayScreen::processLoop(long unsigned current_millis)
{

}
void DisplayScreen::processButton(PANEL_BUTTON button)
{

}
int DisplayScreen::getXLine0()
{
  return line0_x;
}


//Menu *MenuScreen::main = 0;

MenuScreen::MenuScreen() : main(0)
{

  initMainMenu();

  Menu::Current = main;
  Menu::Current->parent = 0;
  Menu::Current->index = 0;


}
void MenuScreen::initMainMenu()
{


  MenuItem *item = 0;

  Menu *bluetooth = new Menu();
  bluetooth->processor = new BluetoothMenuProcessor();
  bluetooth->allocateItems(2);
  item = bluetooth->itemAt(0);
  item->name = F("Power Cycle");
  item->menu = 0;

  item = bluetooth->itemAt(1);
  item->name = F("Power");
  item->menu = 0;

  Menu *cubieboard = new Menu();
  cubieboard->processor = new CubieboardMenuProcessor();
  cubieboard->allocateItems(2);
  item = cubieboard->itemAt(0);
  item->name = F("Power");
  item->menu = 0;

  item = cubieboard->itemAt(1);
  item->name = F("Auto-Power");
  item->menu = 0;


  Menu *settings = new Menu();
  settings->processor = new SettingsMenuProcessor();
  settings->allocateItems(8);

  item = settings->itemAt(0);
  item->name = F("Bluetooth");
  item->menu = bluetooth;

  item = settings->itemAt(1);
  item->name = F("Cubeboard");
  item->menu = cubieboard;

  item = settings->itemAt(2);
  item->name = F("Auto-sleep");
  item->menu = 0;

  item = settings->itemAt(3);
  item->name = F("Timeout");
  item->menu = 0;

  item = settings->itemAt(4);
  item->name = F("Threshold DB");
  item->menu = 0;

  item = settings->itemAt(5);
  item->name = F("Amp Power");
  item->menu = 0;

  item = settings->itemAt(6);
  item->name = F("FFT Refresh");
  item->menu = 0;

  item = settings->itemAt(7);
  item->name = F("OLED Cntr");
  item->menu = 0;

  Menu *clock = new Menu();
  clock->processor = new ClockMenuProcessor();

  clock->index = -1;

  main = new Menu();
  main->processor = new MenuProcessor();
  main->allocateItems(2);
  main->parent = 0;

  item = main->itemAt(0);
  item->name = F("Adjust Clock");
  item->menu = clock;

  item = main->itemAt(1);
  item->name = F("Settings");
  item->menu = settings;
}
MenuScreen::~MenuScreen()
{
  Serial.println(F("DTOR"));
  if (main) {
    delete main;
    main = 0;
  }
}
void MenuScreen::render(char *line0, char* line1)
{
  Menu::Current->processor->renderScreen(line0, line1);

}
void MenuScreen::processButton(PANEL_BUTTON button)
{

  if (Menu::Current && Menu::Current->processor) {
    Serial.println(F("MenuScreen::processButton"));
    Menu::Current->processor->processButton(button);
  }

}

SelectorScreen::SelectorScreen()
{
  line0_x = 11;
  clock_dot = false;
  updateClock();
}
SelectorScreen::~SelectorScreen()
{

}
void SelectorScreen::render(char *line0, char* line1)
{


  snprintf(line0, 10, "%4s%5s", "    ", clock_str);

  if (SELECTOR.isMute() == true) {

    snprintf(line1, 17, "%16s", "Mute");

  }
  else {

    snprintf(line1, 17, "%16s",  TEXTS.getChannelName(SELECTOR.getActiveChannel()) );

  }


}
void SelectorScreen::processLoop(long unsigned current_millis)
{

  if (current_millis - COUNTERS.clock_update >= interval_clock_refresh) {

    updateClock();
    MDISPLAY.renderClock(clock_str);
    COUNTERS.clock_update = millis();

  }

  if (current_millis - COUNTERS.fft_update >= CONFIG.values().display_fft_refresh) {
    EQ7.process();
    const uint8_t *lv = EQ7.getLevels();
    //&levels[0];
    MDISPLAY.renderFFT(lv);
    COUNTERS.fft_update = millis();
  }


}
void SelectorScreen::processButton(PANEL_BUTTON button)
{
  if (button == BUTTON_UP) {
    SELECTOR.setNextChannel();
  }
  else if (button == BUTTON_DOWN) {
    SELECTOR.setPreviousChannel();
  }
}

void SelectorScreen::updateClock()
{
  clock_dot = !clock_dot;

  snprintf(clock_str, 10, "%4s%5s", "    ", "--:--");

  if (timeStatus() != timeSet) {
    //Serial.println("Unable to sync with the RTC");

  }
  else {
    if (clock_dot) {
      snprintf(clock_str, 10, "%4s%02d:%02d", "    ", hour(), minute());
    }
    else {
      snprintf(clock_str, 10, "%4s%02d %02d", "    ", hour(), minute());
    }
  }


}


SignalScreen::SignalScreen()
{
  current_mode = SM_VOLUME;
  updateData();
}
SignalScreen::~SignalScreen()
{

}
void SignalScreen::updateData()
{
  GLOBALS.temp_selector = RTC.getTemp();

  long vcc_result;

  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  vcc_result = ADCL;
  vcc_result |= ADCH << 8;
  vcc_result = 1126400L / vcc_result; // Back-calculate AVcc in mV
  GLOBALS.vcc_selector = vcc_result;

}

void SignalScreen::render(char *line0, char* line1)
{

  float vcc = 0.0;
  float temp = 0.0;

  snprintf(line0, 17, "%16s",  TEXTS.getScreenName(current_mode));

  if (current_mode == SM_VOLUME) {

    vcc = GLOBALS.vcc_volume / 1000.0;
    temp = GLOBALS.temp_volume;
  }
  else if (current_mode == SM_SELECTOR) {

    vcc = GLOBALS.vcc_selector / 1000.0;
    temp = GLOBALS.temp_selector;
  }
  else if (current_mode == SM_POWER) {

    vcc = GLOBALS.vcc_power / 1000.0;
    temp = GLOBALS.temp_power;

  }
  dtostrf(vcc, 4, 1, buf_vcc);
  dtostrf(temp, 5, 1, buf_temp);
  snprintf(line1, 17, "V:%s T:%s", buf_vcc,  buf_temp);
}
void SignalScreen::processLoop(unsigned long current_millis)
{
  if (current_millis - COUNTERS.signal_update >= interval_signal_refresh) {

    updateData();
    MDISPLAY.updateScreen();
    COUNTERS.signal_update = current_millis;
  }

}
void SignalScreen::processButton(PANEL_BUTTON button)
{

  int next_screen = current_mode;

  if (button == BUTTON_CANCEL) {
    MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SELECTOR);
  }
  else if (button == BUTTON_UP) {
    next_screen++;

  }
  else if (button == BUTTON_DOWN) {
    next_screen--;
  }

  current_mode = (SIGNAL_MODE)next_screen;

  if (current_mode < SM_VOLUME) current_mode = SM_SELECTOR;
  if (current_mode > SM_SELECTOR) current_mode = SM_VOLUME;

}

DisplayDevice::DisplayDevice() : current_screen(0)
{

  cursor_x = -1;
  cursor_y = -1;

  COUNTERS.message_update = 0;

  //timeout_in = CONFIG.values().signal_sleep_timeout  / 1000;

}
DisplayDevice::~DisplayDevice()
{

}
void DisplayDevice::powerOn()
{
  this->init();

  this->loadChars();

  sendCommand(0x0C);  	// **** Turn on On

}
void DisplayDevice::powerOff()
{

  sendCommand(0x08);  	// **** Turn on Off
}
void DisplayDevice::moveCursor(int x, int y)
{
  this->cursor_x = x;
  this->cursor_y = y;
}
void DisplayDevice::showMessage(const __FlashStringHelper* ptr)
{
  showMessage(TEXTS.getText(ptr));

}
void DisplayDevice::showMessage(const char* text)
{

  if (strlen(text) > 0) {
    snprintf(message, 17, "%-16s", text);
    COUNTERS.message_update = millis();
    updateScreen();
  }

}

void DisplayDevice::processLoop()
{
  unsigned long current_millis = millis();

  bool need_update = false;

  if  ( COUNTERS.message_update > 0 &&  current_millis - COUNTERS.message_update > interval_message_refresh ) {
    //reset display
    COUNTERS.message_update = 0;
    updateScreen();
  }


  current_screen->processLoop(current_millis);



}

void DisplayDevice::changeDisplayMode(DISPLAY_MODE mode)
{

  clear();


  if (mode > MODE_FIRST && mode < MODE_LAST) {
    current_mode = mode;
  }
  else {
    current_mode = MODE_SELECTOR;
  }


  if (current_screen) {
    delete current_screen;
  }

  if (current_mode == MODE_SELECTOR) {

    current_screen = new SelectorScreen();


  }
  else if (current_mode == MODE_MENU) {

    //menu_index = 0;
    //active_menu = -1;


    current_screen = new MenuScreen();


  }
  else if (current_mode == MODE_SIGNAL) {

    current_screen = new SignalScreen();

  }

  updateScreen();

}

DisplayDevice::DISPLAY_MODE DisplayDevice::currentMode()
{
  return current_mode;
}



void DisplayDevice::updateScreen()
{

  if (!current_screen) return;

  //enableCursor(false);

  current_screen->render(line0, line1);

  if (COUNTERS.message_update > 0) {
    snprintf(line1, 17, "%-16s",  message);
  }

  this->setCursor(current_screen->getXLine0(), 0);
  this->print(line0);

  this->setCursor(0, 1);
  this->print(line1);

  if (cursor_x > -1 && cursor_y > -1) {

    enableCursor(true);
    setCursor(cursor_x, cursor_y);

  }
  else {
    enableCursor(false);
  }

  Serial.println(line0);
  Serial.println(line1);
}


void DisplayDevice::renderFFT(const uint8_t* levels)
{
  if (current_mode != MODE_SELECTOR) return;

  setCursor(0, 0);
  for (int a = 0; a < 7; a++) {
    byte val = levels[a];
    sendData(val);
  }
  //sendData(0x20);
  //sendData(0x20);
  //sendData(0x20);
  //sendData(0x20);

}
void DisplayDevice::renderClock(char* clock_str)
{
  //16-5=11 - 4 spaces = 7
  this->setCursor(7, 0);
  this->print(clock_str);
}

/**
   void debugMenu(int menu_button)
   {

   Serial.print("Active Menu: ");
   Serial.print(active_menu);
   Serial.print("|");
   Serial.print("MenuIndex: ");
   Serial.print(menu_index);
   Serial.print("|");
   Serial.print(button_names[menu_button]);
   Serial.print("|");
   Serial.println();

   }
*/


void DisplayDevice::processButton(PANEL_BUTTON menu_button)
{

  if (!current_screen)return;
  current_screen->processButton(menu_button);
  updateScreen();

}


#define OLED_Address 0x3c
#define OLED_Command_Mode 0x80
#define OLED_Data_Mode 0x40


void DisplayDevice::loadChars()
{

  //custom display character holder
  byte bar[8];

  for (int a = 7; a >= 0; a--) {

    bar[a] = 0b00000;
  }

  for (int a = 7; a >= 0; a--) {
    bar[a] = 0b11111;
    createChar((7 - a), bar);
    delay(30);
  }

}
////
void DisplayDevice::enableCursor(bool mode)
{
  if (mode == this->cursor_enabled)return;

  if (mode) {
    sendCommand(0x0F);
  }
  else {
    sendCommand(0x0C);
  }
  this->cursor_enabled = mode;
}
void DisplayDevice::clear()
{
  bool old_cursor = this->cursor_enabled;
  if (old_cursor) {
    enableCursor(false);
  }
  sendCommand(0x01);	// **** Clear display
  enableCursor(old_cursor);
}
const int row_offsets[] = { 0x00, 0x40 };
void DisplayDevice::setCursor(uint8_t col, uint8_t row)
{

  sendCommand(0x80 | (col + row_offsets[row]));
}
void DisplayDevice::print(const char* data)
{
  uint8_t i = 0;
  while (data[i])
  {
    sendData(data[i]);      // *** Show String to OLED
    i++;
    if (i > 15) break;
  }
}
void DisplayDevice::createChar(uint8_t location, uint8_t charmap[])
{
  location &= 0x7;            // we only have 8 locations 0-7

  sendCommand(0x40 | (location << 3));
  delayMicroseconds(30);

  for (int i = 0; i < 8; i++)
  {
    sendData(charmap[i]);      // call the virtual write method
    delayMicroseconds(40);
  }
}
void DisplayDevice::sendCommand(uint8_t command)
{
  Wire.beginTransmission(OLED_Address); 	 // **** Start I2C
  Wire.write((uint8_t)OLED_Command_Mode);     		 // **** Set OLED Command mode
  Wire.write(command);
  Wire.endTransmission();                 	 // **** End I2C


}
void DisplayDevice::sendData(uint8_t data)
{
  Wire.beginTransmission(OLED_Address);  	// **** Start I2C
  Wire.write((uint8_t)OLED_Data_Mode);     		// **** Set OLED Data mode
  Wire.write(data);
  Wire.endTransmission();                     // **** End I2C
}
void DisplayDevice::setContrast(uint8_t contrast) // contrast as 0x00 to 0xFF
{
  // **** Set OLED Characterization *** //
  sendCommand(0x2A);
  sendCommand(0x79);
  //
  sendCommand(0x81);  	// Set Contrast
  sendCommand(contrast);	// contrast value

  // **** Exiting Set OLED Characterization
  sendCommand(0x78);
  sendCommand(0x28);
  sendCommand(0x2A);

  //sendCommand(0x05);   // **** Set Entry Mode
  sendCommand(0x06);   // **** Set Entry Mode
  sendCommand(0x08);
  sendCommand(0x28);   // **** Set "IS"=0 , "RE" =0 //28
  sendCommand(0x01);
  sendCommand(0x80);   // **** Set DDRAM Address to 0x80 (line 1 start)
  delay(100);


}
void DisplayDevice::invertDisplay(bool mode)
{

}
void DisplayDevice::init()
{

  // *** I2C initial *** //
  delay(100);
  sendCommand(0x2A);	// **** Set "RE"=1	00101010B
  sendCommand(0x71);
  sendCommand(0x5C);
  sendCommand(0x28);

  sendCommand(0x08);	// **** Set Sleep Mode On
  sendCommand(0x2A);	// **** Set "RE"=1	00101010B
  sendCommand(0x79);	// **** Set "SD"=1	01111001B

  sendCommand(0xD5);
  sendCommand(0x70);
  sendCommand(0x78);	// **** Set "SD"=0

  sendCommand(0x08);	// **** Set 5-dot, 3 or 4 line(0x09), 1 or 2 line(0x08)

  sendCommand(0x06);	// **** Set Com31-->Com0  Seg0-->Seg99

  // **** Set OLED Characterization *** //
  sendCommand(0x2A);  	// **** Set "RE"=1
  sendCommand(0x79);  	// **** Set "SD"=1

  // **** CGROM/CGRAM Management *** //
  sendCommand(0x72);  	// **** Set ROM
  sendCommand(0x00);  	// **** Set ROM A and 8 CGRAM


  sendCommand(0xDA); 	// **** Set Seg Pins HW Config
  sendCommand(0x10);

  sendCommand(0x81);  	// **** Set Contrast
  sendCommand(0xFF);

  sendCommand(0xDB);  	// **** Set VCOM deselect level
  sendCommand(0x30);  	// **** VCC x 0.83

  sendCommand(0xDC);  	// **** Set gpio - turn EN for 15V generator on.
  sendCommand(0x03);

  sendCommand(0x78);  	// **** Exiting Set OLED Characterization
  sendCommand(0x28);
  sendCommand(0x2A);
  //sendCommand(0x05); 	// **** Set Entry Mode
  sendCommand(0x06); 	// **** Set Entry Mode
  sendCommand(0x08);
  sendCommand(0x28); 	// **** Set "IS"=0 , "RE" =0 //28
  sendCommand(0x01);
  sendCommand(0x80); 	// **** Set DDRAM Address to 0x80 (line 1 start)

  delay(100);

}
DisplayDevice MDISPLAY = DisplayDevice();




