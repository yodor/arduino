#include "TextUtils.h"

const char CH_AUX[] PROGMEM = "Aux";
const char CH_CUBIE[] PROGMEM = "Cubie";
const char CH_OPTICAL[] PROGMEM = "Optical";
const char CH_BT[] PROGMEM = "Bluetooth";
PGM_P const channel_names[] PROGMEM = { CH_AUX, CH_CUBIE, CH_OPTICAL, CH_BT };

const char B_VOLUME[] PROGMEM = "Volume Box";
const char B_SELECTOR[] PROGMEM = "Selector Box";
const char B_POWER[] PROGMEM = "Power Box";
PGM_P const screen_names[] PROGMEM = { B_VOLUME, B_SELECTOR, B_POWER };


const char BTN_UP[] PROGMEM = "Up";
const char BTN_DOWN[] PROGMEM = "Down";
const char BTN_ENTER[] PROGMEM = "Enter";
const char BTN_CANCEL[] PROGMEM = "Cancel";
PGM_P const button_names[] = { BTN_UP, BTN_DOWN, BTN_ENTER, BTN_CANCEL };

const char BL_POWER[] PROGMEM = "BT_POWER";
const char BL_FWD[] PROGMEM = "BT_FWD";
const char BL_BACK[] PROGMEM = "BT_BACK";
const char BL_VOLUP[] PROGMEM = "BT_VOLUP";
const char BL_VOLDN[] PROGMEM = "BT_VOLDN";
const char BL_PLAY[] PROGMEM = "BT_PLAY";

PGM_P const bt_button_names[]  = { BL_POWER, BL_FWD, BL_BACK, BL_VOLUP, BL_VOLDN, BL_PLAY };


TextUtils::TextUtils()
{}
TextUtils::~TextUtils()
{}

const char* TextUtils::getChannelName(CHANNEL idx)
{
    memset(buf, 0 , 17);
    strncpy_P(buf, (PGM_P)pgm_read_word(&(channel_names[idx])), 16);
    return buf;
}
const char* TextUtils::getScreenName(SIGNAL_MODE idx)
{
    memset(buf, 0 , 17);
    strncpy_P(buf, (PGM_P)pgm_read_word(&(screen_names[idx])), 16);
    return buf;
    
}
const char* TextUtils::getText(const __FlashStringHelper* ptr)
{
    memset(buf, 0 , 17);
    strncpy_P(buf, reinterpret_cast<const char*>(ptr), 17);
    return buf;
}
TextUtils TEXTS = TextUtils();
