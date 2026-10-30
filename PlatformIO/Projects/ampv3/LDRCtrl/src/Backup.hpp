#pragma once
// EXPORT / IMPORT: a backup of the attenuator's files as one text block over the console.
//
//   ===== EXPORT BEGIN ldr-attenuator 1 =====
//   # comments (firmware, groups) are ignored
//   FILE /settings.txt TEXT 74 crc=1a2b3c4d     each text line follows, prefixed with '|'
//   |source_ohms=300                            (so blank lines and spaces survive the link)
//   FILE /cal_left.bin BASE64 2132 crc=89abcdef base64, 76 characters per line
//   AAECAwQF...
//   ===== EXPORT END 8 files =====
//
// TEXT is used only when the file restores byte-exactly that way (printable characters and
// tabs, '\n' endings, a final newline); anything else goes as BASE64. size and crc (CRC-32,
// IEEE) are of the file's bytes, so IMPORT verifies every file before writing any.
// Pure (no Arduino): the emitter and the parser are host-tested with a round trip.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Backup {

constexpr const char *BEGIN_LINE = "===== EXPORT BEGIN ldr-attenuator 1 =====";
constexpr const char *END_PREFIX = "===== EXPORT END";
constexpr uint8_t B64_PER_LINE = 57;                 // bytes per base64 line (-> 76 characters)

// ---- CRC-32 (IEEE 802.3, reflected, as zlib) ----
inline uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n) {
  crc = ~crc;
  while (n--) { crc ^= *p++; for (uint8_t k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
  return ~crc;
}

// ---- base64 ----
inline size_t b64encode(const uint8_t *in, size_t n, char *out) {      // out: 4*ceil(n/3)+1
  static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t k = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[k++] = T[v >> 18 & 63]; out[k++] = T[v >> 12 & 63];
    out[k++] = i + 1 < n ? T[v >> 6 & 63] : '=';
    out[k++] = i + 2 < n ? T[v & 63] : '=';
  }
  out[k] = '\0';
  return k;
}
inline int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
// One base64 line -> bytes. Returns the byte count, or -1 if the line is malformed.
inline int b64decodeLine(const char *s, uint8_t *out, size_t cap) {
  size_t len = strlen(s);
  while (len && (s[len - 1] == ' ' || s[len - 1] == '\r')) len--;
  if (len % 4) return -1;
  size_t k = 0;
  for (size_t i = 0; i < len; i += 4) {
    const int a = b64val(s[i]), b = b64val(s[i + 1]);
    const bool p2 = s[i + 2] == '=', p3 = s[i + 3] == '=';
    const int c = p2 ? 0 : b64val(s[i + 2]), d = p3 ? 0 : b64val(s[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 || (p2 && !p3) || ((p2 || p3) && i + 4 != len)) return -1;
    const uint32_t v = (uint32_t)a << 18 | (uint32_t)b << 12 | (uint32_t)c << 6 | (uint32_t)d;
    const size_t m = p2 ? 1 : p3 ? 2 : 3;
    if (k + m > cap) return -1;
    out[k++] = (uint8_t)(v >> 16);
    if (m > 1) out[k++] = (uint8_t)(v >> 8);
    if (m > 2) out[k++] = (uint8_t)v;
  }
  return (int)k;
}

// ---- which files a backup may contain (IMPORT writes nothing else) ----
inline bool allowedPath(const char *p) {
  static const char *ok[] = {"/settings.txt", "/cal_left.bin", "/cal_right.bin", "/model.txt", "/lastmatch.txt",
                             "/rail_ref.txt", "/thermal_state.txt", "/thermal_log.csv"};
  for (const char *o : ok) if (strcmp(o, p) == 0) return true;
  return false;
}

// Whether a file can go as TEXT and come back byte-exactly.
inline bool textSafe(const uint8_t *d, size_t n) {
  if (n == 0) return true;
  if (d[n - 1] != '\n') return false;
  for (size_t i = 0; i < n; i++) {
    const uint8_t c = d[i];
    if (c == '\n' || c == '\t') continue;
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

// ---- emitter: the lines of one file, to a callback (line without '\n') ----
template <typename EmitLine>
void emitFile(const char *path, const uint8_t *d, size_t n, EmitLine emit) {
  char line[96];
  const bool text = textSafe(d, n);
  snprintf(line, sizeof line, "FILE %s %s %lu crc=%08lx", path, text ? "TEXT" : "BASE64", (unsigned long)n,
           (unsigned long)crc32(0, d, n));
  emit(line);
  if (text) {
    size_t i = 0;
    while (i < n) {
      const uint8_t *e = (const uint8_t *)memchr(d + i, '\n', n - i);
      const size_t len = (size_t)(e - (d + i));
      char buf[200];
      buf[0] = '|';
      const size_t m = len < sizeof buf - 2 ? len : sizeof buf - 2;   // text files here have short lines
      memcpy(buf + 1, d + i, m); buf[m + 1] = '\0';
      emit(buf);
      i += len + 1;
    }
  } else {
    for (size_t i = 0; i < n; i += B64_PER_LINE) {
      b64encode(d + i, (n - i) < B64_PER_LINE ? (n - i) : B64_PER_LINE, line);
      emit(line);
    }
  }
}

// ---- parser: IMPORT feeds it every received line; it rebuilds the files in an arena ----
struct FileEntry { char path[24]; uint32_t off, size, crc; bool text; };

class Parser {
public:
  static constexpr uint8_t MAX_FILES = 8;
  enum class State : uint8_t { WAIT_BEGIN, IN_BLOCK, DONE, FAILED };

  Parser(uint8_t *arena, uint32_t cap) : arena_(arena), cap_(cap) {}

  // One line (without '\r'/'\n'; NOT trimmed). Lines before BEGIN are ignored.
  void feed(const char *line) {
    if (state_ == State::DONE || state_ == State::FAILED) return;
    if (state_ == State::WAIT_BEGIN) { if (strcmp(trimmed(line), BEGIN_LINE) == 0) state_ = State::IN_BLOCK; return; }
    if (strncmp(line, END_PREFIX, strlen(END_PREFIX)) == 0) { if (closeFile()) state_ = State::DONE; return; }
    if (strncmp(line, "FILE ", 5) == 0) { if (closeFile()) openFile(line + 5); return; }
    if (!open_) { if (line[0] == '#' || trimmed(line)[0] == '\0') return; fail("content outside a FILE"); return; }
    FileEntry &f = files_[n_ - 1];
    if (f.text) {
      if (line[0] != '|') { fail("text line without '|'"); return; }
      const size_t len = strlen(line + 1);
      if (used_ + len + 1 > cap_) { fail("backup larger than the import buffer"); return; }
      memcpy(arena_ + used_, line + 1, len); used_ += (uint32_t)len;
      arena_[used_++] = '\n';
    } else {
      const int m = b64decodeLine(trimmed(line), arena_ + used_, cap_ - used_);
      if (m < 0) { fail("malformed base64 line"); return; }
      used_ += (uint32_t)m;
    }
  }

  State state() const { return state_; }
  const char *error() const { return err_; }
  uint8_t fileCount() const { return n_; }
  const FileEntry &file(uint8_t i) const { return files_[i]; }
  const uint8_t *data(const FileEntry &f) const { return arena_ + f.off; }

private:
  static const char *trimmed(const char *s) { while (*s == ' ') s++; return s; }
  void fail(const char *why) { if (state_ != State::FAILED) { snprintf(err_, sizeof err_, "%s", why); state_ = State::FAILED; } }

  void openFile(const char *h) {            // "<path> <TEXT|BASE64> <size> crc=<hex>"
    if (n_ == MAX_FILES) { fail("too many files"); return; }
    FileEntry &f = files_[n_];
    char kind[8]; unsigned long size = 0, crc = 0;
    if (sscanf(h, "%23s %7s %lu crc=%lx", f.path, kind, &size, &crc) != 4) { fail("malformed FILE line"); return; }
    if (!allowedPath(f.path)) { snprintf(err_, sizeof err_, "file not allowed: %s", f.path); state_ = State::FAILED; return; }
    for (uint8_t i = 0; i < n_; i++) if (strcmp(files_[i].path, f.path) == 0) { fail("file listed twice"); return; }
    if (strcmp(kind, "TEXT") == 0) f.text = true;
    else if (strcmp(kind, "BASE64") == 0) f.text = false;
    else { fail("unknown FILE kind"); return; }
    f.off = used_; f.size = (uint32_t)size; f.crc = (uint32_t)crc;
    n_++; open_ = true;
  }
  bool closeFile() {                         // check the file just completed (if any)
    if (state_ == State::FAILED) return false;
    if (!open_) return true;
    open_ = false;
    const FileEntry &f = files_[n_ - 1];
    if (used_ - f.off != f.size) { snprintf(err_, sizeof err_, "%s: %lu bytes, expected %lu", f.path, (unsigned long)(used_ - f.off), (unsigned long)f.size); state_ = State::FAILED; return false; }
    if (crc32(0, arena_ + f.off, f.size) != f.crc) { snprintf(err_, sizeof err_, "%s: checksum mismatch", f.path); state_ = State::FAILED; return false; }
    return true;
  }

  uint8_t *arena_;
  uint32_t cap_, used_ = 0;
  State state_ = State::WAIT_BEGIN;
  FileEntry files_[MAX_FILES];
  uint8_t n_ = 0;
  bool open_ = false;
  char err_[72] = "";
};

} // namespace Backup
