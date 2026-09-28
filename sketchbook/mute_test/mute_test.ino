/*
 * Raspberry Pi Pico 2 (RP2350) - Safe Delay Mute Controller
 * Holds GPIO 11 LOW (Muted) for 10 seconds post-boot, then shifts HIGH (Unmuted).
 * Uses a non-blocking millis() timer to keep the CPU active.
 */

// Define the mute control pin (GPIO 11 on Pico 2)
const int mutePin = 11;

// Configurable delay in milliseconds (10 seconds)
const unsigned long muteDuration = 10000; 

// Flag to track the switching state
bool isUnmuted = false;

void setup() {
  // 1. Immediately force the pin LOW before establishing it as an output.
  // This guarantees the absolute minimum transition window during boot.
  digitalWrite(mutePin, LOW);
  pinMode(mutePin, OUTPUT);

  // Optional: Initialize Serial Monitor for debugging
  Serial.begin(115200);
  Serial.println("System Booted. Amplifier status: MUTED (Failsafe Mode)");
}

void loop() {
  // Check if 10 seconds have elapsed and we haven't unmuted yet
  if (!isUnmuted && (millis() >= muteDuration)) {
    digitalWrite(mutePin, HIGH); // Send 3.3V to turn on the 4N25 and unmute the amp
    isUnmuted = true;            // Flip flag to prevent repeated execution
    
    Serial.println("10 seconds elapsed. Amplifier status: UNMUTED (Active Playback)");
  }

  // Your main audio logic or other application code can go here without being blocked
}
