# Smart Tower 배선 · 설정 가이드 (Arduino Uno R4 WiFi)

화재·낙상·출입·환경 모니터링 스마트 교실 — 최종 배선과 설치·클라우드 연결 순서입니다.

```
[초음파·카메라·센서] → R4 WiFi (판정·제어) → LED·TFT·모터·서보·부저
                          ├─ USB ─→ 크롬 대시보드 (실시간·음성안내·시간표)
                          └─ WiFi ─→ 텔레그램 (휴대폰 알림·양방향)
```

> ⚠️ **중요(R4 특성)**: R4 WiFi에서 **A4/A5 = SDA/SCL(I2C)** 입니다(같은 핀). 그래서 A4/A5는 HuskyLens#1·BH1750용이고 다른 데 못 씁니다. 핀 부족으로 **물리 7세그먼트는 제외**(인원은 TFT·대시보드 표시), **긴급 표시는 R4 내장 LED 매트릭스**(핀 0개), **A3는 흰색 LED(교실 전등)** 로 씁니다.

---

## 1. 준비물

| 부품 | 용도 |
|---|---|
| **Arduino Uno R4 WiFi** | 메인 보드 (WiFi 내장) |
| HuskyLens ×2 | #1 화재(색종이 불) · #2 사람(서있음/쓰러짐) |
| 초음파 HC-SR04 ×2 | 문 출입 인원 카운트 |
| **DHT11** | 온도·습도 |
| MQ-2 | 연기·가스 |
| BH1750 | 조도 |
| 흰색 LED | 교실 전등 (사람+어두움 ON) (+ 220Ω) |
| (R4 내장 매트릭스) | 긴급 시 전체 빨강 점멸 (핀 0개) |
| 3.2" TFT (ILI9341 + XPT2046 터치) | 인원·환경 표시 + 도움요청 버튼 |
| DC모터+프로펠러 | 환기 (트랜지스터+다이오드) |
| SG90 서보 | 창문 |
| 능동 피에조 부저 | 화재 경보 |
| 별도 5V 전원 | **모터·서보용** |

---

## 2. 핀 배선표 ⭐

| 핀 | 연결 | 핀 | 연결 |
|---|---|---|---|
| D2 | 초음파1(바깥) Trig | D10 | TFT DC |
| D3 | 초음파1(바깥) Echo | D11 | SPI COPI/MOSI (공유) |
| D4 | 초음파2(안쪽) Trig | D12 | SPI CIPO/MISO (터치 T_DO) |
| D5 | 초음파2(안쪽) Echo | D13 | SPI SCK (공유) |
| D6 | **DHT11 데이터** | A0 | 터치 T_CS |
| D7 | 능동 부저 (+극) | A1 | TFT CS |
| D8 | DC모터(트랜지스터) | A2 | MQ-2 AOUT |
| D9 | 서보 신호 | A3 | **흰색 LED(교실 전등)** (220Ω) |
| D0/D1 | **Serial1** → HuskyLens#2 | **A4/A5** | **SDA/SCL** → HuskyLens#1 + BH1750 |

> TFT RST → 보드 RESET / 터치 T_IRQ → 안 함 / TFT LED(백라이트) → 3.3~5V
> ⚠️ **TFT의 SD_ 핀(SD_CS·SD_MOSI 등)은 연결하지 마세요** (SPI 충돌·하얀화면 원인)
> 터치 T_CLK→D13, T_DIN→D11, T_DO→D12 (화면과 SPI 공유)

---

## 3. 부품별 배선

**초음파 ×2** — 각 VCC→5V, GND→GND. **바깥=US1(D2/D3), 안쪽=US2(D4/D5)**. 밖→안=입장(+1), 안→밖=퇴장(−1).

**DHT11** — VCC→5V(또는 3.3V), GND→GND, **DATA→D6**. (3핀 모듈 기준)

**HuskyLens #1(화재)** — Protocol=**I2C**, **A4(SDA)/A5(SCL)**. BH1750과 버스 공유.
**HuskyLens #2(사람)** — Protocol=**Serial**, **TX→D0(RX), RX→D1(TX)**.
**BH1750** — VCC→3.3V, GND→GND, **A4/A5** (병렬).

**흰색 LED(교실 전등)** — **220Ω→A3**, 짧은다리→GND. (사람 있고 어두우면 자동 ON)
**TFT+터치 (FEIYANG 2.4" ILI9341)** — CS→A1, DC→D10, RST→보드RESET, SDI(MOSI)→D11, SDO(MISO)→D12, SCK→D13, LED→3.3~5V / 터치 T_CS→A0, T_DIN→D11, T_DO→D12, T_CLK→D13, T_IRQ→안함. **SD_ 핀은 연결 안 함.**

> ★ **하얀 화면 해결**: 이 모듈은 터치 CS가 뜨면 초기화가 깨져 하얀 화면이 돼요. 코드가 초기화 전에 터치 CS를 HIGH로 두어 방지합니다. 그래도 하얀 화면이면 (1) `tft.begin(24000000)` 처럼 속도 낮추기, (2) 코드 `#define TFT_RST -1`을 남는 핀 번호로 바꾸고 TFT RESET을 그 핀에 배선.
**DC모터** — D8→트랜지스터, 모터는 별도 5V, 역기전력 다이오드.
**서보** — 신호→D9, 별도 5V, GND 공통.
**MQ-2** — AOUT→A2 (예열).
**부저** — +→D7, −→GND.

> ⚠️ **모터·서보는 반드시 별도 5V**, GND만 공통. (보드 직결=과열·연기 위험)

---

## 4. 라이브러리 (라이브러리 관리)

- `HUSKYLENS` · `Adafruit GFX` · `Adafruit ILI9341` · `XPT2046_Touchscreen`
- `DHT sensor library` (Adafruit) + `Adafruit Unified Sensor`
- (WiFiS3는 R4 보드 패키지에 기본 포함)

**보드**: 툴 → 보드 → **Arduino Uno R4 WiFi** 선택

---

## 5. HuskyLens 학습

**#1 화재** — **Color Recognition(색 인식)**, **색종이 불의 색(빨강/주황)을 Learn** → 그 색이 보이면 화재. ⚠ 주변에 비슷한 색 물체 치우기.
**#2 사람** — Object Classification, **서있는 레고=obj1(ID1)**, **눕힌 레고=obj2(ID2)** → obj2가 5초 지속되면 낙상. (각도·거리별 여러 번 학습!)

---

## 6. 텔레그램 (휴대폰 알림 · 양방향)

1. 텔레그램에서 **@BotFather** → `/newbot` → **토큰** 받기
2. **@userinfobot** 에게 말 걸어 내 **Chat ID** 확인
3. 스케치 위쪽 4줄 채우기:
```cpp
const char* WIFI_SSID = "우리WiFi";
const char* WIFI_PASS = "비번";
const char* BOT_TOKEN = "토큰";
const char* CHAT_ID   = "ChatID";
```
- 화재·낙상·도움요청 시 **휴대폰으로 자동 메시지**. 관리자가 텔레그램에서 답하면 **양방향 소통**도 됩니다.
- 안 채우면 WiFi/텔레그램은 건너뛰고 **대시보드 알림만** 작동.

---

## 7. (선택) Arduino Cloud 연결 — 어디서나 대시보드 보기

텔레그램이 알림·소통을 담당하고, **Arduino Cloud는 센서값을 클라우드에 올려 폰/웹 어디서나 보기**용입니다.

1. [cloud.arduino.cc](https://cloud.arduino.cc) 로그인 → **IoT Cloud → Create Thing**
2. **Device 연결**: Uno R4 WiFi 등록 (Device ID/Secret Key 발급 — 잘 보관)
3. **Cloud Variables 추가**: 예) `temperature`(int), `humidity`(int), `gas`(int), `people`(int), `fireAlarm`(bool), `fallAlarm`(bool)
4. **Network 설정**: WiFi 이름·비번 + Secret Key 입력
5. Cloud가 **`thingProperties.h`** 를 자동 생성 → 스케치에 추가하고:
```cpp
#include "thingProperties.h"
// setup(): initProperties(); ArduinoCloud.begin(ArduinoIoTPreferredConnection);
// loop():  ArduinoCloud.update(); temperature=g_temp; humidity=g_humi; ... 로 값 대입
```
6. Cloud의 **Dashboards**에서 위젯(게이지·차트)으로 배치 → **Arduino IoT Remote 앱**(무료)으로 폰에서 확인

> 라이브러리 `ArduinoIoTCloud` 설치 필요. 채팅은 Cloud보다 **텔레그램**이 확실합니다.

---

## 8. 실행 순서

1. 라이브러리 설치 + 보드 **Uno R4 WiFi** 선택
2. HuskyLens 2개 학습 (6번 아님 5번)
3. (선택) 텔레그램 4줄 / Arduino Cloud 설정
4. **업로드**
5. **모터·서보 별도 5V** 확인 후 전원 인가
6. 대시보드(`safeclass-dashboard.html`)를 **크롬**으로 → **USB 연결** → 포트 선택
7. **터치 보정**(처음 1회): `CAL_MODE 1`로 좌표 확인 → `mapTouch` 값 조정 → `CAL_MODE 0`
8. **📷 웹캠 켜기**(실시간 영상+위치) · **🔊 음성 ON** · 시간표/알람 설정

---

## 9. 작동 요약

| 상황 | 대응 |
|---|---|
| 색종이 불(HuskyLens#1) 또는 MQ-2 高 | 🔥 부저경보 + 모터환기 + 빨강점멸 + 창문 + 대시보드 긴급 + **음성 "대피하세요"** + 텔레그램 |
| 낙상 5초 지속(HuskyLens#2 obj2) | 🚨 대시보드 관리자 알림 + **음성 "확인 필요"** + 텔레그램 |
| TFT HELP 터치 | 🆘 대시보드 알림 + 텔레그램 |
| 문 통과 | 실내 인원 ±1 → TFT·대시보드 |
| 사람 있고 어두움 | 조명 상태 ON(표시) |
| 알람 시각 | ⏰ **음성 "일어나세요"** |

---

## 10. 트러블슈팅

- **컴파일 A4/A5 충돌** → A4/A5는 I2C 전용. 다른 부품 물리지 말기(위 배선표대로)
- **TFT 하얀 화면** → (1) SD_ 핀 연결 안 했는지 (2) 코드가 터치 CS를 HIGH로 두는지(기본 포함) (3) `tft.begin(24000000)`으로 속도 낮추기 (4) 그래도면 `TFT_RST`를 남는 핀으로 바꾸고 RESET 배선
- **터치 위치 안 맞음/안 눌림** → **보정 모드**로: 코드 `#define CAL_MODE 0`을 **1**로 바꿔 업로드 → 화면 네 귀퉁이를 눌러 나오는 raw X/Y의 최소·최대값 확인 → `mapTouch()`의 `map(..,200,3900,..)` 숫자를 그 값으로 바꾸고 → `CAL_MODE` 다시 **0** 으로 업로드
- **DHT11 nan** → 배선·전원 확인, 첫 읽기 실패는 정상(2초 주기 재시도)
- **초음파 오작동** → 두 센서 간격 벌리고 `US_NEAR`(20cm) 조정
- **음성 안 나옴** → 크롬에서 페이지 클릭 후(사용자 상호작용) 작동, 🔊 ON 확인
- **보드 리셋/과열** → 모터·서보 별도 5V (제일 흔한 원인)

---

즐거운 제작 되세요! 막히면 어디서 멈췄는지(에러/증상) 알려주세요. 🙂
