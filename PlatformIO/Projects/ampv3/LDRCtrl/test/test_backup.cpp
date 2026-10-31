// Host test: backup. Run all with: python3 test/run_tests.py
// EXPORT -> IMPORT round trip (byte-exact, text and base64), and every way a paste can go wrong
// must be refused before anything is written.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "Backup.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace Backup;

struct F { const char *path; std::vector<uint8_t> d; };
static std::vector<std::string> exportBlock(const std::vector<F> &files) {
  std::vector<std::string> L;
  L.push_back(BEGIN_LINE);
  L.push_back("# FW test, EXPORT ALL");
  for (auto &f : files) emitFile(f.path, f.d.data(), f.d.size(), [&](const char *s){ L.push_back(s); });
  L.push_back(std::string(END_PREFIX) + " " + std::to_string(files.size()) + " files =====");
  return L;
}
static Parser::State importLines(const std::vector<std::string> &L, Parser &p) { for (auto &l : L) p.feed(l.c_str()); return p.state(); }
static std::vector<uint8_t> bytes(const char *s) { return std::vector<uint8_t>(s, s + strlen(s)); }

int main(){
  // known CRC-32 and base64 vectors
  CHECK(crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
  char b[16]; b64encode((const uint8_t *)"Man", 3, b); CHECK(strcmp(b, "TWFu") == 0);
  b64encode((const uint8_t *)"Ma", 2, b); CHECK(strcmp(b, "TWE=") == 0);
  uint8_t o[8]; CHECK(b64decodeLine("TWE=", o, 8) == 2 && o[0] == 'M' && o[1] == 'a');
  CHECK(b64decodeLine("TW=E", o, 8) < 0 && b64decodeLine("TWE", o, 8) < 0 && b64decodeLine("T!E=", o, 8) < 0);

  std::vector<uint8_t> bin; for (int r = 0; r < 9; r++) for (int i = 0; i < 256; i++) bin.push_back((uint8_t)i);  // 2304 bytes
  std::vector<F> files = {
    {"/settings.txt", bytes("# LDR attenuator settings\nsource_ohms=300\n\n  load_ohms = 10000  \nauto_pref=DEEP\n")},
    {"/thermal_log.csv", bytes("0.120,-0.080,0.450,0.0100,27.910,3.3290,50350\n-0.300,-0.250,-0.900,0.0000,26.950,3.3286,61200\n")},
    {"/cal_left.bin", bin},
    {"/lastmatch.txt", bytes("0.03")},            // no final newline -> must go as BASE64 to restore exactly
  };
  auto L = exportBlock(files);
  int textFiles = 0; for (auto &l : L) if (l.find(" TEXT ") != std::string::npos) textFiles++;
  static uint8_t arena[32768];
  { Parser p(arena, sizeof arena);
    std::vector<std::string> withJunk = {"IMPORT", "OK paste the block", ""};
    withJunk.insert(withJunk.end(), L.begin(), L.end());
    CHECK(importLines(withJunk, p) == Parser::State::DONE);
    CHECK(p.fileCount() == files.size());
    for (uint8_t i = 0; i < p.fileCount(); i++) {
      const FileEntry &f = p.file(i);
      CHECK(strcmp(f.path, files[i].path) == 0 && f.size == files[i].d.size() && memcmp(p.data(f), files[i].d.data(), f.size) == 0);
    }
    printf("  round trip: %zu files (%d as TEXT, %zu as BASE64), %zu lines, byte-exact\n", files.size(), textFiles, files.size() - textFiles, L.size());
  }
  // failures: each must end FAILED (nothing would be written)
  struct Case { const char *what; std::vector<std::string> lines; };
  std::vector<Case> cases;
  { auto c = L; for (auto &l : c) if (l.rfind("|source_ohms", 0) == 0) l = "|source_ohms=301"; cases.push_back({"one changed character (text)", c}); }
  { auto c = L; for (auto &l : c) if (l.size() == 76 && l[0] != '|' && l[0] != 'F') { l[10] = (l[10] == 'A' ? 'B' : 'A'); break; } cases.push_back({"one changed character (base64)", c}); }
  { auto c = L; c.erase(c.begin() + c.size() / 2, c.end()); cases.push_back({"truncated paste (no END)", c}); }
  { auto c = L; c.insert(c.begin() + 2, "FILE /etc/passwd TEXT 1 crc=00000000"); cases.push_back({"file outside the allowed list", c}); }
  { auto c = L; for (auto &l : c) if (l.size() == 76 && l[0] != '|') { l[5] = '!'; break; } cases.push_back({"malformed base64", c}); }
  { auto c = L; for (auto &l : c) if (l.rfind("|auto_pref", 0) == 0) l = "auto_pref=DEEP"; cases.push_back({"text line without its '|'", c}); }
  for (auto &k : cases) {
    Parser p(arena, sizeof arena);
    const Parser::State s = importLines(k.lines, p);
    printf("  %-34s -> %s%s%s\n", k.what, s == Parser::State::FAILED ? "refused: " : (s == Parser::State::DONE ? "ACCEPTED" : "not complete (nothing written)"), p.error(), "");
    CHECK(s != Parser::State::DONE);
  }
  // a buffer too small is refused, not overrun
  { static uint8_t tiny[512]; Parser p(tiny, sizeof tiny); CHECK(importLines(L, p) == Parser::State::FAILED); }
  printf("backup host test: PASS\n"); return 0; }
