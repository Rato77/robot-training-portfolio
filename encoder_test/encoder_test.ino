// encoder_test.ino W1 首个编码器读取脚本 Demo
// 控制主责：Rato77
// 引脚定义，可根据实际接线修改
#define ENC_A  34
#define ENC_B  35

volatile long encoder_count = 0;

void IRAM_ATTR readEncoderA(){
  if(digitalRead(ENC_A) == digitalRead(ENC_B)){
    encoder_count++;
  }else{
    encoder_count--;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ENC_A,INPUT_PULLUP);
  pinMode(ENC_B,INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_A), readEncoderA, CHANGE);
}

void loop() {
  static long last_cnt = 0;
  if(encoder_count != last_cnt){
    Serial.print("Encoder count: ");
    Serial.println(encoder_count);
    last_cnt = encoder_count;
  }
  delay(50);
}
