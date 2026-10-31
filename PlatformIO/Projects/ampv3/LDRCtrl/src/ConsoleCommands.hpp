#pragma once
#include <Arduino.h>
#include "VerifyHistory.hpp"
#include "Console.hpp"
#include "Board.hpp"
#include "DriverChannels.hpp"
#include "LDRVolume.hpp"

// ============================================================================
// ConsoleCommands -- console mode, entered with DEBUG (left with EXIT) on either
// link. Deliberately small:
// The commands are the table kConsole in ConsoleCommands.cpp (one row each, with its HELP lines);
// HELP prints them. Concepts (characterization, resync, re-solve, learning, verify, report):
// ldr-attenuator-console.md.
// ============================================================================
class ConsoleCommands {
public:
  ConsoleCommands(Stream &stream, Board &board, DriverChannels &driver,
                LDRVolume &leftChannel, LDRVolume &rightChannel)
      : stream_(stream), board_(board), driver_(driver), left_(leftChannel), right_(rightChannel) {}

  void processLine(const String &line);
  // VERIFY for the protocol: the same run with its text going nowhere. verifyRunning(): one is
  // running (STATUS then says BUSY with the calibration still OK).
  void runVerifyQuiet();
  static bool verifyRunning();
  // IMPORT in progress: the link then passes lines unechoed and untouched.
  bool importing() const { return importing_; }
  void cancelImport();

private:
  static constexpr int MAX_TOK = 4;

  Stream &stream_;
  Board &board_;
  DriverChannels &driver_;
  LDRVolume &left_;
  LDRVolume &right_;

  void handleLine(String line);

  // The debug console's command table (ConsoleCommands.cpp): first word -> handler. Each handler
  // gets the line's words (tok[0..n-1]), the upper-cased first word (cmd).
  struct Args { String tok[MAX_TOK]; int n = 0; String cmd; };
  // name (the first word), handler, HELP lines ('\n'-separated; nullptr = alias, listed elsewhere)
  struct ConsoleCommand { const char *name; void (ConsoleCommands::*fn)(Args &a); const char *help; };
  static const ConsoleCommand kConsole[];
  void cmdHelp(Args &a);  void cmdCal(Args &a);   void cmdForget(Args &a); void cmdVol(Args &a);
  void cmdComp(Args &a);  void cmdLearn(Args &a); void cmdTemp(Args &a);   void cmdMutes(Args &a);
  void cmdSet(Args &a);   void cmdBal(Args &a);   void cmdModel(Args &a);  void cmdVer(Args &a);
  void cmdRead(Args &a);  void cmdVerify(Args &a); void cmdDump(Args &a);  void cmdReport(Args &a);
  void cmdSettings(Args &a);  void cmdExport(Args &a);  void cmdImport(Args &a);
  void buildVerifyRecord(VerifyHistory::Record &r);
  void printVerifyChanges(Console &out, const VerifyHistory::Record &a, const VerifyHistory::Record &b);
  void verifyHistory();
  void runVerify();
  bool importing_ = false;
  uint32_t importLastMs_ = 0;
  void importLine(const String &line);
  void printHelp();

  // VERIFY / DUMP sections
  void dumpSnapshot();
  void verifyCells();
  void verifySteps();
  void verifyWalk();
  void verifyCreep(uint16_t quietSeconds);
  void printState();
  void printStored(LDRVolume &v, const char *label);
  void printCurve(const char *label, const LdrCurve &curve);
  void printLut(const VolumeLut &lut);
  void printVolumeDetail(LDRVolume &v, const char *label);
  void printModel();
  void printVolumeTable();

  // The last VERIFY's measurements live in Results (shared by both serial links).

  // Plain-language findings from the stored calibration and the last verify, each naming
  // the element (L/R series/shunt) and what to do next. End of VOL INFO and VERIFY.
  void printHealth();
};
