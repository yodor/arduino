#include <Servo.h>

static const int SpindlePin = 9;
static const long MaxRpm = 26400;
static Servo SpindleServo;

void setup() {
    Serial.begin(9600);
    SpindleServo.attach(SpindlePin);
    
    // Start at safe idle
    SpindleServo.writeMicroseconds(1000);
    
    Serial.println("UNO Advanced ESC Controller Online.");
    Serial.println("Commands: H=100%, L=0%, S<RPM> (e.g., S15000), S0=Stop");
}

void loop() {
    if (Serial.available() > 0) {
        char firstChar = Serial.peek();
        
        // Parse speed scaling commands
        if (firstChar == 's' || firstChar == 'S') {
            Serial.read(); 
            long targetRpm = Serial.parseInt();
            
            if (targetRpm > MaxRpm) {
                targetRpm = MaxRpm;
            }
            if (targetRpm < 0) {
                targetRpm = 0;
            }
            
            // Map the RPM dynamically into the 1000us to 2000us range
            int pulseWidth = 1000 + ((targetRpm * 1000) / MaxRpm);
            SpindleServo.writeMicroseconds(pulseWidth);
            
            Serial.print("Target: ");
            Serial.print(targetRpm);
            Serial.print(" RPM -> Pulse: ");
            Serial.print(pulseWidth);
            Serial.println("us");
        } 
        // Fallback to direct navigation inputs
        else {
            char actionChar = Serial.read();
            if (actionChar == 'h' || actionChar == 'H') {
                SpindleServo.writeMicroseconds(2000);
                Serial.println(">> SIGNAL HIGH (2000us)");
            } 
            else if (actionChar == 'l' || actionChar == 'L') {
                SpindleServo.writeMicroseconds(1000);
                Serial.println(">> SIGNAL LOW (1000us)");
            }
        }
    }
}
