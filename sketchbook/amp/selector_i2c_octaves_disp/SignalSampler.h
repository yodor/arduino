#ifndef SIGNAL_SAMPLER_H
#define SIGNAL_SAMPLER_H

#include "SelectorConfig.h"


class SignalSampler {
  
public:
  SignalSampler();
  ~SignalSampler();

  void process(int from_pin, unsigned long sampleWindow);
  
  double getVRMS();
  double getDBM();
  double getDBV();
  double getDBU();
  double getVP();
  double getVolts();
  double getSignalMin();
  double getSignalMax();

  double getSNR();
  double getSNRDB();
  unsigned int getSample();

private:
  
  double i_Vrms;
  double i_dBm;
  double i_dBV; //SPL?
  double i_dBu; //
  double i_Vp;
  double i_Volts;
  
  int i_signalMin;
  int i_signalMax;

  double SNR;

  double SNRdb;

  unsigned int i_sample;



};

extern SignalSampler SAMPLER;
#endif
