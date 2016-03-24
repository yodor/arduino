

int pin_sensor1_low = 5;
int pin_sensor1_high = 6;
int pin_pump1_relay = 2;
boolean pump1_working = false;


int pin_sensor2_low = 7;
int pin_sensor2_high = 8;
int pin_pump2_relay = 3;
boolean pump2_working = false;

const int PIN_LED = 9;
const int LED_PWM_PERIOD = 1500;

int interval_update = 1000;
long last_update_millis = 0;   

void setup()
{
  
  pinMode(PIN_LED, OUTPUT);
  analogWrite(PIN_LED, 128);
 
  pinMode(pin_pump1_relay, OUTPUT);
  digitalWrite(pin_pump1_relay, LOW);
  pinMode(pin_pump2_relay, OUTPUT);
  digitalWrite(pin_pump2_relay, LOW);
  
  //pinMode(pin_pump_gnd, OUTPUT);
  //digitalWrite(pin_pump_gnd, LOW);
  
  pinMode(pin_sensor1_low, INPUT_PULLUP);
  //digitalWrite(pin_pump_low, HIGH);
  
  pinMode(pin_sensor1_high, INPUT_PULLUP);
  //digitalWrite(pin_pump_high, HIGH);
  
  pinMode(pin_sensor2_low, INPUT_PULLUP);
  //digitalWrite(pin_pump_low, HIGH);
  
  pinMode(pin_sensor2_high, INPUT_PULLUP);
  //digitalWrite(pin_pump_high, HIGH);
  
  Serial.begin(9600);
  Serial.println("Setup Done");
}

void loop()
{
  int value1_low = digitalRead(pin_sensor1_low);
  int value1_high = digitalRead(pin_sensor1_high);
  int value2_low = digitalRead(pin_sensor2_low);
  int value2_high = digitalRead(pin_sensor2_high);
  //Serial.println("");
  
  
  
  if ( (value1_low == LOW) && (value1_high == LOW) && (pump1_working==false) ) {
    startPump(1);
  }

  if ( (value1_low == HIGH) && (value1_high == HIGH)  && pump1_working==true ) {
    stopPump(1);
  }
  
  if ( (value2_low == LOW) && (value2_high == LOW) && (pump2_working==false) ) {
    startPump(2);
  }

  if ( (value2_low == HIGH) && (value2_high == HIGH)  && pump2_working==true ) {
    stopPump(2);
  }
  
  if ( millis() - last_update_millis > interval_update ) {
    Serial.print("PUMP#1 => ");
    Serial.print(" | PROBE_LOW=");
    Serial.print(value1_low);
    Serial.print(" | PROBE_HIGH=");
    Serial.println(value1_high);
    Serial.print("PUMP#2 => ");
    Serial.print(" | PROBE_LOW=");
    Serial.print(value2_low);
    Serial.print(" | PROBE_HIGH=");
    Serial.println(value2_high);
    
    Serial.println("Pump status: ");
    Serial.print(" [1] => ");
    Serial.print(pump1_working);
    Serial.print(" [2] => ");
    Serial.println(pump2_working);
    Serial.println("");
    last_update_millis = millis();
  }
  
  int value = 128+127*cos(2*PI/LED_PWM_PERIOD*millis());
  analogWrite(PIN_LED, value);
}

void startPump(int pump_number)
{
  Serial.print("Starting pump: ");
  Serial.println(pump_number);
  if (pump_number == 1) {
    digitalWrite(pin_pump1_relay, HIGH);
    pump1_working = true;
  }
  else if (pump_number == 2) {
    digitalWrite(pin_pump2_relay, HIGH);
    pump2_working = true;
  }
}
void stopPump(int pump_number)
{
  Serial.print("Stopping pump: ");
  Serial.println(pump_number);
  if (pump_number == 1) {
    digitalWrite(pin_pump1_relay, LOW);
    pump1_working = false;
  }
  else if (pump_number == 2) {
    digitalWrite(pin_pump2_relay, LOW);
    pump2_working = false;
  }
}
