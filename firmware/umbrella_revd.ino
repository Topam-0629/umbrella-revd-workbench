// 스마트 우산통 REV D — 자동 운전 코드
// 보드: ESP32 Dev Module (Arduino IDE, ESP32 보드 패키지). 추가 라이브러리 없음.
// 같은 폴더의 glyphs.h(화면 글자 그림)가 함께 있어야 한다.
// 핀 배정은 연결 설명서 핀표·revd_wiring_test와 같다. 배선 시험(S0~S12, T0~T9)을 끝낸 뒤에 올린다.
//
// 하는 일
//  - 켜지면 두 받침을 차례로 끝까지 내리고, 빈 통 거리를 재서 기준으로 삼는다.
//  - 우산을 넣으면(3초 동안 계속 감지) 보관으로 바꾸고 그 구멍 팬을 10분 돌린다.
//  - 보관 15초 뒤부터, 사람이 다가오면(PIR) 먼저 넣은 우산의 받침을 올린다.
//  - 우산을 빼면(2초 동안 빈 통) 내린다. 30초 안에 안 가져가면 내리고 60초 동안 다시 안 올린다.
//  - 모터는 한 번에 하나만. 최근 10분 모터 시간이 120초를 넘으면 쉰다.
//
// 시리얼 모니터 115200, 줄 끝 "새 줄". 명령: s 상태 / UL UR 올림 / DL DR 내림 / c 빈 통 다시 재기 / ? 도움말

#include <SPI.h>
#include "glyphs.h"

// ---------------- 핀 ----------------
const int L_RPWM = 25, L_LPWM = 26, L_EN = 27;
const int R_RPWM = 32, R_LPWM = 33, R_EN = 13;
const int USL_TRIG = 14, USL_ECHO = 34;
const int USR_TRIG = 4,  USR_ECHO = 35;
const int PIR_OUT = 36;
const int TFT_SCK = 18, TFT_MOSI = 23, TFT_CS = 5, TFT_DC = 16, TFT_RST = 17;
const int DF_TX = 21, DF_RX = 22;
const int FAN_L = 19, FAN_R = 2;

// ---------------- 실물에 맞춰 바꾸는 값 ----------------
const unsigned long MOVE_MS = 8000;        // 끝까지 움직이는 시간 (시험 T9에서 잰 값 + 여유. 8초 넘게 걸리면 늘린다)
const long EMPTY_REF_MM[2] = {0, 0};      // 시험 T3에서 잰 빈 통 거리 {L, R}. 0이면 켤 때마다 잰 값을 쓴다
const long DEFAULT_BASE_MM = 92;          // 켤 때 잰 값이 이상할 때 쓰는 빈 통 거리 (설계값)
const bool TAKEN_DETECT_UP = true;        // 올린 상태에서 「빈 통 2초 = 꺼냄」을 쓸지. 우산을 넣고 UL로 올린 뒤 s로 거리를 봐서 빈 통처럼 나오면 false
const int  EMPTY_TOL_MM = 8;               // 빈 통 판정 폭
const int  PRESENT_DROP_MM = 10;           // 빈 통보다 이만큼 짧으면 우산 있음
const unsigned long CONFIRM_MS = 3000;     // 같은 판정이 이만큼 이어져야 확정
const unsigned long TAKEN_MS = 2000;       // 올린 뒤 빈 통이 이만큼 이어지면 꺼낸 것
const unsigned long LIFT_AFTER_STORE_MS = 15000;
const unsigned long UP_TIMEOUT_MS = 30000; // 올린 뒤 안 가져가면 내림
const unsigned long RELIFT_BLOCK_MS = 60000;
const unsigned long FAN_MS = 600000;       // 보관 뒤 팬 10분
const unsigned long DUTY_WINDOW_MS = 600000, DUTY_MAX_MS = 120000;   // 최근 10분 모터 120초
const unsigned long BOOT_PIR_IGNORE_MS = 60000;
const unsigned long PIR_HOLD_MS = 500;     // PIR이 0.5초 이상 1이어야 인정
const unsigned long VOICE_MS = 4000;       // 안내 음성 길이(대략)
const int  DF_VOLUME = 20;                 // 0~30

// ---------------- 상태 ----------------
enum BayState { B_EMPTY, B_STORED, B_UP, B_ERR };
enum Cls { C_UNKNOWN, C_EMPTY, C_PRESENT };
struct Bay {
  char name;
  int rpwm, lpwm, en, trig, echo, fan;
  BayState st;
  long base;                 // 빈 통 거리 (mm)
  long dist;                 // 최근 중앙값
  Cls cls, lastCls;
  unsigned long clsSince;    // lastCls가 시작된 시각
  unsigned long storedAt, upAt, fanUntil, noLiftUntil;
  bool manual;               // 시리얼 명령으로 올림 (짧은 물건: 센서가 못 봄)
};
Bay bay[2] = {
  {'L', L_RPWM, L_LPWM, L_EN, USL_TRIG, USL_ECHO, FAN_L, B_EMPTY, 0, 0, C_UNKNOWN, C_UNKNOWN, 0, 0, 0, 0, 0, false},
  {'R', R_RPWM, R_LPWM, R_EN, USR_TRIG, USR_ECHO, FAN_R, B_EMPTY, 0, 0, C_UNKNOWN, C_UNKNOWN, 0, 0, 0, 0, 0, false},
};

unsigned long bootAt = 0;
unsigned long pirIgnoreUntil = 0;
unsigned long pirHighSince = 0;
bool pirArmed = false;            // 0을 본 뒤에만 다음 1을 사람으로 인정
unsigned long pirLowSince = 0;
bool voicedThisVisit = false;
unsigned long byeUntil = 0;
bool waitShown = false;

// 모터 사용 기록 (최근 10분)
const int RUNLOG = 40;
unsigned long runStart[RUNLOG], runDur[RUNLOG];
int runIdx = 0;

// =====================================================================
// 화면 ST7789 (직접 구동)
// =====================================================================
const uint16_t COL_BLACK = 0x0000, COL_WHITE = 0xFFFF, COL_BLUE = 0x1AFF, COL_GRAY = 0x9CF3, COL_YELLOW = 0xF600, COL_RED = 0xF9E6;

void tftCmd(uint8_t c) { digitalWrite(TFT_DC, LOW); digitalWrite(TFT_CS, LOW); SPI.transfer(c); digitalWrite(TFT_CS, HIGH); }
void tftData(uint8_t d) { digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW); SPI.transfer(d); digitalWrite(TFT_CS, HIGH); }
void tftWindow(int x0, int y0, int x1, int y1) {
  tftCmd(0x2A); tftData(x0 >> 8); tftData(x0 & 0xFF); tftData(x1 >> 8); tftData(x1 & 0xFF);
  tftCmd(0x2B); tftData(y0 >> 8); tftData(y0 & 0xFF); tftData(y1 >> 8); tftData(y1 & 0xFF);
  tftCmd(0x2C);
}
void tftRect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  tftWindow(x, y, x + w - 1, y + h - 1);
  digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW);
  for (long i = 0; i < (long)w * h; i++) { SPI.transfer(c >> 8); SPI.transfer(c & 0xFF); }
  digitalWrite(TFT_CS, HIGH);
}
// 글자 그림: cx = 가운데 x, baseline = 글자 기준선 y
void tftText(const Glyph &g, int cx, int baseline, uint16_t fg, uint16_t bg) {
  int x = cx - g.w / 2, y = baseline + g.dy;
  int bpr = (g.w + 7) / 8;
  tftWindow(x, y, x + g.w - 1, y + g.h - 1);
  digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW);
  for (int r = 0; r < g.h; r++)
    for (int c = 0; c < g.w; c++) {
      uint8_t b = pgm_read_byte(g.bits + r * bpr + (c >> 3));
      uint16_t col = (b & (0x80 >> (c & 7))) ? fg : bg;
      SPI.transfer(col >> 8); SPI.transfer(col & 0xFF);
    }
  digitalWrite(TFT_CS, HIGH);
}
void tftInit() {
  pinMode(TFT_CS, OUTPUT); pinMode(TFT_DC, OUTPUT); pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  SPI.begin(TFT_SCK, 39, TFT_MOSI, -1);   // MISO는 입력 전용 39 (GPIO19는 L 팬)
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_RST, LOW); delay(20); digitalWrite(TFT_RST, HIGH); delay(150);
  tftCmd(0x01); delay(150);
  tftCmd(0x11); delay(120);
  tftCmd(0x3A); tftData(0x55);
  tftCmd(0x36); tftData(0x00);   // 시험 T2-1에서 위아래가 뒤집혔으면 0xC0
  tftCmd(0x21);                  // 빨강이 하늘색으로 나오면 0x20
  tftCmd(0x13); tftCmd(0x29); delay(20);
}

// 화면 그리기 (바뀔 때만 다시 그림)
enum Screen { S_NONE, S_IDLE, S_STORE, S_LIFT, S_DOWN, S_BYE, S_WAIT, S_ERR };
int shownScreen = -1, shownSig = -1;

void header(uint16_t c) { tftRect(0, 0, 240, 320, COL_BLACK); tftRect(0, 0, 240, 6, c); }
const Glyph &statusGlyph(int i) {
  Bay &b = bay[i];
  if (b.st == B_ERR) return i == 0 ? T_L_ERR : T_R_ERR;
  if (b.st == B_EMPTY) return i == 0 ? T_L_EMPTY : T_R_EMPTY;
  return i == 0 ? T_L_KEEP : T_R_KEEP;
}
void drawScreen(Screen s, int sig) {
  if (s == shownScreen && sig == shownSig) return;
  shownScreen = s; shownSig = sig;
  switch (s) {
    case S_IDLE:
      header(COL_BLUE);
      tftText(T_UMB, 120, 70, COL_WHITE, COL_BLACK); tftText(T_PUT, 120, 114, COL_WHITE, COL_BLACK);
      tftRect(24, 170, 192, 2, COL_BLUE);
      tftText(statusGlyph(0), 120, 206, COL_GRAY, COL_BLACK); tftText(statusGlyph(1), 120, 244, COL_GRAY, COL_BLACK);
      tftText(T_TIP, 120, 290, COL_GRAY, COL_BLACK);
      break;
    case S_STORE:
      header(COL_BLUE);
      tftText(T_KEEP, 120, 70, COL_WHITE, COL_BLACK); tftText(T_DRY, 120, 114, COL_WHITE, COL_BLACK);
      tftRect(24, 170, 192, 2, COL_BLUE);
      tftText(statusGlyph(0), 120, 206, COL_GRAY, COL_BLACK); tftText(statusGlyph(1), 120, 244, COL_GRAY, COL_BLACK);
      tftText(T_FAN, 120, 290, COL_GRAY, COL_BLACK);
      break;
    case S_LIFT:
      header(COL_BLUE);
      tftText(T_UMB, 120, 70, COL_WHITE, COL_BLACK); tftText(T_TAKE, 120, 114, COL_BLUE, COL_BLACK);
      tftRect(24, 170, 192, 2, COL_BLUE);
      tftText(T_UP, 120, 206, COL_GRAY, COL_BLACK); tftText(T_HAND, 120, 244, COL_GRAY, COL_BLACK);
      break;
    case S_DOWN:
      header(COL_BLUE);
      tftText(T_DOWN, 120, 120, COL_WHITE, COL_BLACK); tftText(T_HAND, 120, 160, COL_GRAY, COL_BLACK);
      break;
    case S_BYE:
      header(COL_BLUE);
      tftText(T_GOOD, 120, 96, COL_WHITE, COL_BLACK); tftText(T_BE, 120, 140, COL_WHITE, COL_BLACK);
      break;
    case S_WAIT:
      header(COL_YELLOW);
      tftText(T_WAIT1, 120, 96, COL_WHITE, COL_BLACK); tftText(T_WAIT2, 120, 140, COL_WHITE, COL_BLACK);
      tftText(T_COOL, 120, 206, COL_GRAY, COL_BLACK);
      break;
    case S_ERR:
      header(COL_RED);
      tftText(T_SEN1, 120, 96, COL_WHITE, COL_BLACK); tftText(T_SEN2, 120, 140, COL_WHITE, COL_BLACK);
      tftText(T_REST, 120, 206, COL_GRAY, COL_BLACK);
      break;
    default: break;
  }
}

// =====================================================================
// 소리 (DFPlayer)
// =====================================================================
unsigned long voiceUntil = 0;
void dfSend(uint8_t cmd, uint16_t param) {
  uint8_t b[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, (uint8_t)(param >> 8), (uint8_t)param, 0, 0, 0xEF};
  int16_t sum = -(b[1] + b[2] + b[3] + b[4] + b[5] + b[6]);
  b[7] = (uint8_t)(sum >> 8); b[8] = (uint8_t)sum;
  Serial2.write(b, 10);
}
void playVoice(uint16_t track) {   // /mp3/0001.mp3 = 1
  dfSend(0x12, track);
  voiceUntil = millis() + VOICE_MS;
  pirIgnoreUntil = max(pirIgnoreUntil, voiceUntil + 1500);
}

// =====================================================================
// 거리 센서
// =====================================================================
long readMM(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(3);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 30000UL);
  if (us == 0) return -1;
  return (long)(us * 0.343 / 2.0);
}
long median5(Bay &b) {
  long v[5];
  for (int i = 0; i < 5; i++) { v[i] = readMM(b.trig, b.echo); delay(30); }
  for (int i = 1; i < 5; i++) { long k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
  return v[2];
}
Cls classify(const Bay &b, long d) {
  if (d <= 0 || d < 30) return C_PRESENT;                     // -1(울림 없음), 0, 아주 가까움
  if (labs(d - b.base) <= EMPTY_TOL_MM) return C_EMPTY;
  if (d < b.base - PRESENT_DROP_MM) return C_PRESENT;
  return C_UNKNOWN;
}
// 한 구멍을 한 번 재고 판정 갱신
void sense(Bay &b) {
  b.dist = median5(b);
  Cls c = classify(b, b.dist);
  if (c != b.lastCls) { b.lastCls = c; b.clsSince = millis(); }
  if (c != C_UNKNOWN && millis() - b.clsSince >= CONFIRM_MS) b.cls = c;
}
bool stableFor(const Bay &b, Cls c, unsigned long ms) { return b.lastCls == c && millis() - b.clsSince >= ms; }

// =====================================================================
// 모터
// =====================================================================
void motorStopAll() {
  for (int i = 0; i < 2; i++) { digitalWrite(bay[i].en, LOW); digitalWrite(bay[i].rpwm, LOW); digitalWrite(bay[i].lpwm, LOW); }
}
unsigned long dutyUsed() {
  unsigned long now = millis(), sum = 0;
  for (int i = 0; i < RUNLOG; i++) if (runDur[i] && now - runStart[i] < DUTY_WINDOW_MS) sum += runDur[i];
  return sum;
}
// 끝까지 한 방향으로. stopWhenTaken이면 올리는 중 빈 통 2초에서 멈추고 true를 돌려준다.
bool motorMove(Bay &b, int dir, unsigned long maxMs, bool stopWhenTaken) {
  motorStopAll();
  digitalWrite(FAN_L, LOW); digitalWrite(FAN_R, LOW);
  delay(500);                                         // 모터 사이 0.5초
  digitalWrite(b.rpwm, dir > 0 ? HIGH : LOW);
  digitalWrite(b.lpwm, dir > 0 ? LOW : HIGH);
  digitalWrite(b.en, HIGH);
  unsigned long t0 = millis();
  bool taken = false;
  unsigned long emptySince = 0;
  Serial.printf("[모터] %c %s\n", b.name, dir > 0 ? "올림" : "내림");
  while (millis() - t0 < maxMs) {
    if (stopWhenTaken) {
      long d = median5(b);
      if (classify(b, d) == C_EMPTY) { if (!emptySince) emptySince = millis(); if (millis() - emptySince >= TAKEN_MS) { taken = true; break; } }
      else emptySince = 0;
    } else delay(50);
  }
  motorStopAll();
  unsigned long dur = millis() - t0;
  runStart[runIdx] = t0; runDur[runIdx] = dur; runIdx = (runIdx + 1) % RUNLOG;
  pirIgnoreUntil = max(pirIgnoreUntil, millis() + 3000);   // 모터 뒤 3초 PIR 무시
  b.lastCls = C_UNKNOWN; b.cls = C_UNKNOWN; b.clsSince = millis();   // 움직인 뒤에는 판정을 처음부터 (팬은 loop에서 다시 켬)
  Serial.printf("[모터] %c 정지 (%lu ms%s)\n", b.name, dur, taken ? ", 우산 꺼냄" : "");
  return taken;
}
void lowerBay(Bay &b) {
  drawScreen(S_DOWN, 0);
  motorMove(b, -1, MOVE_MS + 1000, false);
}
void liftBay(Bay &b, bool manual) {
  if (b.st == B_UP) return;                             // 이미 올라가 있음
  manual = manual && b.st != B_STORED;                  // 보관 중인 우산이면 자동처럼 꺼냄 감지 유지
  if (dutyUsed() > DUTY_MAX_MS) { Serial.println("[모터] 최근 10분 사용이 많아 쉼"); return; }
  for (int i = 0; i < 2; i++) if (bay[i].st == B_UP && &bay[i] != &b) { lowerBay(bay[i]); bay[i].st = bay[i].manual ? B_EMPTY : B_STORED; }  // 두 구멍을 동시에 올려 두지 않음
  drawScreen(S_LIFT, 0);
  bool taken = motorMove(b, +1, MOVE_MS + 1000, !manual && TAKEN_DETECT_UP);
  if (taken) {                                // 올리는 중 꺼냄 → 바로 내림
    lowerBay(b);
    b.st = B_EMPTY; b.fanUntil = 0;
    byeUntil = millis() + 4000;
  } else {
    b.st = B_UP; b.upAt = millis(); b.manual = manual;
  }
}

// =====================================================================
// 팬
// =====================================================================
void fansApply() {
  unsigned long now = millis();
  for (int i = 0; i < 2; i++) {
    Bay &b = bay[i];
    bool on = (b.st == B_STORED) && b.fanUntil && (long)(b.fanUntil - now) > 0 && b.lastCls != C_EMPTY;   // 판정이 흔들리는 동안 끔
    digitalWrite(b.fan, on ? HIGH : LOW);
  }
}

// =====================================================================
// 부팅: 내리고 빈 통 재기
// =====================================================================
void calibrate(Bay &b) {
  long v[20]; int n = 0, neg = 0;
  for (int i = 0; i < 20; i++) { long d = readMM(b.trig, b.echo); if (d < 0) neg++; else v[n++] = d; delay(40); }
  if (neg == 20) { b.st = B_ERR; Serial.printf("[센서] %c 20회 모두 -1 → 이 구멍 쉼\n", b.name); return; }
  for (int i = 1; i < n; i++) { long k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
  long m = v[n / 2];
  long ref = EMPTY_REF_MM[b.name == 'L' ? 0 : 1];
  if (b.st == B_ERR) b.st = B_EMPTY;
  b.base = ref > 0 ? ref : ((m >= 60 && m <= 140) ? m : DEFAULT_BASE_MM);
  if (m < b.base - PRESENT_DROP_MM || neg > 10) {   // 켤 때 이미 우산이 들어 있음
    b.st = B_STORED; b.storedAt = millis(); b.fanUntil = millis() + FAN_MS;
    Serial.printf("[센서] %c 켤 때 우산이 들어 있음 (%ld mm) → 보관으로 시작\n", b.name, m);
  }
  Serial.printf("[센서] %c 빈 통 기준 %ld mm, 지금 %ld mm (정상 85~100)\n", b.name, b.base, m);
  if (b.base < 60 || b.base > 140) Serial.printf("[경고] %c 빈 통 값이 이상함. 우산이 들어 있거나 센서 방향 확인\n", b.name);
}

// =====================================================================
// 시리얼 명령
// =====================================================================
String line;
void printStatus() {
  const char *nm[] = {"비어 있음", "보관", "올림", "센서 고장"};
  for (int i = 0; i < 2; i++) {
    Bay &b = bay[i];
    Serial.printf("%c: %s, 거리 %ld mm (기준 %ld), 팬 %s\n", b.name, nm[b.st], b.dist, b.base, digitalRead(b.fan) ? "켬" : "끔");
  }
  Serial.printf("PIR %d, 최근 10분 모터 %lu초\n", digitalRead(PIR_OUT), dutyUsed() / 1000);
}
void help() {
  Serial.println("명령: s 상태 | UL UR 올림 | DL DR 내림 | c 빈 통 다시 재기 (받침을 내리고 우산을 뺀 뒤) | ? 도움말");
}
void handleLine(String s) {
  s.trim(); s.toUpperCase();
  if (s == "S") printStatus();
  else if (s == "UL" || s == "UR") { Bay &b = bay[s[1] == 'L' ? 0 : 1]; if (b.st != B_ERR) liftBay(b, true); }
  else if (s == "DL" || s == "DR") { Bay &b = bay[s[1] == 'L' ? 0 : 1]; lowerBay(b); if (b.st == B_UP) { b.st = b.manual ? B_EMPTY : B_STORED; b.noLiftUntil = millis() + RELIFT_BLOCK_MS; } }
  else if (s == "C") { calibrate(bay[0]); calibrate(bay[1]); }
  else help();
}

// =====================================================================
void setup() {
  int outs[] = {L_RPWM, L_LPWM, L_EN, R_RPWM, R_LPWM, R_EN, USL_TRIG, USR_TRIG, FAN_L, FAN_R};
  for (int p : outs) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  pinMode(USL_ECHO, INPUT); pinMode(USR_ECHO, INPUT); pinMode(PIR_OUT, INPUT);
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
  tftInit();
  pinMode(FAN_L, OUTPUT); digitalWrite(FAN_L, LOW);   // SPI 설정 뒤 L 팬 핀을 다시 출력으로
  Serial.println("\n=== 스마트 우산통 REV D 자동 운전 ===");
  drawScreen(S_DOWN, 0);
  delay(1500);
  dfSend(0x06, DF_VOLUME);
  // 두 받침을 하나씩 끝까지 내림 (위치를 모르므로)
  for (int i = 0; i < 2; i++) motorMove(bay[i], -1, MOVE_MS * 3 / 2, false);
  delay(500);
  calibrate(bay[0]); calibrate(bay[1]);
  bootAt = millis();
  pirIgnoreUntil = bootAt + BOOT_PIR_IGNORE_MS;
  help();
}

void loop() {
  // 시리얼
  while (Serial.available()) { char c = Serial.read(); if (c == '\n' || c == '\r') { if (line.length()) handleLine(line); line = ""; } else line += c; }

  unsigned long now = millis();

  // 센서 (고장 아닌 구멍만)
  for (int i = 0; i < 2; i++) if (bay[i].st != B_ERR) { sense(bay[i]); delay(20); }

  // PIR: 0.5초 이상 1, 무시 구간 제외
  bool pirRaw = digitalRead(PIR_OUT);
  bool pirEvent = false;
  if (pirRaw) { if (!pirHighSince) pirHighSince = millis(); pirLowSince = 0; }
  else { pirHighSince = 0; if (!pirLowSince) pirLowSince = millis(); }
  bool ignoring = (long)(millis() - pirIgnoreUntil) <= 0;
  if (!pirRaw && !ignoring) pirArmed = true;            // 무시 구간 밖에서 0을 봐야 다음 1을 인정
  bool held = pirHighSince && millis() - pirHighSince >= PIR_HOLD_MS;
  if (pirArmed && held && !ignoring) { pirEvent = true; pirArmed = false; }
  if (pirLowSince && millis() - pirLowSince > 10000) voicedThisVisit = false;   // 10초 동안 사람이 없으면 새 손님

  // 구멍별 상태
  for (int i = 0; i < 2; i++) {
    Bay &b = bay[i];
    switch (b.st) {
      case B_EMPTY:
        if (b.cls == C_PRESENT) {
          b.st = B_STORED; b.storedAt = now; b.fanUntil = now + FAN_MS;
          Serial.printf("[보관] %c 우산 들어옴\n", b.name);
        }
        break;
      case B_STORED:
        if (b.cls == C_EMPTY) { b.st = B_EMPTY; b.fanUntil = 0; Serial.printf("[보관] %c 비었음\n", b.name); }
        break;
      case B_UP:
        if (!b.manual && TAKEN_DETECT_UP && stableFor(b, C_EMPTY, TAKEN_MS)) {   // 꺼내 감
          Serial.printf("[꺼냄] %c\n", b.name);
          lowerBay(b); b.st = B_EMPTY; b.fanUntil = 0; byeUntil = millis() + 4000;
        } else if (millis() - b.upAt > UP_TIMEOUT_MS) {  // 안 가져감
          Serial.printf("[시간 초과] %c 내림, 60초 쉼\n", b.name);
          lowerBay(b); b.st = b.manual ? B_EMPTY : B_STORED; b.noLiftUntil = millis() + RELIFT_BLOCK_MS;
        }
        break;
      default: break;
    }
  }

  // 사람이 다가오면: 안내 음성(한 번) + 먼저 넣은 우산 올리기
  if (pirEvent) {
    Serial.println("[PIR] 사람 감지");
    int pick = -1;
    bool anyUp = bay[0].st == B_UP || bay[1].st == B_UP;
    for (int i = 0; i < 2; i++) {
      Bay &b = bay[i];
      bool liftOk = !b.noLiftUntil || (long)(millis() - b.noLiftUntil) > 0;
      if (b.st == B_STORED && millis() - b.storedAt >= LIFT_AFTER_STORE_MS && liftOk)
        if (pick < 0 || millis() - b.storedAt > millis() - bay[pick].storedAt) pick = i;   // 더 오래된 것
    }
    if (!voicedThisVisit) { playVoice(1); voicedThisVisit = true; }
    if (pick >= 0 && !anyUp) {
      if (dutyUsed() > DUTY_MAX_MS) { drawScreen(S_WAIT, 0); waitShown = true; }
      else liftBay(bay[pick], false);
    }
  }

  fansApply();

  // 화면 고르기
  bool anyUp = false, anyStored = false; int errs = 0;
  for (int i = 0; i < 2; i++) { if (bay[i].st == B_UP) anyUp = true; if (bay[i].st == B_STORED) anyStored = true; if (bay[i].st == B_ERR) errs++; }
  int sig = bay[0].st * 4 + bay[1].st;
  if (waitShown && dutyUsed() <= DUTY_MAX_MS) waitShown = false;
  if (anyUp) drawScreen(S_LIFT, 0);
  else if (byeUntil && (long)(byeUntil - millis()) > 0) drawScreen(S_BYE, 0);
  else if (waitShown) drawScreen(S_WAIT, 0);
  else if (errs == 2) drawScreen(S_ERR, 0);
  else if (anyStored) drawScreen(S_STORE, sig);
  else drawScreen(S_IDLE, sig);

  delay(30);
}
