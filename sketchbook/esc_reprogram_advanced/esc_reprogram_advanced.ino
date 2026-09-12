#include <Arduino.h>
#include <Servo.h>

static const int SpindlePin = 9;
static const long MaxRpm = 26400;
static Servo SpindleServo;

class EscDriver {
public:
    static const int PulseHighUs = 2000;
    static const int PulseLowUs = 1000;
    
    static bool IsRecording;
    static unsigned long LastToggleTime;

    static void ExecuteAutomation() {
        // Phase 1: Enter programming mode
        SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);
        delay(7000);

        // Phase 2: Stay HIGH to transit main menu loop items 1 to 6
        SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);
        delay(22500);

        // Phase 3: Drop to LOW to enter the selected Timing menu item
        SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);
        delay(2000);

        // Phase 4: Wait for Medium Timing value option, then flip HIGH to save
        delay(3500);
        SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);
        delay(2000);

        // Phase 5: Drop to LOW immediately to exit directly to normal operation
        SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);
    }
};

bool EscDriver::IsRecording = false;
unsigned long EscDriver::LastToggleTime = 0;

void setup() {
    Serial.begin(9600);
    SpindleServo.attach(SpindlePin);
    
    // Start at safe idle
    SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);
    
    Serial.println("UNO Advanced ESC Controller Online.");
    Serial.println("Commands: H=100%, L=0%, S<RPM>, A=Automate, R=Record Mode Toggle");
}

void loop() {
    if (Serial.available() > 0) {
        char firstChar = Serial.peek();
        
        // Handle Record Mode Toggle
        if (firstChar == 'r' || firstChar == 'R') {
            Serial.read(); 
            if (!EscDriver::IsRecording) {
                EscDriver::IsRecording = true;
                SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);
                EscDriver::LastToggleTime = millis();
                
                Serial.println("\n// === LIVE RECORDING STARTED ===");
                Serial.println("// ESC signal forced to HIGH. Power up your ESC now!");
                Serial.println("// Listen closely and send H or L inputs to log transitions.");
                Serial.println("SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);");
            } else {
                unsigned long durationMs = millis() - EscDriver::LastToggleTime;
                Serial.print("delay(\n");
                Serial.print(durationMs);
                Serial.println(");");
                
                EscDriver::IsRecording = false;
                SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);
                
                Serial.println("SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);");
                Serial.println("// === LIVE RECORDING STOPPED ===");
                Serial.println("// Copy the code lines above directly into your ExecuteAutomation() method.\n");
            }
        }
        // Handle incoming timing triggers while recording
        else if (EscDriver::IsRecording) {
            char actionChar = Serial.read();
            if (actionChar == 'l' || actionChar == 'L') {
                unsigned long durationMs = millis() - EscDriver::LastToggleTime;
                Serial.print("delay(");
                Serial.print(durationMs);
                Serial.println(");");
                Serial.println("SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);");
                
                SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);
                EscDriver::LastToggleTime = millis();
            }
            else if (actionChar == 'h' || actionChar == 'H') {
                unsigned long durationMs = millis() - EscDriver::LastToggleTime;
                Serial.print("delay(");
                Serial.print(durationMs);
                Serial.println(");");
                Serial.println("SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);");
                
                SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);
                EscDriver::LastToggleTime = millis();
            }
        }
        // Standard non-recording speed parsing
        else if (firstChar == 's' || firstChar == 'S') {
            Serial.read(); 
            long targetRpm = Serial.parseInt();
            
            if (targetRpm > MaxRpm) {
                targetRpm = MaxRpm;
            }
            if (targetRpm < 0) {
                targetRpm = 0;
            }
            
            int pulseWidth = EscDriver::PulseLowUs + ((targetRpm * 1000) / MaxRpm);
            SpindleServo.writeMicroseconds(pulseWidth);
            
            Serial.print("Target: ");
            Serial.print(targetRpm);
            Serial.print(" RPM -> Pulse: ");
            Serial.print(pulseWidth);
            Serial.println("us");
        } 
        // Standard manual adjustments
        else {
            char actionChar = Serial.read();
            if (actionChar == 'h' || actionChar == 'H') {
                SpindleServo.writeMicroseconds(EscDriver::PulseHighUs);
                Serial.println(">> SIGNAL HIGH (2000us)");
            } 
            else if (actionChar == 'l' || actionChar == 'L') {
                SpindleServo.writeMicroseconds(EscDriver::PulseLowUs);
                Serial.println(">> SIGNAL LOW (1000us)");
            }
        }
    }
}
