#include "SignalSampler.h"
#include "SharedDefs.h"
#include "DisplayDevice.h"
#include <Arduino.h>

const double noise_Vrms = 1.228;
const double Zero_dBm = 0.224;
const double Zero_dBU = 0.7746;




SignalSampler::SignalSampler()
{
  i_Vrms = 0.0;
  i_dBm = 0.0;
  i_dBV = 0.0; //SPL?
  i_dBu = 0.0; //
  i_Vp = 0.0;
  i_Volts = 0.0;

  i_signalMin = 0;
  i_signalMax = 0;

  i_sample = 0;

  SNR = 0.0;

  SNRdb = 0.0;

    
}

SignalSampler::~SignalSampler()
{


}

void SignalSampler::process(int from_pin, unsigned long sampleWindow)
{


  unsigned int sample = 0;

  unsigned long startMillis = millis();  // Start of sample window
  unsigned int peakToPeak = 0;   // peak-to-peak level


  i_signalMax = 0;
  i_signalMin = 1024;


  while (millis() - startMillis < sampleWindow)
  {


    sample = analogRead(from_pin); //A4

    if (CONFIG.values().pulse_led_signal_enabled==true) {
 

      analogWrite(PIN_ONBOARD_LED, (((sample) * (5.0 / 1024.0))-2.5) * pow(2,6+CONFIG.values().pulse_signal_scale));

    }

    if (sample < 1024)  // toss out spurious readings
    {
      if (sample > i_signalMax)
      {
        i_signalMax = sample;  // save just the max levels
      }
      else if (sample < i_signalMin)
      {
        i_signalMin = sample;  // save just the min levels
      }

    }

  }


  i_sample = sample;

  peakToPeak = i_signalMax - i_signalMin;  // max - min = peak-peak amplitude



  i_Volts = (peakToPeak * 3.3) / 1024 ;  // convert to volts



  i_Vp = i_Volts / 2.0;

  i_Vrms = i_Vp * 0.707;



  i_dBm = 20 * log10( i_Vrms / Zero_dBm );
  i_dBV = 20 * log10( i_Vrms );  //SPL  
  i_dBu = 20 * log10( i_Vrms / Zero_dBU ); // VU
  SNR = (i_Vrms / noise_Vrms) ;

  SNRdb = 20 * log10(SNR);


}


unsigned int SignalSampler::getSample()
{
  return i_sample;
}
double SignalSampler::getVRMS()
{
  return i_Vrms;
}
double SignalSampler::getDBM()
{
  return i_dBm;
}
double SignalSampler::getDBV()
{
  return i_dBV;
}
double SignalSampler::getDBU()
{
  return i_dBu;
}
double SignalSampler::getVP()
{
  return i_Vp;
}
double SignalSampler::getVolts()
{
  return i_Volts;
}
double SignalSampler::getSignalMin()
{
  return i_signalMin;
}
double SignalSampler::getSignalMax()
{
  return i_signalMax;
}

double SignalSampler::getSNR()
{
  return SNR;
}
double SignalSampler::getSNRDB()
{
  return SNRdb;
}

SignalSampler SAMPLER = SignalSampler();



