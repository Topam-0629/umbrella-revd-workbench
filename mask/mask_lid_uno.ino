// 스마트 우산통 REV D — 마스크통 자동 뚜껑 (아두이노 우노 + 서보만)
// 전원을 켜면 서보가 0° ↔ 180°를 끝까지 천천히 오가며 계속 반복한다.
// 배선: 서보 빨강 → 5V,  갈색 → GND (5V 옆),  주황 → 9번
// 보드: Arduino Uno. 추가 라이브러리 없음.

#include <Servo.h>

const int SERVO_PIN = 9;
const int ANGLE_A = 0;                 // 한쪽 끝
const int ANGLE_B = 180;               // 다른 쪽 끝
const unsigned long WAIT_MS = 5000;    // 끝에서 멈춰 있는 시간 (1000 = 1초)
const int STEP_MS = 10;                // 1°씩 움직이는 간격. 크면 느리게, 작으면 빠르게

Servo lid;
int nowDeg = ANGLE_A;

void moveTo(int to) {
  lid.attach(SERVO_PIN);
  while (nowDeg != to) {
    nowDeg += (to > nowDeg) ? 1 : -1;
    lid.write(nowDeg);
    delay(STEP_MS);
  }
  delay(300);
  lid.detach();                        // 멈춰 있을 때 힘 빼기 → 떨림·소리 없음
}

void setup() {
  lid.attach(SERVO_PIN);
  lid.write(ANGLE_A);                  // 켜면 먼저 한쪽 끝으로
  delay(1000);
  lid.detach();
}

void loop() {
  delay(WAIT_MS);
  moveTo(ANGLE_B);
  delay(WAIT_MS);
  moveTo(ANGLE_A);
}
