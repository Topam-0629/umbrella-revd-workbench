// 스마트 우산통 REV D — 마스크통 뚜껑 (아두이노 우노 단독)
// 버튼을 누르면 뚜껑이 열리고, 한 번 더 누르면 닫힌다.
// 필요: Arduino IDE. 보드: Arduino Uno. Servo 라이브러리는 IDE에 기본으로 들어 있다.
// 시리얼 모니터 9600, 줄 끝 "새 줄".
//
// 배선
//   서보 빨강 → 5V,  서보 갈색 → GND (5V 옆),  서보 주황 → 9번
//   버튼 선 하나 → 2번,  다른 선 → GND (13번 옆)   (저항 없음)
//
// 명령 (시리얼 모니터)
//   b : 버튼 확인 (10초 동안 누를 때마다 표시)
//   c : 가운데 90° (혼 달기 전에 한 번)
//   o / x : 열기 / 닫기
//   + / - : 지금 각도에서 2°씩
//   C / O : 지금 각도를 닫힘 / 열림으로 (전원 끄면 사라짐 → 숫자를 메모해서 아래에 넣기)
//   r : 10번 여닫기
//   명령을 안 보내면 버튼으로 열고 닫는다.

#include <Servo.h>

const int SERVO_PIN = 9;
const int BTN_PIN = 2;

int closedDeg = 90;   // 시험 T6에서 찾은 숫자로 바꾼다
int openDeg = 0;      // 시험 T7에서 찾은 숫자로 바꾼다

Servo lid;
int nowDeg = 90;
bool isOpen = false;

// 움직인 뒤 신호를 끊는다 → 서보가 힘을 빼서 떨림·소리가 없다
void servoGo(int deg, int ms) {
  deg = constrain(deg, 0, 180);
  lid.attach(SERVO_PIN);
  lid.write(deg);
  delay(ms);
  lid.detach();
  nowDeg = deg;
  Serial.print("서보 ");
  Serial.println(deg);
}

// 천천히 움직이기 (쾅 닫히지 않게)
void servoSlow(int to) {
  to = constrain(to, 0, 180);
  lid.attach(SERVO_PIN);
  while (nowDeg != to) {
    nowDeg += (to > nowDeg) ? 1 : -1;
    lid.write(nowDeg);
    delay(12);
  }
  delay(300);
  lid.detach();
  Serial.print("서보 ");
  Serial.println(nowDeg);
}

bool pressed() {
  if (digitalRead(BTN_PIN) != LOW) return false;
  delay(30);                       // 접점 튐 거르기
  return digitalRead(BTN_PIN) == LOW;
}

void waitRelease() {
  while (digitalRead(BTN_PIN) == LOW) delay(10);
  delay(30);
}

void menu() {
  Serial.println();
  Serial.println(F("== 마스크통 뚜껑 (우노) =="));
  Serial.print(F("닫힘 ")); Serial.print(closedDeg);
  Serial.print(F(", 열림 ")); Serial.print(openDeg);
  Serial.print(F(", 지금 ")); Serial.println(nowDeg);
  Serial.println(F("b 버튼 | c 가운데 | o 열기 | x 닫기 | + - 2도씩"));
  Serial.println(F("C 지금=닫힘 | O 지금=열림 | r 10번 여닫기"));
}

void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT_PULLUP);  // 안 누르면 HIGH, 누르면 LOW
  menu();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'b': {
        Serial.println(F("10초 동안 버튼을 눌러 보세요"));
        unsigned long t0 = millis();
        int n = 0;
        while (millis() - t0 < 10000UL) {
          if (pressed()) { n++; Serial.print(F("눌림 ")); Serial.println(n); waitRelease(); }
        }
        Serial.print(F("끝: ")); Serial.print(n); Serial.println(F("번 (0이면 배선 확인)"));
        break;
      }
      case 'c': servoGo(90, 1000); break;
      case 'o': isOpen = true;  servoSlow(openDeg); break;
      case 'x': isOpen = false; servoSlow(closedDeg); break;
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
  if (pressed()) {
    isOpen = !isOpen;
    Serial.println(isOpen ? F("열기") : F("닫기"));
    servoSlow(isOpen ? openDeg : closedDeg);
    waitRelease();
  }
}
