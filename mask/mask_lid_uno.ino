// 스마트 우산통 REV D — 마스크통 자동 뚜껑 (아두이노 우노 + 서보만)
// 전원을 켜면 뚜껑이 저절로 열렸다 닫혔다를 반복한다. 센서·버튼·납땜 없음.
// 필요: Arduino IDE. 보드: Arduino Uno. Servo 라이브러리는 IDE에 기본으로 들어 있다.
// 시리얼 모니터 9600, 줄 끝 "새 줄".
//
// 배선 (듀퐁선 수-수 3가닥)
//   서보 빨강 → 5V,  갈색 → GND (5V 옆),  주황 → 9번
//
// 시험 명령 (시리얼 모니터)
//   s : 자동 멈춤 / 다시 시작
//   c : 가운데 90° (혼 달기 전에 한 번)
//   o / x : 열기 / 닫기
//   + / - : 지금 각도에서 2°씩
//   C / O : 지금 각도를 닫힘 / 열림으로 저장 (우노 안에 저장돼서 전원을 꺼도 남는다)
//   R : 저장한 각도 지우고 처음 값(90 / 0)으로

#include <Servo.h>
#include <EEPROM.h>

const int SERVO_PIN = 9;

int closedDeg = 90;               // 시험에서 찾은 숫자로 바꾼다
int openDeg = 0;                  // 시험에서 찾은 숫자로 바꾼다
unsigned long OPEN_MS = 5000;     // 열려 있는 시간
unsigned long CLOSED_MS = 5000;   // 닫혀 있는 시간

Servo lid;
int nowDeg = 90;
bool isOpen = false;
bool autoOn = true;               // 켜면 바로 자동 반복. 멈추려면 시리얼 모니터에 s
unsigned long tMark = 0;

void servoGo(int deg, int ms) {
  deg = constrain(deg, 0, 180);
  lid.attach(SERVO_PIN);
  lid.write(deg);
  delay(ms);
  lid.detach();                   // 힘 빼기 → 떨림·소리 없음
  nowDeg = deg;
  Serial.print(F("서보 ")); Serial.println(deg);
}

void servoSlow(int to) {          // 천천히 (쾅 닫히지 않게)
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

void openLid()  { isOpen = true;  Serial.println(F("열기")); servoSlow(openDeg);   tMark = millis(); }
void closeLid() { isOpen = false; Serial.println(F("닫기")); servoSlow(closedDeg); tMark = millis(); }

void saveDeg() { EEPROM.update(0, 0xA5); EEPROM.update(1, closedDeg); EEPROM.update(2, openDeg); }
void loadDeg() { if (EEPROM.read(0) == 0xA5) { closedDeg = EEPROM.read(1); openDeg = EEPROM.read(2); } }

void menu() {
  Serial.println();
  Serial.println(F("== 마스크통 자동 뚜껑 (우노) =="));
  Serial.print(F("닫힘 ")); Serial.print(closedDeg);
  Serial.print(F(", 열림 ")); Serial.print(openDeg);
  Serial.print(F(", 자동 ")); Serial.println(autoOn ? F("켜짐") : F("멈춤"));
  Serial.println(F("s 자동 켜기/멈춤 | c 가운데 | o 열기 | x 닫기 | + - 2도씩"));
  Serial.println(F("C 지금=닫힘 저장 | O 지금=열림 저장 | R 처음 값"));
}

void setup() {
  Serial.begin(9600);
  delay(300);
  loadDeg();
  menu();
  servoGo(closedDeg, 800);       // 켜자마자 한 번 움직여서 살아 있는지 보여 준다
  tMark = millis();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's': autoOn = !autoOn; Serial.println(autoOn ? F("자동 켜짐") : F("자동 멈춤")); tMark = millis(); break;
      case 'c': servoGo(90, 1000); break;
      case 'o': openLid(); break;
      case 'x': closeLid(); break;
      case '+': servoGo(nowDeg + 2, 300); break;
      case '-': servoGo(nowDeg - 2, 300); break;
      case 'C': closedDeg = nowDeg; saveDeg(); Serial.print(F("닫힘 저장 = ")); Serial.println(closedDeg); break;
      case 'O': openDeg = nowDeg;   saveDeg(); Serial.print(F("열림 저장 = ")); Serial.println(openDeg); break;
      case 'R': closedDeg = 90; openDeg = 0; EEPROM.update(0, 0); Serial.println(F("처음 값으로")); break;
      case '\n': case '\r': break;
      default: menu();
    }
  }
  if (autoOn) {
    if (isOpen && millis() - tMark > OPEN_MS) closeLid();
    else if (!isOpen && millis() - tMark > CLOSED_MS) openLid();
  }
}
