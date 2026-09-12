#include <Arduino_GFX_Library.h>
#include <math.h>

// 1. YOUR PRODUCTION HARDWARE PIN MAP
#define TFT_DC     4   // Screen DC  -> Pico GP4 (Pin 6)
#define TFT_CS     5   // Screen CS  -> Pico GP5 (Pin 7)
#define TFT_SCK    2   // Screen SCK -> Pico GP2 (Pin 4) [SPI0 SCK]
#define TFT_MOSI   3   // Screen SDA -> Pico GP3 (Pin 5) [SPI0 TX]
#define TFT_MISO  -1   // Dummy MISO -> Pico GP16 (Pin 21) [Bypasses register traps]
#define TFT_RST    6   // Screen RST -> Pico GP6 (Pin 9)
#define TFT_BL     7   // Screen BL  -> Pico GP7 (Pin 10)

// 4-BUTTON INTERFACE PINS
#define BTN_UP     0   // UP Button    -> Pico GP0 (Pin 1)
#define BTN_DOWN   1   // DOWN Button  -> Pico GP1 (Pin 2)
#define BTN_LEFT  14   // LEFT Button  -> Pico GP14 (Pin 19)
#define BTN_RIGHT 15   // RIGHT Button -> Pico GP15 (Pin 20)

// SINGLE-CHANNEL ANALOG INPUT PIN
#define AUDIO_R   28   // Sampling directly from your Right Channel breadboard circuit

// EMPTY DISPLAY POINTERS FOR CORE 1 INITIALIZATION
Arduino_DataBus *bus = nullptr;
Arduino_GFX *tft = nullptr;

// LOCAL CORE 0 STATE VARIABLES
int volume = 20;
bool currentModeIsWave = true;
unsigned long lastButtonTime = 0;

// GLOBAL PERFORMANCE CACHE STRUCTS
int oldWaveY[275];           
int oldFFTHeight[18];

// =========================================================================
// 🔥 CORE 0: HIGH-SPEED AUDIO ACQUISITION & NATIVE FOURIER MATRIX
// =========================================================================
void setup() {
  Serial1.setTX(12);  Serial1.setRX(13);
  Serial1.begin(115200);

  analogReadResolution(12); 
  pinMode(AUDIO_R, INPUT);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
}

void loop() {
  // --- BUTTON NAVIGATION SAMPLING ---
  if (millis() - lastButtonTime > 200) {
    if (digitalRead(BTN_UP) == LOW && volume < 50)   { volume++; lastButtonTime = millis(); }
    if (digitalRead(BTN_DOWN) == LOW && volume > 0)   { volume--; lastButtonTime = millis(); }
    if (digitalRead(BTN_LEFT) == LOW || digitalRead(BTN_RIGHT) == LOW) {
      currentModeIsWave = !currentModeIsWave;
      lastButtonTime = millis();
    }
  }

  // --- AUDIO ACQUISITION PIPELINE ---
  if (currentModeIsWave) {
    rp2040.fifo.push_nb(0x10000000 | (volume & 0xFF));
    
    for (int i = 0; i < 275; i++) {
      int rawSample = analogRead(AUDIO_R);
      int finalY = map(rawSample, 0, 4096, 130, 10);
      //if (finalY < 12)  finalY = 12;
      //if (finalY > 130) finalY = 130;
      
      rp2040.fifo.push_nb(0x20000000 | finalY);
      delayMicroseconds(50); 
    }
  } 
  else {
    rp2040.fifo.push_nb(0x30000000 | (volume & 0xFF));
    
    // GATHER CONSECUTIVE TIME SNAPSHOTS
    const int numSamples = 64;
    int16_t audioSamples[numSamples];
    
    for (int i = 0; i < numSamples; i++) {
      audioSamples[i] = analogRead(AUDIO_R) - 2048; // Center bias subtraction
      delayMicroseconds(62); // Strict ~16kHz sample rate timing
    }

    // PROCESS 18 DISTINCT LOGARITHMIC FREQUENCY BANDS (True Bass to Treble Sweep)
    for (int b = 0; b < 18; b++) {
      float realSum = 0;
      float imagSum = 0;
      
      // True exponential frequency scaling spreads bands evenly over all 18 channels
      float targetFreqBin = 1.0 + (pow(b, 1.45) * 0.45); 

      for (int t = 0; t < numSamples; t++) {
        float angle = (2.0 * M_PI * targetFreqBin * t) / numSamples;
        realSum += audioSamples[t] * cos(angle);
        imagSum -= audioSamples[t] * sin(angle);
      }

      float magnitude = sqrt((realSum * realSum) + (imagSum * imagSum)) / numSamples;

      // Logarithmic gain mapping scales mid and high bands so they dance actively
      int dynamicCeiling = 240; // Bass ceiling
      if (b > 3)  dynamicCeiling = 180;  // Low mids
      if (b > 8)  dynamicCeiling = 110;  // Midrange
      if (b > 13) dynamicCeiling = 50;   // Treble sensitivity boost

      int barHeight = map((int)magnitude, 3, dynamicCeiling, 4, 112);
      if (barHeight < 4)   barHeight = 4;
      if (barHeight > 112) barHeight = 112;

      // FIXED ZERO-SHIFT BIT PACKING DEPLOYMENT
      uint32_t fftPacket = 0x40000000 | ((b & 0xFF) << 8) | (barHeight & 0xFF);
      rp2040.fifo.push_nb(fftPacket);
    }
    delay(4); 
  }
}

// =========================================================================
// 🚀 CORE 1: 100MHz HARDWARE FIFO GRAPHICS EXTRACTION ENGINE
// =========================================================================
void setup1() {
  bus = new Arduino_RPiPicoSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, spi0);
  tft = new Arduino_NV3007(
      bus, TFT_RST, 1, false, 142, 428, 12, 0, 14, 0,
      nv3007_279_init_operations, sizeof(nv3007_279_init_operations)
  );

  pinMode(TFT_BL, OUTPUT);   digitalWrite(TFT_BL, HIGH); 
  pinMode(TFT_RST, OUTPUT);  digitalWrite(TFT_RST, HIGH); delay(10);
  digitalWrite(TFT_RST, LOW); delay(150);                  
  digitalWrite(TFT_RST, HIGH); delay(300);                  

  if (!tft->begin(100000000)) { while(1); } // 100MHz extreme speed push
  tft->invertDisplay(false);
  tft->fillScreen(0x0000);    

  for(int i = 0; i < 275; i++) { oldWaveY[i] = 70; }
  for(int b = 0; b < 18; b++) { oldFFTHeight[b] = 4; }

  // Draw Static Dashboard Accents
  tft->drawFastVLine(135, 0, 142, 0x4208);   
  tft->drawRect(1, 1, 426, 140, 0x2104);     
  tft->setTextColor(0x07E0); tft->setTextSize(1);
  tft->setCursor(12, 12);    tft->print("AMP SYSTEM");
  tft->setTextColor(0x94B2);
  tft->setCursor(12, 45);    tft->print("VISUALIZER:");
  tft->setCursor(12, 90);    tft->print("VOLUME:");
}

void loop1() {
  if (tft == nullptr) return;

  static int localVol = -1;
  static bool displayIsWave = true;
  static int waveXCounter = 0;
  int startX = 145;

  while (rp2040.fifo.available()) {
    uint32_t packet = rp2040.fifo.pop();
    uint32_t prefix = packet & 0xF0000000;

    if (prefix == 0x10000000 || prefix == 0x30000000) {
      bool incomingModeIsWave = (prefix == 0x10000000);
      int incomingVol = packet & 0x000000FF; 

      if (incomingModeIsWave != displayIsWave) {
        tft->fillRect(145, 10, 420 - 145, 122, 0x0000); 
        tft->fillRect(12, 60, 115, 20, 0x0000); 
        tft->setTextColor(0x07FF); tft->setTextSize(2);
        tft->setCursor(12, 60);
        tft->print(incomingModeIsWave ? "WAVE" : "FFT");
        
        waveXCounter = 0;
        for(int i = 0; i < 275; i++) oldWaveY[i] = 70;
        for(int b = 0; b < 18; b++) oldFFTHeight[b] = 4;
        displayIsWave = incomingModeIsWave;
      }

      if (incomingVol != localVol) {
        tft->fillRect(65, 90, 60, 20, 0x0000);
        tft->setTextColor(0xFFE0); tft->setTextSize(2);
        tft->setCursor(65, 90); tft->print(incomingVol);

        tft->fillRect(12, 118, 115, 6, 0x18C3); 
        int volumePixelWidth = map(incomingVol, 0, 50, 0, 115);
        tft->fillRect(12, 118, volumePixelWidth, 6, 0xF800); 
        localVol = incomingVol;
      }
    }
    else if (prefix == 0x20000000 && displayIsWave) {
      int data = packet & 0x0000FFFF; 
      int targetX = startX + waveXCounter;
      
      //tft->drawPixel(targetX, oldWaveY[waveXCounter], 0x0000); 
      //tft->drawPixel(targetX, data, 0xFFFF);   
      tft->drawFastVLine(targetX, oldWaveY[waveXCounter], 1, 0x0000);
      tft->drawFastVLine(targetX, data, 1, 0xFFFF);          
      oldWaveY[waveXCounter] = data; 
      
      waveXCounter++;
      if (waveXCounter >= 275) waveXCounter = 0; 
    }
    else if (prefix == 0x40000000 && !displayIsWave) {
      int barIdx = (packet & 0x0000FF00) >> 8;  
      int currentBinHeight = packet & 0x000000FF; 
      int targetX = startX + (barIdx * 15);

      if (barIdx < 18) {
        if (currentBinHeight < oldFFTHeight[barIdx]) {
          tft->fillRect(targetX, 127 - oldFFTHeight[barIdx], 8, oldFFTHeight[barIdx] - currentBinHeight, 0x0000);
        }
        
        uint16_t dynamicBarColor = (currentBinHeight > 85) ? 0xF800 : 0x07E0; 
        tft->fillRect(targetX, 127 - currentBinHeight, 8, currentBinHeight, dynamicBarColor);
        oldFFTHeight[barIdx] = currentBinHeight;
      }
    }
  }
}
