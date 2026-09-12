// DRV8833 Pin Configuration (RP2040 GP16/GP17)
const int IN1_PIN = 16;  
const int IN2_PIN = 17;  

// Speed management (0 - 255)
int currentSpeed = 255; 
const int SPEED_STEP = 25;
const int MIN_SPEED = 50;   
const int MAX_SPEED = 255;

// Safety timeouts
unsigned long lastKeyTime = 0;
const unsigned long KEY_TIMEOUT_MS = 150;     // Stop if key repeat ceases

unsigned long moveStartTime = 0;
const unsigned long MAX_RUN_TIME_MS = 10000;   // 10-second continuous limit

bool isMoving = false;

void stopMotor() {
    // Coast mode (LOW/LOW)
    analogWrite(IN1_PIN, 0);
    analogWrite(IN2_PIN, 0);
    isMoving = false;
    moveStartTime = 0;
}

void moveLeft(int speed) {
    if (!isMoving) {
        moveStartTime = millis();
        isMoving = true;
    }
    analogWrite(IN1_PIN, speed);
    analogWrite(IN2_PIN, 0);
}

void moveRight(int speed) {
    if (!isMoving) {
        moveStartTime = millis();
        isMoving = true;
    }
    analogWrite(IN1_PIN, 0);
    analogWrite(IN2_PIN, speed);
}

void setup() {
    pinMode(IN1_PIN, OUTPUT);
    pinMode(IN2_PIN, OUTPUT);
    
    stopMotor();
    Serial.begin(9600);
}

void loop() {
    if (Serial.available() > 0) {
        char key = Serial.read();
        
        if (key >= 'A' && key <= 'Z') {
            key = key + ('a' - 'A');
        }

        switch (key) {
            case 'a':
                moveLeft(currentSpeed);
                lastKeyTime = millis();
                break;

            case 'd':
                moveRight(currentSpeed);
                lastKeyTime = millis();
                break;

            case 'w':
                currentSpeed = min(MAX_SPEED, currentSpeed + SPEED_STEP);
                break;

            case 's':
                currentSpeed = max(MIN_SPEED, currentSpeed - SPEED_STEP);
                break;

            case 'q':
                stopMotor();
                lastKeyTime = 0;
                break;

            default:
                break;
        }
    }

    // 1. Safety stop: Key release timeout
    if (lastKeyTime > 0 && (millis() - lastKeyTime > KEY_TIMEOUT_MS)) {
        stopMotor();
        lastKeyTime = 0;
        Serial.println("Safety stop - Key release timeout");
    }

    // 2. Safety stop: 10-second continuous execution cutoff
    if (isMoving && (millis() - moveStartTime >= MAX_RUN_TIME_MS)) {
        stopMotor();
        lastKeyTime = 0;
        Serial.println("Safety stop - Max time 10 seconds");
        delay(2000);
    }
}
