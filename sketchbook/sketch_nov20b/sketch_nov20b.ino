
 #include <avr/pgmspace.h>
 
const char string_1[] PROGMEM = "String 1";
const char string_2[] PROGMEM = "String 2";
const char string_3[] PROGMEM = "String 3";
const char string_4[] PROGMEM = "String 4";
const char string_5[] PROGMEM = "String 5";


PGM_P const string_table[] PROGMEM = 
{
  string_1,
  string_2,
  string_3,
  string_4,
  string_5
};

void setup()
{
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  Serial.begin(9600);
  Serial.println(F("Setup"));
    foo(F("TestFlashString"));
}
char buf[255];
void loop()
{

}
void foo(const __FlashStringHelper* ptr)
{
  
  for (unsigned char i = 0; i < 5; i++)
  {
    strcpy_P(buf, (PGM_P)pgm_read_word(&(string_table[i])));
    //strcpy_P(buf, string_table[i]);
    // Display buffer on LCD.
    Serial.println(buf);
  }
  
  strncpy_P(buf, reinterpret_cast<const char*>(ptr), 17);
  Serial.println(buf);
  
}

