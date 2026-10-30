// Host test: settings text. Run all with: python3 test/run_tests.py
// settings.txt's key=value handling: comments, whitespace, CRLF, unknown keys kept, update in
// place, and format -> parse round trip.
#include <cstdio>
#include <cstring>
#include "SettingsText.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace SettingsText;
int main(){
  Store s;
  const char *text =
    "# LDR attenuator settings\r\n"
    "source_ohms = 300\r\n"
    "\r\n"
    "  load_ohms=10000   # amp input\r\n"
    "auto_pref=DEEP\n"
    "future_key=from a newer firmware\n"
    "no equals sign here\n"
    "=value without key\n";
  CHECK(parse(text, s) == 4);
  CHECK(strcmp(s.get("source_ohms"), "300") == 0 && strcmp(s.get("load_ohms"), "10000") == 0);
  CHECK(strcmp(s.get("auto_pref"), "DEEP") == 0 && strcmp(s.get("future_key"), "from a newer firmware") == 0);
  CHECK(s.get("missing") == nullptr);
  // update in place keeps the order; a new key goes to the end
  CHECK(s.set("source_ohms", "47.5") && s.set("bal_default", "0"));
  CHECK(s.n == 5 && strcmp(s.key[0], "source_ohms") == 0 && strcmp(s.val[0], "47.5") == 0 && strcmp(s.key[4], "bal_default") == 0);
  // round trip: the unknown key survives being written and read back
  char out[1024]; format(s, out, sizeof out, "LDR attenuator settings");
  Store t; CHECK(parse(out, t) == 5);
  for (uint8_t i = 0; i < s.n; i++) CHECK(strcmp(t.get(s.key[i]), s.val[i]) == 0);
  CHECK(strncmp(out, "# LDR attenuator settings\n", 26) == 0);
  // capacity: the store refuses beyond MAX_KEYS instead of overflowing
  Store u; char k[16];
  for (int i = 0; i < MAX_KEYS; i++) { snprintf(k, sizeof k, "k%d", i); CHECK(u.set(k, "v")); }
  CHECK(!u.set("one_too_many", "v") && u.n == MAX_KEYS);
  printf("  parsed 4 of 8 lines (comments, blanks and malformed skipped), unknown key kept through a rewrite\n");
  printf("settings text host test: PASS\n"); return 0; }
