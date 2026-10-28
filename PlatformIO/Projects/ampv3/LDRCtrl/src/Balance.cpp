#include "Balance.hpp"
#include "LDRVolume.hpp"

namespace { int8_t gBal = 0; }

namespace Balance {
int8_t get() { return gBal; }
void set(int8_t n, LDRVolume &left, LDRVolume &right) {
  if (n > MAX_UNITS) n = MAX_UNITS;
  if (n < -MAX_UNITS) n = -MAX_UNITS;
  gBal = n;
  left.setBalanceAttDb(attenuationDb(n, false));
  right.setBalanceAttDb(attenuationDb(n, true));
}
} // namespace Balance
