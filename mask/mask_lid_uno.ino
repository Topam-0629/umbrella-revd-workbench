// 스마트 우산통 REV D — 마스크통 자동 뚜껑 (아두이노 우노 단독)
// 앞면 거리 센서 앞 15 cm 안에 손이 오면 뚜껑이 열리고, 손이 사라지고 5초 뒤 닫힌다.
// 버튼 없음, 납땜 없음.
// 필요: Arduino IDE. 보드: Arduino Uno. Servo 라이브러리는 IDE에 기본으로 들어 있다.
// 시리얼 모니터 9600, 줄 끝 "새 줄".
//
// 배선 (전부 듀퐁선 꽂기)
//   서보 빨강 → 5V,  갈색 → GND (5V 옆),  주황 → 9번
//   거리 센서 VCC → 5번 (코드가 5V를 켜 준다),  Trig → 6번,  Echo → 7번,  GND → GND (13번 옆)
//
// 시험 명령 (시리얼 모니터)
//   d : 거리 보기 (10초)
//   c : 가운데 90° (혼 달기 전에 한 번)
//   o / x : 열기 / 닫기
//   + / - : 지금 각도에서 2°씩
//   C / O : 지금 각도를 닫힘 / 열림으로 (전원 끄면 사라짐 → 숫자를 메모해서 아래에 넣기)
//   r : 10번 여닫기
//   명령을 안 보내면 자동으로 움직인다.

#include <Servo.h>

const int SERVO_PIN = 9;
const int US_VCC = 5;      // 센서 전원 (센서는 15 mA라 핀으로 충분)
const int US_TRIG = 6;
const int US_ECHO = 7;

int closedDeg = 90;        // 시험 T6에서 찾은 숫자로 바꾼다
int openDeg = 0;           // 시험 T7에서 찾은 숫자로 바꾼다
int OPEN_CM = 15;          // 이 거리보다 가까우면 연다
unsigned long CLOSE_MS = 5000;   // 손이 사라지고 이만큼 지나면 닫는다

Servo lid;
int nowDeg = 90;
bool isOpen = false;
unsigned long lastSeen = 0;
byte nearCount = 0;

void servoGo(int deg, int ms) {
  deg = constrain(deg, 0, 180);
  lid.attach(SERVO_PIN);
  lid.write(deg);
  delay(ms);
  lid.detach();            // 힘 빼기 → 떨림·소리 없음
  nowDeg = deg;
  Serial.print(F("서보 ")); Serial.println(deg);
}

void servoSlow(int to) {   // 천천히 (쾅 닫히지 않게)
  to = constrain(to, 0, 180);
  lid.attach(SERVO_PIN);
  while (nowDeg != to) {
    nowDeg += (to > nowDeg) ? 1 : -1;
    lid.write(nowDeg);
    delay(12);
  }
  delay(300);
  lid.detach();
  Serial.print(F("서보 ")); Serial.println(nowDeg);
}

// 거리(cm). 아무것도 없거나 너무 멀면 999
int readCm() {
  digitalWrite(US_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(US_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);
  unsigned long us = pulseIn(US_ECHO, HIGH, 25000UL);
  if (us == 0) return 999;
  return (int)(us / 58UL);
}

void openLid()  { isOpen = true;  Serial.println(F("열기")); servoSlow(openDeg); }
void closeLid() { isOpen = false; Serial.println(F("닫기")); servoSlow(closedDeg); }

void menu() {
  Serial.println();
  Serial.println(F("== 마스크통 자동 뚜껑 (우노) =="));
  Serial.print(F("닫힘 ")); Serial.print(closedDeg);
  Serial.print(F(", 열림 ")); Serial.print(openDeg);
  Serial.print(F(", 여는 거리 ")); Serial.print(OPEN_CM); Serial.println(F(" cm"));
  Serial.println(F("d 거리 | c 가운데 | o 열기 | x 닫기 | + - 2도씩"));
  Serial.println(F("C 지금=닫힘 | O 지금=열림 | r 10번 여닫기"));
}

void setup() {
  Serial.begin(9600);
  pinMode(US_VCC, OUTPUT); digitalWrite(US_VCC, HIGH);
  pinMode(US_TRIG, OUTPUT); digitalWrite(US_TRIG, LOW);
  pinMode(US_ECHO, INPUT);
  delay(300);
  menu();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'd': {
        Serial.println(F("10초 동안 센서 앞에 손을 가까이, 멀리 해 보세요"));
        unsigned long t0 = millis();
        while (millis() - t0 < 10000UL) {
          int cm = readCm();
          if (cm == 999) Serial.println(F("없음"));
          else { Serial.print(cm); Serial.println(F(" cm")); }
          delay(300);
        }
        break;
      }
      case 'c': servoGo(90, 1000); break;
      case 'o': openLid(); lastSeen = millis(); break;
      case 'x': closeLid(); break;
      case '+': servoGo(nowDeg + 2, 300); break;
      case '-': servoGo(nowDeg - 2, 300); break;
      case 'C': closedDeg = nowDeg; Serial.print(F("닫힘 = ")); Serial.println(closedDeg); break;
      case 'O': openDeg = nowDeg;   Serial.print(F("열림 = ")); Serial.println(openDeg); break;
      case 'r':
        for (int i = 1; i <= 10; i++) {
          Serial.print(i); Serial.println(F("번째"));
          servoSlow(openDeg);  delay(800);
          servoSlow(closedDeg); delay(800);
        }
        isOpen = false;
        Serial.println(F("10번 끝. 우노가 다시 켜지지 않았으면 통과"));
        break;
      case '\n': case '\r': break;
      default: menu();
    }
  }

  // 자동: 두 번 연속 가까우면 열고, 5초 동안 아무것도 없으면 닫는다
  int cm = readCm();
  bool near = (cm < OPEN_CM);
  nearCount = near ? (nearCount < 2 ? nearCount + 1 : 2) : 0;
  if (nearCount >= 2) {
    lastSeen = millis();
    if (!isOpen) openLid();
  }
  if (isOpen && millis() - lastSeen > CLOSE_MS) closeLid();
  delay(60);
}
