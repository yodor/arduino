#pragma once
// key=value text for settings.txt: one setting per line, '#' starts a comment, blank lines
// ignored, whitespace around key and value trimmed. Unknown keys are KEPT (a newer firmware's
// settings survive an older one), order is preserved. Pure, host-tested.
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace SettingsText {

constexpr uint8_t MAX_KEYS = 24;
constexpr uint8_t KEY_LEN = 32;
constexpr uint8_t VAL_LEN = 48;

struct Store {
  char key[MAX_KEYS][KEY_LEN];
  char val[MAX_KEYS][VAL_LEN];
  uint8_t n = 0;

  const char *get(const char *k) const {
    for (uint8_t i = 0; i < n; i++) if (strcmp(key[i], k) == 0) return val[i];
    return nullptr;
  }
  bool set(const char *k, const char *v) {
    for (uint8_t i = 0; i < n; i++) if (strcmp(key[i], k) == 0) { copy(val[i], v, VAL_LEN); return true; }
    if (n == MAX_KEYS || !*k) return false;
    copy(key[n], k, KEY_LEN); copy(val[n], v, VAL_LEN); n++;
    return true;
  }
  static void copy(char *dst, const char *src, size_t cap) { strncpy(dst, src, cap - 1); dst[cap - 1] = '\0'; }
};

// Parse text into the store (keys already present are updated). Returns the number of settings read.
inline uint8_t parse(const char *text, Store &s) {
  uint8_t got = 0;
  while (*text) {
    const char *eol = strchr(text, '\n');
    const size_t len = eol ? (size_t)(eol - text) : strlen(text);
    char line[KEY_LEN + VAL_LEN + 16];
    const size_t m = len < sizeof line - 1 ? len : sizeof line - 1;
    memcpy(line, text, m); line[m] = '\0';
    text += len + (eol ? 1 : 0);
    char *hash = strchr(line, '#'); if (hash) *hash = '\0';
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char *k = line, *v = eq + 1;
    auto trim = [](char *p) -> char * {
      while (*p == ' ' || *p == '\t') p++;
      char *e = p + strlen(p);
      while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
      return p;
    };
    k = trim(k); v = trim(v);
    if (*k && s.set(k, v)) got++;
  }
  return got;
}

// The store as text, with a header comment. Returns the length written (truncated at cap - 1).
inline size_t format(const Store &s, char *out, size_t cap, const char *header) {
  size_t k = 0;
  if (header) k += (size_t)snprintf(out + k, cap - k, "# %s\n", header);
  for (uint8_t i = 0; i < s.n && k < cap; i++) k += (size_t)snprintf(out + k, cap - k, "%s=%s\n", s.key[i], s.val[i]);
  return k < cap ? k : cap - 1;
}

} // namespace SettingsText
