// motor_test 电机驱动测试，上电默认停机 【B赛道G0安全要求】
// 驱动：L298N
// IN1=13，IN2=14
const int IN1 = 13;
const int IN2 = 14;

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  // 上电默认停机！安全逻辑
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  Serial.begin(115200);
  Serial.println("Motor test start, default STOP");
}

void loop() {
  // 正转2秒
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  Serial.println("Motor Forward");
  delay(2000);

  // 停机1秒
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  Serial.println("Motor Stop");
  delay(1000);

  // 反转2秒
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  Serial.println("Motor Backward");
  delay(2000);

  // 停机1秒
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  Serial.println("Motor Stop");
  delay(1000);
}
