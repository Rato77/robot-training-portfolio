/*
 * 第七组·赛道 B —— ESP32 安全逻辑固件
 * 默认停机 + 500ms 失联停机 + 限速 + 堵转检测 + 串口协议（01号定稿）
 * 目标环境：Arduino-ESP32 2.x / 3.x 均兼容（Wokwi 当前默认 3.x），115200 baud
 * 引脚：编码器左 GPIO4/5，右 GPIO18/19（系统说明文档 §五）
 */

#include <Arduino.h>
#include <stdlib.h>   // strtoul / atoi，避免个别内核版本未间接包含

/* ---------------- 常量（与交付文档一致，禁止随意改） ---------------- */
const uint32_t CMD_TIMEOUT_MS = 500;   // 5 × 100ms 心跳
const uint32_t TELEMETRY_MS   = 50;    // 20 Hz 上行
const uint32_t CONTROL_MS     = 10;    // 100 Hz 控制环
const uint32_t STALL_WINDOW_MS= 300;   // 堵转判定窗口
const int      PWM_LIMIT      = 179;   // 70% × 255

/* ---------------- 引脚 ---------------- */
const uint8_t PIN_STBY = 13;
const uint8_t PIN_PWMA = 12, PIN_AIN1 = 14, PIN_AIN2 = 27;
const uint8_t PIN_PWMB = 26, PIN_BIN1 = 25, PIN_BIN2 = 33;
const uint8_t PIN_EA = 4, PIN_EB = 5, PIN_EC = 18, PIN_ED = 19;
const uint8_t PIN_ESTOP = 32;          // 低有效，内部上拉
const uint8_t LED_RUN = 2, LED_FAULT = 15;

/* ---------------- LEDC PWM 兼容层 ----------------
 * Arduino-ESP32 2.x: ledcSetup(通道,频率,位数) + ledcAttachPin(引脚,通道) + ledcWrite(通道,占空比)
 * Arduino-ESP32 3.x: ledcAttach(引脚,频率,位数) + ledcWrite(引脚,占空比)，通道由内核分配
 * 用下方 3 个包装函数屏蔽差异，业务代码不需要 #if。
 */
const uint8_t PWM_CH_A = 0, PWM_CH_B = 1;
inline void pwmSetup(uint8_t pin, uint8_t ch) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  (void)ch;
  ledcAttach(pin, 20000, 8);
#else
  ledcSetup(ch, 20000, 8);
  ledcAttachPin(pin, ch);
#endif
}
inline void pwmWrite(uint8_t ch, uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(ch == PWM_CH_A ? PIN_PWMA : PIN_PWMB, duty);
#else
  ledcWrite(ch, duty);
#endif
}

/* ---------------- 状态与故障位 ---------------- */
enum State : uint8_t { ST_STOP = 0, ST_RUN = 1, ST_FAULT = 2 };
volatile State state = ST_STOP;
volatile uint8_t flags = 0;
const uint8_t BIT_TIMEOUT = 0x01, BIT_OVERSPEED = 0x02, BIT_ESTOP = 0x04;
const uint8_t BIT_STALL = 0x08, BIT_FORMAT = 0x10;

volatile int32_t cntL = 0, cntR = 0;
volatile uint8_t qL = 0, qR = 0;
uint32_t lastCmdMs = 0, lastTeleMs = 0, lastCtlMs = 0;
uint32_t lastSeq = 0, teleSeq = 0;
int cmdL = 0, cmdR = 0, outL = 0, outR = 0;
String rx = "";

/* 四倍频状态查表：索引 = (上一状态<<2)|当前状态，AB 格雷码 0-1-3-2 */
const int8_t QLUT[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};

/* ---------------- CRC-8 多项式 0x07，初值 0x00 ---------------- */
uint8_t crc8(const char *s) {
  uint8_t crc = 0x00;
  while (*s) {
    crc ^= (uint8_t)*s++;
    for (int i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

/* ---------------- 编码器中断 ---------------- */
void IRAM_ATTR isrL() {
  uint8_t s = (digitalRead(PIN_EA) ? 2 : 0) | (digitalRead(PIN_EB) ? 1 : 0);
  cntL += QLUT[(qL << 2) | s];
  qL = s;
}
void IRAM_ATTR isrR() {
  uint8_t s = (digitalRead(PIN_EC) ? 2 : 0) | (digitalRead(PIN_ED) ? 1 : 0);
  cntR += QLUT[(qR << 2) | s];
  qR = s;
}

/* ---------------- 唯一输出入口 ---------------- */
void applyOutput(int l, int r) {
  if (state != ST_RUN) { l = 0; r = 0; }       // STOP / FAULT 强制归零
  outL = constrain(l, -PWM_LIMIT, PWM_LIMIT);
  outR = constrain(r, -PWM_LIMIT, PWM_LIMIT);
  digitalWrite(PIN_STBY, state == ST_RUN ? HIGH : LOW);

  digitalWrite(PIN_AIN1, outL >= 0 ? HIGH : LOW);
  digitalWrite(PIN_AIN2, outL >= 0 ? LOW  : HIGH);
  digitalWrite(PIN_BIN1, outR >= 0 ? HIGH : LOW);
  digitalWrite(PIN_BIN2, outR >= 0 ? LOW  : HIGH);
  pwmWrite(PWM_CH_A, (uint32_t)abs(outL));
  pwmWrite(PWM_CH_B, (uint32_t)abs(outR));

  digitalWrite(LED_RUN,   state == ST_RUN   ? HIGH : LOW);
  digitalWrite(LED_FAULT, state == ST_FAULT ? HIGH : LOW);
}

void enterFault(uint8_t bit) {
  if (state != ST_FAULT) {
    state = ST_FAULT;
    flags |= bit;
    applyOutput(0, 0);                          // 判定后同一控制周期内归零
  }
}

/* ---------------- 帧解析：!C,... / #T,... ---------------- */
void handleCommand(const String &body) {
  // body 形如 "C,1,1,100,100,1000"
  String f[6]; int n = 0, start = 0;
  for (int i = 0; i <= body.length(); i++) {
    if (i == body.length() || body.charAt(i) == ',') {
      if (n < 6) f[n++] = body.substring(start, i);
      start = i + 1;
    }
  }
  if (n != 6 || f[0] != "C") { flags |= BIT_FORMAT; return; }

  uint32_t seq = strtoul(f[1].c_str(), nullptr, 10);
  if (seq <= lastSeq) return;                   // 重复/乱序帧不刷新看门狗
  lastSeq = seq;

  int cmd = atoi(f[2].c_str());
  int pl  = constrain(atoi(f[3].c_str()), -255, 255);
  int pr  = constrain(atoi(f[4].c_str()), -255, 255);
  // f[5] = t_ms，仅用于上位机时延统计

  if (cmd == 2) {                               // CLR_FAULT：只清锁存，不驱动
    flags = 0; state = ST_STOP; cmdL = cmdR = 0; applyOutput(0, 0); return;
  }
  if (cmd == 0) {                               // STOP
    state = ST_STOP; cmdL = cmdR = 0; applyOutput(0, 0); return;
  }
  if (cmd == 1) {                               // RUN
    if (state == ST_FAULT) return;              // 故障锁存：必须先 CLR
    cmdL = pl; cmdR = pr;
    state = ST_RUN;
    lastCmdMs = millis();                       // 只有合法 RUN 帧续命
    applyOutput(cmdL, cmdR);
  }
}

void handleFrame(const String &line) {
  if (line.length() < 8 || line.length() > 128) { flags |= BIT_FORMAT; return; }
  char kind = line.charAt(0);
  if (kind != '!') return;                       // 上行帧不解析
  int star = line.indexOf('*');
  if (star < 1) { flags |= BIT_FORMAT; return; }
  String body = line.substring(1, star);
  String tail = line.substring(star + 1);
  if (tail.length() != 2) { flags |= BIT_FORMAT; return; }
  uint8_t got = (uint8_t)strtoul(tail.c_str(), nullptr, 16);
  if (got != crc8(body.c_str())) { flags |= BIT_FORMAT; return; }  // CRC 错：丢弃，不刷新 lastCmdMs
  handleCommand(body);
}

/* ---------------- 上行遥测 ---------------- */
void sendTelemetry() {
  String b = "T,";
  b += teleSeq++; b += ",";
  b += micros();  b += ",";
  b += (long)cntL; b += ",";
  b += (long)cntR; b += ",";
  b += outL; b += ",";
  b += outR; b += ",";
  b += (uint8_t)state; b += ",";
  b += (uint8_t)flags;
  uint8_t c = crc8(b.c_str());
  Serial.print('#'); Serial.print(b); Serial.print('*');
  if (c < 16) Serial.print('0');
  Serial.print(c, HEX);
  Serial.print("\r\n");
}

/* ---------------- 初始化 ---------------- */
void setup() {
  Serial.begin(115200);
  pinMode(PIN_STBY, OUTPUT); digitalWrite(PIN_STBY, LOW);   // 默认待机
  pinMode(PIN_AIN1, OUTPUT); pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT); pinMode(PIN_BIN2, OUTPUT);
  pwmSetup(PIN_PWMA, PWM_CH_A);
  pwmSetup(PIN_PWMB, PWM_CH_B);
  pwmWrite(PWM_CH_A, 0); pwmWrite(PWM_CH_B, 0);

  pinMode(PIN_EA, INPUT_PULLUP); pinMode(PIN_EB, INPUT_PULLUP);
  pinMode(PIN_EC, INPUT_PULLUP); pinMode(PIN_ED, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_EA), isrL, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_EB), isrL, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_EC), isrR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ED), isrR, CHANGE);

  pinMode(PIN_ESTOP, INPUT_PULLUP);
  pinMode(LED_RUN, OUTPUT); pinMode(LED_FAULT, OUTPUT);
  state = ST_STOP; flags = 0; lastCmdMs = millis();
  applyOutput(0, 0);
}

/* ---------------- 主循环：非阻塞 ---------------- */
void loop() {
  // 1) 收帧
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') { handleFrame(rx); rx = ""; }
    else if (rx.length() < 128) rx += ch;
  }

  uint32_t now = millis();

  // 2) 硬件急停输入（低有效）
  if (digitalRead(PIN_ESTOP) == LOW) enterFault(BIT_ESTOP);

  // 3) 10ms 控制环
  if ((int32_t)(now - lastCtlMs) >= (int32_t)CONTROL_MS) {
    lastCtlMs = now;

    // 失联判定（唯一阈值来源）
