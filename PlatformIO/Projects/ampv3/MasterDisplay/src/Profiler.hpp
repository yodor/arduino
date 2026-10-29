#pragma once
#include <cstdint>

// A timing statistic for the debug lines: how many samples, their average
// and the largest one. Add microseconds; read milliseconds; reset each
// report period.
//
//   PerfStat render;
//   render.add(micros() - t0);
//   ... once a second: Console::log("Perf", "render %sms (max %s)",
//                                   Console::num(render.avgMs(), 2).s,
//                                   Console::num(render.maxMs(), 2).s);
//       render.reset();
struct PerfStat {
    uint32_t count = 0;
    uint64_t sumUs = 0;
    uint32_t maxUs = 0;

    void  add(uint32_t us) { ++count; sumUs += us; if (us > maxUs) maxUs = us; }
    float avgMs() const    { return count ? static_cast<float>(sumUs) / 1000.0f / count : 0.0f; }
    float maxMs() const    { return maxUs / 1000.0f; }
    void  reset()          { count = 0; sumUs = 0; maxUs = 0; }
};
