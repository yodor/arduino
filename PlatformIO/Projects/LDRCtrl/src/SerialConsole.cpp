#include "SerialConsole.hpp"
#include "Config.hpp"
#include "DualCalibration.hpp"
#include "Board.hpp"
#include "BusyHook.hpp"

namespace {
// "VOL=8 (-40.04 dB)" -- the same number the strict master protocol uses
// (0-based, 0 = quietest), plus the attenuation that step is solved for.
void describeVolume(Stream &out, LDRVolume &v) {
  if (!v.hasLut() || v.currentStep() >= v.numSteps()) {
    out.print(F("VOL=? (no calibration)"));
    return;
  }
  out.print(F("VOL="));
  out.print(v.currentStep());
  out.print(F(" ("));
  out.print(v.lut().step(v.currentStep()).targetDb, 2);
  out.print(F(" dB)"));
  if (v.isMuted()) out.print(F(" [muted]"));
}
} // namespace

void SerialConsole::begin() {
  stream_.println();
  stream_.println(F("=== LDR driver board bring-up test ==="));
  for (uint8_t i = 0; i < DriverChannels::NUM_CHANNELS; i++) {
    auto ch = static_cast<DriverChannels::Channel>(i);
    stream_.print(F("  "));
    stream_.print(DriverChannels::name(ch));
    stream_.print(F(" -> GPIO"));
    stream_.println(DriverChannels::pinFor(ch));
  }
  stream_.println(F("All channels start OFF (duty=0). Both relays start de-energized (audio mode)."));
  printHelp();
}

void SerialConsole::poll() {
  while (stream_.available()) {
    char c = stream_.read();
    if (c == '\r' || c == '\n') {
      if (lineBuf_.length() > 0) {
        stream_.println();
        handleLine(lineBuf_);
        lineBuf_ = "";
      }
    } else {
      stream_.write(c); // local echo
      lineBuf_ += c;
    }
  }
}

void SerialConsole::printHelp() {
  stream_.println(F("--------------------------------------------------------"));
  stream_.println(F("Driver test:"));
  stream_.println(F("  SET <ch> <duty>      raw duty (0-4095) on one channel"));
  stream_.println(F("  PCT <ch> <percent>   duty by percent on one channel"));
  stream_.println(F("  ALL <duty>           raw duty on all 4 channels"));
  stream_.println(F("  ALLPCT <percent>     duty by percent on all 4 channels"));
  stream_.println(F("  SWEEP <ch> <start%> <end%> <step%> <dwell_ms>"));
  stream_.println(F("  STATUS               current duty/relay/volume state of both sides"));
  stream_.println(F("  TEMP | UPTIME        RP2040 die temperature (proxy for board temperature)"));
  stream_.println(F("                       and time since power-up"));
  stream_.println(F("  OFF                  all channels to 0"));
  stream_.println(F("<ch> is SHUNT_L, SER_L, SHUNT_R, SER_R, or 0-3."));
  stream_.println();
  stream_.println(F("[L|R] is optional on every command below -- omit it to apply to BOTH channels."));
  stream_.println();
  stream_.println(F("Relay:"));
  stream_.println(F("  RELAY [L|R] <ON|OFF> energize/de-energize a board's calibration relay directly"));
  stream_.println(F("  AMP_MUTE [ON|OFF]    get/set the amp's own mute (4N25 opto) -- independent of"));
  stream_.println(F("                       any channel's own MUTE; OFF refused during the boot hold"));
  stream_.println(F("  DIAGLED <ON|OFF>     steady on/off for the diagnostic LED (no blinking --"));
  stream_.println(F("                       GPIO toggling was audible as noise on real hardware)"));
  stream_.println();
  stream_.println(F("Calibration (CALAUTO is the normal path; CALDRIVE/CALPOINT/CALSOLVE are for"));
  stream_.println(F("manual point-by-point work, e.g. with a bench DMM instead of the ADS1115):"));
  stream_.println(F("  CALSTART [L|R]                energize relay, clear curves"));
  stream_.println(F("  CALEND [L|R]                  de-energize relay"));
  stream_.println(F("  CALRESET [L|R]                clear curves + LUT, de-energize"));
  stream_.println(F("  CALDRIVE [L|R] <SER|SHUNT> <duty>"));
  stream_.println(F("                                drive a duty, leave it, so you can measure"));
  stream_.println(F("  CALPOINT [L|R] <SER|SHUNT> <duty> <ohms>"));
  stream_.println(F("                                record a characterization point"));
  stream_.println(F("  CALSOLVE [L|R] [steps] [rTotalOhms]"));
  stream_.println(F("                                solve the LUT from curves so far"));
  stream_.println(F("  CALDUMP [L|R]                 print curves + solved LUT"));
  stream_.println(F("  CALTRIM [L|R]                 verify+trim every LUT step against a live"));
  stream_.println(F("                                measurement, re-saves when done"));
  stream_.println(F("  CALAUTO [L|R] <FULL|FAST>     FULL: sweep + knee refine + solve + trim."));
  stream_.println(F("                                FAST: trim-only drift touch-up of the saved"));
  stream_.println(F("                                calibration (no sweep)"));
  stream_.println(F("                                both channels (no L|R) run INTERLEAVED,"));
  stream_.println(F("                                roughly halving total sweep time"));
  stream_.println(F("                                dense sweep=~29pt, fast=8pt drift touch-up"));
  stream_.println(F("                                auto-saves to flash, returns to audio mode"));
  stream_.println(F("                                skips (no-op) if no ADS1115 is detected"));
  stream_.println(F("  CALSCAN [L|R] [start end]     diagnostic: fine 2-way scan of a raw-duty window"));
  stream_.println(F("                                (default 900-1148), 5 raw samples/point -- does"));
  stream_.println(F("                                NOT touch saved curves/LUT, paste output back"));
  stream_.println(F("  CALREF [L|R] [ohms]           get/set Rref used for this channel's readings"));
  stream_.println(F("  CALRTOTAL [L|R] [ohms]        get/set Rs+Rsh target (default from Config.hpp)"));
  stream_.println(F("  CALMODE [L|R] [RTOTAL|FIXEDSERIES]  get/set attenuation mode"));
  stream_.println(F("  CALSERIESDUTY [L|R] [duty]    get/set fixed series duty (FIXEDSERIES mode only)"));
  stream_.println(F("                                (affects future reads only; re-run CALAUTO after)"));
  stream_.println(F("  CALSAVE [L|R]                 manually save current curves+LUT to flash"));
  stream_.println(F("  CALLOAD [L|R]                 manually (re)load from flash"));
  stream_.println();
  stream_.println(F("Volume (uses the solved LUT):"));
  stream_.println(F("  VOL [L|R] [<step>]    apply LUT step (0 = quietest, same numbering as the"));
  stream_.println(F("                        master's VOL/GET VOL); jumps of more than one step"));
  stream_.println(F("                        auto-ramp. With no step: show the current volume"));
  stream_.println(F("  VOLUP [L|R]           one step louder"));
  stream_.println(F("  VOLDOWN [L|R]         one step quieter"));
  stream_.println(F("  MUTE [L|R] <ON|OFF>   fast mute / unmute (works even before calibration)"));
  stream_.println();
  stream_.println(F("ADS1115 (i2c bus is deactivated except while that side's relay is energized):"));
  stream_.println(F("  I2CSCAN [L|R]          scan for ACKing addresses (works regardless of relay)"));
  stream_.println(F("  ADCREAD [L|R]          read Vref/Rs/Rsh -- requires relay ON first"));
  stream_.println(F("--------------------------------------------------------"));
}

void SerialConsole::printStatus() {
  for (uint8_t i = 0; i < DriverChannels::NUM_CHANNELS; i++) {
    auto ch = static_cast<DriverChannels::Channel>(i);
    uint16_t duty = driver_.getDuty(ch);
    float pct = (duty * 100.0f) / DriverChannels::WRAP;
    stream_.print(DriverChannels::name(ch));
    stream_.print(F(" (GPIO"));
    stream_.print(DriverChannels::pinFor(ch));
    stream_.print(F("): duty="));
    stream_.print(duty);
    stream_.print('/');
    stream_.print(DriverChannels::WRAP);
    stream_.print(F(" ("));
    stream_.print(pct, 1);
    stream_.println(F("%)"));
  }

  LDRVolume *sides[2] = {&left_, &right_};
  const char *labels[2] = {"L", "R"};
  for (int s = 0; s < 2; s++) {
    LDRVolume &v = *sides[s];
    stream_.print(F("Side "));
    stream_.print(labels[s]);
    stream_.print(F(": relay="));
    stream_.print(v.relayEnergized() ? F("ENERGIZED(cal)") : F("audio"));
    stream_.print(F("  muted="));
    stream_.print(v.isMuted() ? F("yes") : F("no"));
    stream_.print(F("  LUT="));
    if (v.hasLut()) {
      stream_.print(v.numSteps());
      stream_.print(F(" steps, step="));
      stream_.print(v.currentStep());
      const VolumeLut::Entry &e = v.lut().step(v.currentStep());
      stream_.print(F("  Rs="));
      stream_.print(e.targetRs, 1);
      stream_.print(F(" ohm  Rsh="));
      stream_.print(e.targetRsh, 1);
      stream_.print(F(" ohm"));
      if (!e.valid) stream_.print(F("  (OUT OF RANGE)"));
      stream_.print(F("  depth="));
      stream_.print(v.rangeDb(), 2);
      stream_.print(F("dB  Rtotal="));
      stream_.print((long)v.rTotalOhms());
    } else {
      stream_.print(F("none"));
    }
    stream_.println();
  }

  stream_.print(F("Volume: "));
  if (left_.currentStep() == right_.currentStep() && left_.isMuted() == right_.isMuted()) {
    describeVolume(stream_, left_);
  } else {
    stream_.print(F("L "));
    describeVolume(stream_, left_);
    stream_.print(F("   R "));
    describeVolume(stream_, right_);
  }
  stream_.println();
  Board::reportDieTemp(stream_, F("Die temp"));
}

void SerialConsole::doSweep(DriverChannels::Channel ch, int startPct, int endPct, int stepPct, unsigned long dwellMs) {
  BusyHook::Scope busy;
  if (stepPct == 0) { stream_.println(F("ERR step must be nonzero")); return; }
  stream_.print(F("Sweeping "));
  stream_.print(DriverChannels::name(ch));
  stream_.print(F(" from "));
  stream_.print(startPct);
  stream_.print(F("% to "));
  stream_.print(endPct);
  stream_.print(F("%, step "));
  stream_.print(stepPct);
  stream_.println(F("%."));

  bool increasing = endPct >= startPct;
  int step = increasing ? abs(stepPct) : -abs(stepPct);

  for (int pct = startPct; increasing ? (pct <= endPct) : (pct >= endPct); pct += step) {
    uint16_t duty = (uint16_t)((pct * (long)DriverChannels::WRAP) / 100);
    driver_.setDuty(ch, duty);
    stream_.print(F("  duty="));
    stream_.print(pct);
    stream_.print(F("%  (raw="));
    stream_.print(duty);
    stream_.println(F(")  <- read multimeter now"));

    BusyHook::wait(dwellMs); // services the link while it waits
  }
  stream_.println(F("Sweep complete."));
}

void SerialConsole::printCurve(const char *label, const LdrCurve &curve) {
  stream_.print(label);
  stream_.print(F(" curve ("));
  stream_.print(curve.count());
  stream_.println(F(" points):"));
  for (uint8_t i = 0; i < curve.count(); i++) {
    const auto &p = curve.point(i);
    stream_.print(F("  duty="));
    stream_.print(p.duty);
    stream_.print(F("  ohms="));
    stream_.println(p.ohms, 1);
  }
}

void SerialConsole::printLut(const VolumeLut &lut) {
  stream_.print(F("LUT ("));
  stream_.print(lut.numSteps());
  stream_.println(F(" steps):"));
  for (uint8_t i = 0; i < lut.numSteps(); i++) {
    const auto &e = lut.step(i);
    stream_.print(F("  step "));
    stream_.print(i);
    stream_.print(F(": "));
    stream_.print(e.targetDb, 2);
    stream_.print(F("dB  Rs="));
    stream_.print(e.targetRs, 0);
    stream_.print(F(" Rsh="));
    stream_.print(e.targetRsh, 0);
    stream_.print(F("  seriesDuty="));
    stream_.print(e.seriesDuty);
    stream_.print(F(" shuntDuty="));
    stream_.print(e.shuntDuty);
    stream_.println(e.valid ? F("  OK") : F("  OUT OF RANGE"));
  }
}

void SerialConsole::handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  String tok[MAX_TOK];
  int n = 0, start = 0;
  while (n < MAX_TOK) {
    int sp = line.indexOf(' ', start);
    if (sp < 0) { tok[n++] = line.substring(start); break; }
    tok[n++] = line.substring(start, sp);
    start = sp + 1;
    while (start < (int)line.length() && line[start] == ' ') start++;
  }

  String cmd = tok[0];
  cmd.toUpperCase();

  if (cmd == "HELP" || cmd == "?") {
    printHelp();

  } else if (cmd == "STATUS") {
    printStatus();

  } else if (cmd == "TEMP" || cmd == "UPTIME") {
    Board::reportDieTemp(stream_, F("Die temp"));

  } else if (cmd == "OFF") {
    driver_.setDutyAll(0);
    stream_.println(F("OK all channels off"));

  } else if (cmd == "SET" && n >= 3) {
    int ch = DriverChannels::find(tok[1]);
    if (ch < 0) { stream_.println(F("ERR unknown channel")); return; }
    driver_.setDuty(static_cast<DriverChannels::Channel>(ch), tok[2].toInt());
    stream_.println(F("OK"));

  } else if (cmd == "PCT" && n >= 3) {
    int ch = DriverChannels::find(tok[1]);
    if (ch < 0) { stream_.println(F("ERR unknown channel")); return; }
    int pct = constrain(tok[2].toInt(), 0, 100);
    driver_.setDuty(static_cast<DriverChannels::Channel>(ch), (uint16_t)((pct * (long)DriverChannels::WRAP) / 100));
    stream_.println(F("OK"));

  } else if (cmd == "ALL" && n >= 2) {
    driver_.setDutyAll(tok[1].toInt());
    stream_.println(F("OK"));

  } else if (cmd == "ALLPCT" && n >= 2) {
    int pct = constrain(tok[1].toInt(), 0, 100);
    driver_.setDutyAll((uint16_t)((pct * (long)DriverChannels::WRAP) / 100));
    stream_.println(F("OK"));

  } else if (cmd == "SWEEP" && n >= 6) {
    int ch = DriverChannels::find(tok[1]);
    if (ch < 0) { stream_.println(F("ERR unknown channel")); return; }
    doSweep(static_cast<DriverChannels::Channel>(ch), tok[2].toInt(), tok[3].toInt(), tok[4].toInt(), (unsigned long)tok[5].toInt());

  } else if (cmd == "DIAGLED" && n >= 2) {
    bool on = tok[1].equalsIgnoreCase("ON");
    bool off = tok[1].equalsIgnoreCase("OFF");
    if (!on && !off) { stream_.println(F("ERR expected ON or OFF")); return; }
    board_.setDiagLedEnabled(on);
    stream_.println(F("OK"));

  } else if (cmd == "AMP_MUTE") {
    if (n >= 2) {
      bool on = tok[1].equalsIgnoreCase("ON");
      bool off = tok[1].equalsIgnoreCase("OFF");
      if (!on && !off) {
        stream_.println(F("ERR expected ON or OFF"));
      } else if (board_.setAmpMute(on)) {
        stream_.println(F("OK"));
      } else {
        stream_.print(F("ERR boot mute hold active, "));
        stream_.print(board_.bootHoldRemainingMs());
        stream_.println(F("ms remaining"));
      }
    } else {
      stream_.println(board_.ampMuted() ? F("AMP_MUTE=1") : F("AMP_MUTE=0"));
    }

  } else if (cmd == "RELAY") {
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n < sel.argBase + 1) { stream_.println(F("ERR expected ON or OFF")); return; }
    bool on = tok[sel.argBase].equalsIgnoreCase("ON");
    bool off = tok[sel.argBase].equalsIgnoreCase("OFF");
    if (!on && !off) { stream_.println(F("ERR expected ON or OFF")); return; }
    for (uint8_t i = 0; i < sel.count; i++) sel.items[i]->relayEnergize(on);
    stream_.println(F("OK"));

  } else if (cmd == "CALSTART") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) sel.items[i]->calBegin();
    stream_.println(F("OK relay energized, curves cleared"));

  } else if (cmd == "CALEND") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) sel.items[i]->calEnd();
    stream_.println(F("OK relay de-energized"));

  } else if (cmd == "CALRESET") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) sel.items[i]->calReset();
    stream_.println(F("OK curves and LUT cleared"));

  } else if (cmd == "CALDRIVE") {
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n < sel.argBase + 2) { stream_.println(F("ERR expected SER|SHUNT and a duty")); return; }
    bool isSer = tok[sel.argBase].equalsIgnoreCase("SER");
    bool isShunt = tok[sel.argBase].equalsIgnoreCase("SHUNT");
    if (!isSer && !isShunt) { stream_.println(F("ERR expected SER or SHUNT")); return; }
    uint16_t duty = (uint16_t)tok[sel.argBase + 1].toInt();
    for (uint8_t i = 0; i < sel.count; i++) {
      if (isSer) sel.items[i]->calDriveSeries(duty); else sel.items[i]->calDriveShunt(duty);
    }
    stream_.print(F("OK duty="));
    stream_.print(duty);
    stream_.println(F("  <- measure now, then CALPOINT with this same duty"));

  } else if (cmd == "CALPOINT") {
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n < sel.argBase + 3) { stream_.println(F("ERR expected SER|SHUNT, duty, ohms")); return; }
    bool isSer = tok[sel.argBase].equalsIgnoreCase("SER");
    bool isShunt = tok[sel.argBase].equalsIgnoreCase("SHUNT");
    if (!isSer && !isShunt) { stream_.println(F("ERR expected SER or SHUNT")); return; }
    uint16_t duty = (uint16_t)tok[sel.argBase + 1].toInt();
    float ohms = tok[sel.argBase + 2].toFloat();
    for (uint8_t i = 0; i < sel.count; i++) {
      bool ok = isSer ? sel.items[i]->calFeedSeriesPoint(duty, ohms) : sel.items[i]->calFeedShuntPoint(duty, ohms);
      if (sel.count == 2) { stream_.print(sideLabel(sel.items[i])); stream_.print(F(": ")); }
      stream_.println(ok ? F("OK point added") : F("ERR duty must be strictly greater than the last point added"));
    }

  } else if (cmd == "CALSOLVE") {
    // CALSOLVE [L|R] [steps] [rTotalOhms]. Re-solves from the existing
    // curves (no sweep); the range is always computed. With BOTH channels
    // (no L|R) the pair shares one common range so L and R match; solving
    // a single side alone uses that side's own range.
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n > sel.argBase + 2) {
      stream_.println(F("ERR CALSOLVE takes [steps] [rTotalOhms] -- the range is no longer an argument (it is always computed)"));
      return;
    }
    uint8_t steps = (n >= sel.argBase + 1) ? (uint8_t)tok[sel.argBase].toInt() : NUM_VOLUME_STEPS_DEFAULT;
    // An explicit Rtotal is applied as the channel's Rtotal (same as
    // CALRTOTAL first): it is what the saved calibration records, and what
    // the computed range is derived from.
    if (n >= sel.argBase + 2) {
      float rTotal = tok[sel.argBase + 1].toFloat();
      for (uint8_t i = 0; i < sel.count; i++) sel.items[i]->setRTotalOhms(rTotal);
    }
    if (sel.count == 2) {
      uint8_t vl, vr;
      DualCalibration::solveBoth(left_, right_, stream_, vl, vr, steps);
      uint8_t valid[2] = {vl, vr};
      const char *lab[2] = {"L", "R"};
      for (uint8_t i = 0; i < 2; i++) {
        stream_.print(lab[i]);
        stream_.print(F(": OK solved "));
        stream_.print(valid[i]);
        stream_.print('/');
        stream_.print(steps);
        stream_.println(F(" steps in range (see CALDUMP for details)"));
      }
    } else {
      LDRVolume *v = sel.items[0];
      uint8_t valid = v->calSolve(steps, stream_);
      stream_.print(F("OK solved "));
      stream_.print(valid);
      stream_.print('/');
      stream_.print(steps);
      stream_.println(F(" steps in range (see CALDUMP for details)"));
      stream_.println(F("NOTE: solved alone, so this side used its OWN range -- CALSOLVE with no L|R keeps both sides matched"));
    }

  } else if (cmd == "CALAUTO") {
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n < sel.argBase + 1) { stream_.println(F("ERR expected FULL or FAST")); return; }
    LDRVolume::CalMode mode;
    if (tok[sel.argBase].equalsIgnoreCase("FULL")) mode = LDRVolume::CalMode::FULL;
    else if (tok[sel.argBase].equalsIgnoreCase("FAST")) mode = LDRVolume::CalMode::FAST;
    else { stream_.println(F("ERR expected FULL or FAST")); return; }
    if (sel.count == 2) {
      DualCalibration::runBoth(*sel.items[0], *sel.items[1], mode, stream_);
    } else {
      sel.items[0]->runAutoCalibration(mode, stream_);
      // The other channel keeps its own curves; re-solve both with the
      // common range so this recalibration can't leave L and R unequal.
      DualCalibration::equalizeRanges(left_, right_, stream_);
    }

  } else if (cmd == "CALMODE") {
    SideSelection sel = resolveOptionalSide(tok, n);
    bool hasValue = (n >= sel.argBase + 1);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      if (hasValue) {
        if (tok[sel.argBase].equalsIgnoreCase("RTOTAL")) {
          v->setMode(LDRVolume::AttenuationMode::CONSTANT_RTOTAL);
          stream_.println(F("OK mode = CONSTANT_RTOTAL. Re-run CALSOLVE/CALAUTO to apply."));
        } else if (tok[sel.argBase].equalsIgnoreCase("FIXEDSERIES")) {
          v->setMode(LDRVolume::AttenuationMode::FIXED_SERIES);
          stream_.println(F("OK mode = FIXED_SERIES. Re-run CALSOLVE/CALAUTO to apply."));
        } else {
          stream_.println(F("ERR expected RTOTAL or FIXEDSERIES"));
        }
      } else {
        stream_.println(v->mode() == LDRVolume::AttenuationMode::FIXED_SERIES
                            ? F("Mode = FIXED_SERIES")
                            : F("Mode = CONSTANT_RTOTAL"));
      }
    }

  } else if (cmd == "CALSERIESDUTY") {
    SideSelection sel = resolveOptionalSide(tok, n);
    bool hasValue = (n >= sel.argBase + 1);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      if (hasValue) {
        v->setFixedSeriesDuty((uint16_t)tok[sel.argBase].toInt());
        stream_.print(F("OK fixedSeriesDuty = "));
        stream_.print(v->fixedSeriesDuty());
        stream_.println(F(". Re-run CALSOLVE/CALAUTO to apply."));
      } else {
        stream_.print(F("fixedSeriesDuty = "));
        stream_.println(v->fixedSeriesDuty());
      }
    }

  } else if (cmd == "CALRTOTAL") {
    SideSelection sel = resolveOptionalSide(tok, n);
    bool hasValue = (n >= sel.argBase + 1);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      if (hasValue) {
        float ohms = tok[sel.argBase].toFloat();
        if (ohms <= 0.0f) { stream_.println(F("ERR Rtotal must be positive")); continue; }
        v->setRTotalOhms(ohms);
        stream_.print(F("OK Rtotal set to "));
        stream_.print(ohms, 1);
        stream_.println(F(" ohm. Affects future CALSOLVE/CALAUTO -- re-run to apply to the saved LUT."));
      } else {
        stream_.print(F("Rtotal = "));
        stream_.print(v->rTotalOhms(), 1);
        stream_.println(F(" ohm"));
      }
    }

  } else if (cmd == "CALTRIM") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) sel.items[i]->runTrimPass(stream_);

  } else if (cmd == "CALSCAN") {
    SideSelection sel = resolveOptionalSide(tok, n);
    long scanStart = 900, scanEnd = 1148;
    if (n >= sel.argBase + 2) {
      scanStart = tok[sel.argBase].toInt();
      scanEnd = tok[sel.argBase + 1].toInt();
      if (scanStart < 4 || scanEnd <= scanStart || scanEnd > (long)DriverChannels::WRAP) {
        stream_.print(F("ERR CALSCAN window: need 4 <= start < end <= "));
        stream_.println(DriverChannels::WRAP);
        return;
      }
    } else if (n != sel.argBase) {
      stream_.println(F("ERR usage: CALSCAN [L|R] [start end]"));
      return;
    }
    for (uint8_t i = 0; i < sel.count; i++)
      sel.items[i]->runDiagnosticScan(stream_, (uint16_t)scanStart, (uint16_t)scanEnd);

  } else if (cmd == "CALREF") {
    SideSelection sel = resolveOptionalSide(tok, n);
    bool hasValue = (n >= sel.argBase + 1);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      if (hasValue) {
        float ohms = tok[sel.argBase].toFloat();
        if (ohms <= 0.0f) { stream_.println(F("ERR Rref must be positive")); continue; }
        v->setRRefOhms(ohms);
        stream_.print(F("OK Rref set to "));
        stream_.print(ohms, 1);
        stream_.println(F(" ohm. Applies to future reads only -- re-run CALAUTO to bake it into a saved calibration."));
      } else {
        stream_.print(F("Rref = "));
        stream_.print(v->rRefOhms(), 1);
        stream_.println(F(" ohm"));
      }
    }

  } else if (cmd == "CALSAVE") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      stream_.println(v->calSave() ? F("OK saved") : F("ERR save failed"));
    }

  } else if (cmd == "CALLOAD") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      stream_.println(v->calLoad() ? F("OK loaded") : F("ERR no valid saved calibration"));
    }

  } else if (cmd == "CALDUMP") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(F("=== ")); stream_.print(sideLabel(v)); stream_.println(F(" ===")); }
      stream_.println(v->mode() == LDRVolume::AttenuationMode::FIXED_SERIES
                          ? F("Mode: FIXED_SERIES")
                          : F("Mode: CONSTANT_RTOTAL"));
      stream_.print(F("Divider model: Rsrc="));
      stream_.print(AUDIO_SOURCE_OHMS, 0);
      stream_.print(F(" Rload="));
      stream_.print(AMP_INPUT_LOAD_OHMS, 0);
      stream_.println(F(" ohm (LUT dB = loaded gain)"));
      printCurve("Series", v->seriesCurve());
      printCurve("Shunt", v->shuntCurve());
      printLut(v->lut());
    }

  } else if (cmd == "VOL") {
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n < sel.argBase + 1) {
      // No step given: report the current volume instead of erroring.
      if (sel.count == 2 && left_.currentStep() == right_.currentStep() &&
          left_.isMuted() == right_.isMuted()) {
        describeVolume(stream_, left_);
        stream_.println();
      } else {
        for (uint8_t i = 0; i < sel.count; i++) {
          stream_.print(sel.items[i] == &left_ ? F("L: ") : F("R: "));
          describeVolume(stream_, *sel.items[i]);
          stream_.println();
        }
      }
      return;
    }
    uint8_t step = (uint8_t)tok[sel.argBase].toInt();
    bool ok[2];
    VolumeRamp::rampTo(sel.items, sel.count, step, ok);
    bool allOk = ok[0] && (sel.count < 2 || ok[1]);
    stream_.println(allOk ? F("OK") : F("ERR step out of range, no LUT, or that step is out of the curve's range"));

  } else if (cmd == "VOLUP") {
    SideSelection sel = resolveOptionalSide(tok, n);
    bool allOk = true;
    for (uint8_t i = 0; i < sel.count; i++) if (!sel.items[i]->stepUp()) allOk = false;
    stream_.print(allOk ? F("OK step=") : F("ERR step="));
    stream_.println(sel.items[0]->currentStep());

  } else if (cmd == "VOLDOWN") {
    SideSelection sel = resolveOptionalSide(tok, n);
    bool allOk = true;
    for (uint8_t i = 0; i < sel.count; i++) if (!sel.items[i]->stepDown()) allOk = false;
    stream_.print(allOk ? F("OK step=") : F("ERR step="));
    stream_.println(sel.items[0]->currentStep());

  } else if (cmd == "MUTE") {
    SideSelection sel = resolveOptionalSide(tok, n);
    if (n < sel.argBase + 1) { stream_.println(F("ERR expected ON or OFF")); return; }
    bool on = tok[sel.argBase].equalsIgnoreCase("ON");
    bool off = tok[sel.argBase].equalsIgnoreCase("OFF");
    if (!on && !off) { stream_.println(F("ERR expected ON or OFF")); return; }
    for (uint8_t i = 0; i < sel.count; i++) { if (on) sel.items[i]->mute(); else sel.items[i]->unmute(); }
    stream_.println(F("OK"));

  } else if (cmd == "I2CSCAN") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) {
      if (sel.count == 2) { stream_.print(sideLabel(sel.items[i])); stream_.println(':'); }
      sel.items[i]->i2cScan(); // activates/deactivates around the scan itself; relay state untouched
    }

  } else if (cmd == "ADCREAD") {
    SideSelection sel = resolveOptionalSide(tok, n);
    for (uint8_t i = 0; i < sel.count; i++) {
      LDRVolume *v = sel.items[i];
      if (sel.count == 2) { stream_.print(sideLabel(v)); stream_.print(F(": ")); }
      if (!v->relayEnergized()) {
        stream_.println(F("ERR relay not energized -- i2c bus is deactivated while de-energized."));
        if (sel.count == 1) stream_.println(F("    RELAY ON (or CALSTART) first."));
        continue;
      }
      float vRef, rs, rsh;
      bool ok = v->adcRead(vRef, rs, rsh);
      stream_.print(F("Vref="));
      stream_.print(vRef, 4);
      stream_.print(F("V"));
      if (!ok) {
        stream_.println(F("  ERR Vref <= 0 -- both LDRs likely dark (outside the ~50k operating range)"));
      } else {
        stream_.print(F("  Rs="));
        stream_.print(rs, 1);
        stream_.print(F(" ohm  Rsh="));
        stream_.print(rsh, 1);
        stream_.println(F(" ohm"));
      }
    }

  } else {
    stream_.println(F("ERR unrecognized command, try HELP"));
  }
}

SerialConsole::SideSelection SerialConsole::resolveOptionalSide(const String tok[], int n) {
  SideSelection sel;
  if (n >= 2 && tok[1].equalsIgnoreCase("L")) {
    sel.items[0] = &left_;
    sel.count = 1;
    sel.argBase = 2;
  } else if (n >= 2 && tok[1].equalsIgnoreCase("R")) {
    sel.items[0] = &right_;
    sel.count = 1;
    sel.argBase = 2;
  } else {
    sel.items[0] = &left_;
    sel.items[1] = &right_;
    sel.count = 2;
    sel.argBase = 1;
  }
  return sel;
}