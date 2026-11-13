// 스마트 우산통 REV D — 마스크통 뚜껑 시험 코드 (서보 + 버튼)
// 우산통 ESP32에 그대로 올려 쓴다. 우산통 다른 핀은 건드리지 않는다.
// 필요: Arduino IDE + ESP32 보드 패키지 (추가 라이브러리 없음). 보드: ESP32 Dev Module.
// 시리얼 모니터 115200, 줄 끝 "새 줄".
//
// 배선
//   서보 주황(신호) → ESP32 GPIO15
//   서보 빨강(5V)   → LM-B 출력 + 나사 (소리 모듈 선과 함께)
//   서보 갈색(접지) → LM-B 출력 − 나사 (소리 모듈 선과 함께)
//   버튼 다리 하나  → ESP32 GPIO12,  다른 다리 → L3 셋째 구멍 (저항 없음)
//
// 명령
//   b : 버튼 확인 (10초 동안 누를 때마다 표시)
//   c : 가운데 90° (혼 달기 전에 한 번)
//   o / x : 열기 / 닫기 (지금 저장된 각도로)
//   + / - : 지금 각도에서 2°씩 더/덜 (닫힘·열림 각도 찾기)
//   C / O : 지금 각도를 닫힘 / 열림 각도로 기억 (전원 끄면 사라짐 → 숫자를 메모)
//   r : 10번 여닫기 (버튼 없이)
//   아무 명령도 안 할 때는 버튼으로 열고 닫는다.

const int SERVO_PIN = 15;
const int BTN_PIN = 12;      // 켤 때 HIGH면 안 되는 핀. 버튼은 접지로만 이어지므로 안전.

int closedDeg = 90;          // 실물에서 C로 찾아서 여기 숫자를 바꾼다
int openDeg = 0;             // 실물에서 O로 찾아서 여기 숫자를 바꾼다
int nowDeg = 90;
bool isOpen = false;

// 라이브러리 없이 50 Hz 펄스를 보낸다. 0° = 500 µs, 180° = 2400 µs.
// 다 보내면 신호를 끊는다 → 서보가 힘을 빼서 떨림·소리가 없다.
void servoGo(int deg, int ms = 700) {
  deg = constrain(deg, 0, 180);
  int us = map(deg, 0, 180, 500, 2400);
  for (int t = 0; t < ms; t += 20) {
    digitalWrite(SERVO_PIN, HIGH);
    delayMicroseconds(us);
    digitalWrite(SERVO_PIN, LOW);
    delayMicroseconds(20000 - us);
  }
  nowDeg = deg;
  Serial.printf("서보 %d°\n", deg);
}

// 천천히 움직이기 (뚜껑이 쾅 닫히지 않게)
void servoSlow(int to) {
  int step = to > nowDeg ? 2 : -2;
  while (abs(to - nowDeg) > 2) servoGo(nowDeg + step, 40);
  servoGo(to, 500);
}

bool pressed() {
  if (digitalRead(BTN_PIN) != LOW) return false;
  delay(30);                                   // 접점 튐 거르기
  return digitalRead(BTN_PIN) == LOW;
}

void toggleLid() {
  isOpen = !isOpen;
  Serial.println(isOpen ? "열기" : "닫기");
  servoSlow(isOpen ? openDeg : closedDeg);
}

void menu() {
  Serial.println();
  Serial.println("== 마스크통 뚜껑 시험 ==");
  Serial.printf("닫힘 %d°, 열림 %d°, 지금 %d°\n", closedDeg, openDeg, nowDeg);
  Serial.println("b 버튼 확인 | c 가운데 90° | o 열기 | x 닫기 | + / - 2°씩");
  Serial.println("C 지금 각도=닫힘 | O 지금 각도=열림 | r 10번 여닫기");
  Serial.println("그 밖에는 버튼으로 열고 닫는다");
}

void setup() {
  Serial.begin(115200);
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  pinMode(BTN_PIN, INPUT_PULLUP);              // 안 누르면 HIGH, 누르면 LOW
  delay(500);
  menu();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'b': {
        Serial.println("10초 동안 버튼을 눌러 보세요");
        unsigned long t0 = millis(); int n = 0;
        while (millis() - t0 < 10000) {
          if (pressed()) { Serial.printf("눌림 %d\n", ++n); while (digitalRead(BTN_PIN) == LOW) delay(10); }
        }
        Serial.printf("끝: %d번 눌림 (0이면 배선 확인)\n", n);
        break;
      }
      case 'c': servoGo(90, 1000); break;
      case 'o': isOpen = true;  servoSlow(openDeg); break;
      case 'x': isOpen = false; servoSlow(closedDeg); break;
      case '+': servoGo(nowDeg + 2, 300); break;
      case '-': servoGo(nowDeg - 2, 300); break;
      case 'C': closedDeg = nowDeg; Serial.printf("닫힘 = %d° (메모하세요)\n", closedDeg); break;
      case 'O': openDeg = nowDeg;   Serial.printf("열림 = %d° (메모하세요)\n", openDeg); break;
      case 'r':
        for (int i = 1; i <= 10; i++) {
          Serial.printf("%d번째\n", i);
          servoSlow(openDeg); delay(800);
          servoSlow(closedDeg); delay(800);
        }
        isOpen = false;
        Serial.println("10번 끝. ESP32가 다시 켜지지 않았으면 통과");
        break;
      case '\n': case '\r': break;
      default: menu();
    }
  }
  if (pressed()) {
    toggleLid();
    while (digitalRead(BTN_PIN) == LOW) delay(10);   // 손 뗄 때까지
  }
}
