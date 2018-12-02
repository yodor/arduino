/*     Simple Stepper Motor Control Exaple Code
 *      
 *  by Dejan Nedelkovski, www.HowToMechatronics.com
 *  
 */

#define ORIG_Y_STEP_PIN         54
#define ORIG_Y_DIR_PIN          47
#define ORIG_Y_ENABLE_PIN       55
#define ORIG_Y_MIN_PIN          35
#define ORIG_Y_MAX_PIN          34

// defines pins numbers
const int stepPin = ORIG_Y_STEP_PIN; 
const int dirPin = ORIG_Y_DIR_PIN; 
const int enPin = ORIG_Y_ENABLE_PIN;

void setup() {
  // Sets the two pins as Outputs
  pinMode(stepPin,OUTPUT); 
  digitalWrite(stepPin,LOW);
  pinMode(dirPin,OUTPUT);
  digitalWrite(dirPin,LOW);

  pinMode(enPin, OUTPUT);
  digitalWrite(enPin, LOW);
  
}
void loop() {
  digitalWrite(dirPin,HIGH); // Enables the motor to move in a particular direction
  // Makes 200 pulses for making one full cycle rotation
  for(int x = 0; x < 100*16; x++) {
    digitalWrite(stepPin,HIGH); 
    delayMicroseconds(100); 
    digitalWrite(stepPin,LOW); 
    delayMicroseconds(100); 
  }
  delay(1000); // One second delay
  
  digitalWrite(dirPin,LOW); //Changes the rotations direction
  // Makes 400 pulses for making two full cycle rotation
  for(int x = 0; x < 200*16; x++) {
    digitalWrite(stepPin,HIGH);
    delayMicroseconds(100);
    digitalWrite(stepPin,LOW);
    delayMicroseconds(100);
  }
  delay(1000);
}
