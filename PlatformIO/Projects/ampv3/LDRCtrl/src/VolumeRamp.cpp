#include "VolumeRamp.hpp"
#include "Config.hpp"

namespace VolumeRamp {

void rampTo(LDRVolume *channels[], uint8_t count, uint8_t targetStep, bool okOut[]) {
  uint8_t cur[2];
  int dir[2];
  uint8_t dist[2];
  uint8_t iterations = 0;

  for (uint8_t c = 0; c < count; c++) {
    cur[c] = channels[c]->currentStep();
    dist[c] = (targetStep > cur[c]) ? (uint8_t)(targetStep - cur[c]) : (uint8_t)(cur[c] - targetStep);
    dir[c] = (targetStep > cur[c]) ? 1 : -1;
    if (dist[c] == 0) {
      // Already "at" the target BY COUNT -- but always explicitly
      // re-apply rather than skipping the write entirely. currentStep()
      // matching the target is no guarantee the real hardware duty still
      // does: e.g. right after a fresh characterization, which leaves raw
      // duty at whatever the last characterization point was, without
      // resetting currentStep_ to reflect that mismatch. Skipping here
      // would silently leave stale hardware state in place while
      // reporting success.
      okOut[c] = channels[c]->setStep(targetStep);
    } else if (dist[c] > iterations) {
      iterations = dist[c];
    }
  }

  for (uint8_t i = 1; i <= iterations; i++) {
    for (uint8_t c = 0; c < count; c++) {
      if (dist[c] > 0 && i <= dist[c]) {
        uint8_t next = (uint8_t)(cur[c] + dir[c] * (int)i);
        okOut[c] = channels[c]->setStep(next);
      }
    }
    if (i < iterations) {
      delay(VOL_RAMP_STEP_DELAY_MS);
    }
  }
}

} // namespace VolumeRamp
