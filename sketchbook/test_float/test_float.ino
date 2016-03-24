void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
}

float roundFloat(float float_in)
{
    float float_out = float_in;

    float fract = float_out - (int)float_out;
    fract*=100;
    fract = (int)fract * 1.0;
    fract/=100;
    
    float_out = (int)float_in + fract;
    return float_out;
}

void loop() {
  // put your main code here, to run repeatedly: 
  long val1 = 5243;
  float val2 = val1 / 1000.0;
  float test = roundFloat((float)(val1/1000.0));
  
  char buf[4];
  dtostrf(val2, 3, 2, buf);
  
  Serial.println(buf);
  Serial.println("--");
  delay(1000);
}
