// uart_test 串口收发测试demo
// 功能：开机打印提示，收到字符原样回显
void setup() {
  Serial.begin(115200);
  Serial.println("==== UART Test Ready ====");
  Serial.println("Input anything, I will echo back:");
}

void loop() {
  if (Serial.available() > 0) {
    char ch = Serial.read();
    Serial.print("Recv: ");
    Serial.println(ch);
  }
}
