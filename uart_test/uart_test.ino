#define IN1 13
#define IN2 14
void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
}
void loop() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  delay(800);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  delay(800);
}