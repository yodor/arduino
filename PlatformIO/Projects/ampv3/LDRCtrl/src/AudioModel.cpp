#include "AudioModel.hpp"
#include "Config.hpp"
#include "Settings.hpp"
#include <stdlib.h>

namespace {
float gSrc = AUDIO_SOURCE_OHMS_DEFAULT;
float gLoad = AMP_INPUT_LOAD_OHMS_DEFAULT;
void save() {                                      // settings.txt: source_ohms / load_ohms
  char v[24];
  Settings::formatNumber(v, sizeof v, gSrc);  Settings::set("source_ohms", v);
  Settings::formatNumber(v, sizeof v, gLoad); Settings::set("load_ohms", v);
}
} // namespace

namespace AudioModel {

void begin() {                                     // after Settings::begin()
  const char *v = Settings::get("source_ohms");
  if (v) { const float x = (float)strtod(v, nullptr); if (x >= SRC_MIN && x <= SRC_MAX) gSrc = x; }
  v = Settings::get("load_ohms");
  if (v) { const float x = (float)strtod(v, nullptr); if (x == 0.0f || (x >= LOAD_MIN && x <= LOAD_MAX)) gLoad = x; }
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
