
#include "DisplayDevice.h"
#include <Wire.h>


#include <Arduino.h>


#include "ChannelSelector.h"
#include "MSGEQ7.h"

const unsigned long interval_message_refresh = 1000;
const unsigned long interval_clock_refresh = 500;
const unsigned long interval_signal_refresh = 1000;
const unsigned long interval_fft_refresh = 33;


const __FlashStringHelper *sm_screen_names[] = {
  (const __FlashStringHelper *)("Volume Box"), (const __FlashStringHelper *)("Selector Box"), (const __FlashStringHelper *)("Power Box")
};

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

Menu* MenuScreen::main = 0;

MenuScreen::MenuScreen()
{
  
  initMainMenu();
  
  Menu::Current = main;
  Menu::Current->parent = 0;
  Menu::Current->index = 0;


}
void MenuScreen::initMainMenu()
{
  if (MenuScreen::main) return;
  
  MenuItem *item = 0;

  Menu *bluetooth = new Menu();
  bluetooth->processor = new BluetoothMenuProcessor();
  bluetooth->allocateItems(2);
  item = bluetooth->itemAt(0);
  item->name = "Power Cycle";
  item->menu = 0;

  item = bluetooth->itemAt(1);
  item->name = "Power";
  item->menu = 0;

  Menu *cubieboard = new Menu();
  cubieboard->processor = new CubieboardMenuProcessor();
  cubieboard->allocateItems(2);
  item = cubieboard->itemAt(0);
  item->name = "Power";
  item->menu = 0;

  item = cubieboard->itemAt(1);
  item->name = "Auto-Power";
  item->menu = 0;


  Menu *settings = new Menu();
  settings->processor = new SettingsMenuProcessor();
  settings->allocateItems(6);

  item = settings->itemAt(0);
  item->name = "Bluetooth";
  item->menu = bluetooth;

  item = settings->itemAt(1);
  item->name = "Cubeboard";
  item->menu = cubieboard;

  item = settings->itemAt(2);
  item->name = "Auto-sleep";
  item->menu = 0;

  item = settings->itemAt(3);
  item->name = "Timeout";
  item->menu = 0;

  item = settings->itemAt(4);
  item->name = "Threshold DB";
  item->menu = 0;

  item = settings->itemAt(5);
  item->name = "Amp Power";
  item->menu = 0;


  Menu *clock = new Menu();
  clock->processor = new ClockMenuProcessor();

  clock->index = -1;

  main = new Menu();
  main->processor = new MenuProcessor();
  main->allocateItems(2);
  main->parent = 0;
  
  item = main->itemAt(0);
  item->name = "Adjust Clock";
  item->menu = clock;

  item = main->itemAt(1);
  item->name = "Settings";
  item->menu = settings;
}
MenuScreen::~MenuScreen()
{

}
void MenuScreen::render(char *line0, char* line1)
{
   Menu::Current->processor->renderScreen(line0, line1);

}
void MenuScreen::processButton(PANEL_BUTTON button)
{
  if (Menu::Current && Menu::Current->processor) {
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
  
  //snprintf(line0, 17, "%16s", "                ");

  snprintf(line0, 6, "%5s", clock_str);
  
  if (SELECTOR.isMute() == true) {

    snprintf(line1, 17, "%s - %s", (const char*)SELECTOR.getChannelName(), "Mute");


  }
  else {

    snprintf(line1, 17, "%16s",  (const char*)SELECTOR.getChannelName() );

  }
  
    
}
void SelectorScreen::processLoop(long unsigned current_millis)
{
  
  if (current_millis - COUNTERS.clock_update >= interval_clock_refresh) {

      updateClock();
      MDISPLAY.renderClock(clock_str);
      COUNTERS.clock_update = current_millis;
      
  }
  
  if (current_millis - COUNTERS.fft_update >= interval_fft_refresh) {
        EQ7.process();
        uint8_t *lv = EQ7.getLevels();
        //&levels[0];
        MDISPLAY.renderFFT(lv);
        COUNTERS.fft_update = current_millis;
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
  
  snprintf(clock_str, 6, "%5s", "--:--");

  if(timeStatus()!= timeSet) {
    //Serial.println("Unable to sync with the RTC");

  }
  else {
    if (clock_dot) {
      snprintf(clock_str, 6, "%02d:%02d", hour(), minute());
    }
    else {
      snprintf(clock_str, 6, "%02d %02d", hour(), minute());
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
  while (bit_is_set(ADCSRA,ADSC));
  vcc_result = ADCL;
  vcc_result |= ADCH<<8;
  vcc_result = 1126400L / vcc_result; // Back-calculate AVcc in mV
  GLOBALS.vcc_selector = vcc_result;

}

void SignalScreen::render(char *line0, char* line1)
{

  float vcc = 0.0;
  
  if (current_mode == SM_VOLUME) {  
    snprintf(line0, 17, "%16s",  "Volume Box");
    vcc = GLOBALS.vcc_volume/1000.0;
    dtostrf(vcc, 4, 1, buf_vcc);
    dtostrf(GLOBALS.temp_volume, 5, 1, buf_temp);
  }
  else if (current_mode == SM_SELECTOR) {
    snprintf(line0, 17, "%16s",  "Selector Box");
    vcc = GLOBALS.vcc_selector/1000.0;
    dtostrf(vcc, 4, 1, buf_vcc);
    dtostrf(GLOBALS.temp_selector, 5, 1, buf_temp);
  }
  else if (current_mode == SM_POWER) {
    snprintf(line0, 17, "%16s",  "Power Box");
    vcc = GLOBALS.vcc_power/1000.0;
    dtostrf(vcc, 4, 1, buf_vcc);
    dtostrf(GLOBALS.temp_power, 5, 1, buf_temp);
  }
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
  int sm_mode = current_mode;
  
  if (button == BUTTON_CANCEL) {
    MDISPLAY.changeDisplayMode(DisplayDevice::MODE_SELECTOR);
  }
  else if (button == BUTTON_UP) {
    sm_mode++;  
  }
  else if (button == BUTTON_DOWN) {
    sm_mode--;
  }
  
  if (sm_mode<SM_POWER) {
    sm_mode = SM_SELECTOR;
  }
  if (sm_mode>SM_SELECTOR) {
    sm_mode = SM_POWER;
  }
  current_mode = SIGNAL_MODE(sm_mode);
}

DisplayDevice::DisplayDevice() : LiquidCrystal_I2C(0x20,16,2), current_screen(0)
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

  this->backlight();

  
}
void DisplayDevice::powerOff()
{
  
  this->noDisplay();
  this->noBacklight();

}
void DisplayDevice::moveCursor(int x, int y)
{
  this->cursor_x = x;
  this->cursor_y = y;
}
void DisplayDevice::showMessage(const __FlashStringHelper* text)
{
  showMessage(reinterpret_cast<const char*>(text));
}
void DisplayDevice::showMessage(const char* text)
{

  if (strlen(text)>0) {
    snprintf(message, 17, "%-16s", text);
    COUNTERS.message_update = millis();
    updateScreen();
  }

}

void DisplayDevice::processLoop()
{
  unsigned long current_millis = millis();

  bool need_update = false;
  
  if  ( COUNTERS.message_update > 0 &&  current_millis - COUNTERS.message_update > CONFIG.values().display_message_timeout ) {
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
  
  

  current_screen->render(line0, line1);
  
  if (COUNTERS.message_update>0) {
    snprintf(line1, 17, "%-16s",  message);
  }

  this->setCursor(current_screen->getXLine0(), 0);
  this->print(line0);

  this->setCursor(0,1);
  this->print(line1);

  if ( (this->cursor_x>-1) && (this->cursor_y>-1) ) {

    cursor();
    blink();
    setCursor(cursor_x,cursor_y);

  }
  else {
    setCursor(0,0);
    noCursor();
    noBlink();
    
  }

  Serial.println(line0);
  Serial.println(line1);
}


void DisplayDevice::renderFFT(uint8_t* levels)
{
  if (current_mode != MODE_SELECTOR) return;

  setCursor(0,0);
  for (int a=0;a<7;a++) {
    byte val = levels[a];
    MDISPLAY.write(val);
  }

}
void DisplayDevice::renderClock(char* clock_str)
{
  this->setCursor(11,0);
  this->print(clock_str);
}

/**
 * void debugMenu(int menu_button)
 * {
 * 
 * Serial.print("Active Menu: ");
 * Serial.print(active_menu);
 * Serial.print("|");
 * Serial.print("MenuIndex: ");
 * Serial.print(menu_index);
 * Serial.print("|");
 * Serial.print(button_names[menu_button]);  
 * Serial.print("|");
 * Serial.println();
 * 
 * }
 */

void DisplayDevice::processButton(PANEL_BUTTON menu_button)
{

  if (!current_screen)return;
  current_screen->processButton(menu_button);
  updateScreen();
  
}



void DisplayDevice::loadChars()
{

  //custom display character holder
  byte bar[8];

  for (int a=7;a>=0;a--) {

    bar[a] = 0b00000;
  }

  for (int a=7;a>=0;a--) {
    bar[a] = 0b11111;
    createChar((7-a), bar);

  }

}

DisplayDevice MDISPLAY = DisplayDevice();




