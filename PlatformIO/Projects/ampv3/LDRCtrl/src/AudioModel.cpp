#include "AudioModel.hpp"
#include "Config.hpp"
#include <LittleFS.h>
#include <stdlib.h>

namespace {
constexpr const char *PATH = "/audio_model.txt";   // "<source ohms> <load ohms>"
float gSrc = AUDIO_SOURCE_OHMS_DEFAULT;
float gLoad = AMP_INPUT_LOAD_OHMS_DEFAULT;

void save() {
  File f = LittleFS.open(PATH, "w");
  if (!f) return;
  f.print(gSrc, 1); f.print(' '); f.print(gLoad, 1); f.println();
  f.close();
}
} // namespace

namespace AudioModel {

void begin() {
  File f = LittleFS.open(PATH, "r");
  if (!f) return;                                  // never set: the Config.hpp defaults
  char buf[48] = {0};
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  char *end;
  const float s = (float)strtod(buf, &end);
  if (end == buf) return;
  const char *p = end;
  const float l = (float)strtod(p, &end);
  if (end == p) return;
  if (s >= SRC_MIN && s <= SRC_MAX) gSrc = s;
  if (l == 0.0f || (l >= LOAD_MIN && l <= LOAD_MAX)) gLoad = l;
}

float sourceOhms() { return gSrc; }
float loadOhms() { return gLoad; }

bool setSource(float ohms) {
  if (!(ohms >= SRC_MIN && ohms <= SRC_MAX)) return false;
  gSrc = ohms; save(); return true;
}
bool setLoad(float ohms) {
  if (!(ohms == 0.0f || (ohms >= LOAD_MIN && ohms <= LOAD_MAX))) return false;
  gLoad = ohms; save(); return true;
}

} // namespace AudioModel
