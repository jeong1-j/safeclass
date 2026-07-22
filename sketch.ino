/* =========================================================
   SafeClass / Smart Tower — Arduino Uno R4 WiFi (최종)
   카메라 : HuskyLens ×2
     #1(화재) → I2C(A4/A5=SDA/SCL) : 색종이 불(obj1~4 전부 불) → 화재
     #2(사람) → UART(Serial1 D0/D1) : obj1=서있음, obj2=쓰러짐 → 5초 지속 시 도움요청
   출입   : 초음파 ×2 (문 안/밖) → 입장/퇴장 카운트
   센서   : MQ-2(연기·가스) + BH1750(조도) + DHT11(온·습도)
   출력   : KY-011 빨강 LED(긴급) · 3.2"TFT(+터치, 인원·환경 표시) · DC모터 · 서보 · 능동부저
   알림   : 대시보드(USB) + 텔레그램(WiFi) — 화재 / 쓰러짐 / 도움요청
   ⚠ R4에서 A4/A5 = I2C(SDA/SCL). 물리 7세그·KY-011 초록은 핀 부족으로 제외(값은 화면 표시)
   라이브러리: HUSKYLENS, Adafruit_GFX, Adafruit_ILI9341, XPT2046_Touchscreen,
              DHT sensor library, WiFiS3(내장)
   ========================================================= */
#include <Wire.h>
#include <SPI.h>
#include "HUSKYLENS.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "DHT.h"
#include <WiFiS3.h>
#include <WiFiSSLClient.h>

// ---------- 핀 ----------
#define US1_TRIG  2      // 초음파1(바깥) Trig
#define US1_ECHO  3      // 초음파1(바깥) Echo
#define US2_TRIG  4      // 초음파2(안쪽) Trig
#define US2_ECHO  5      // 초음파2(안쪽) Echo
#define DHTPIN    6      // DHT11 데이터
#define BUZZER    7      // 능동 피에조
#define MOTOR     8      // 환기 DC모터(트랜지스터) — 화재 시에만
#define SERVO_PIN 9      // 창문 서보
#define TFT_DC    10     // TFT DC
#define T_CS      A0     // 터치(XPT2046) CS
#define TFT_CS    A1     // TFT CS
#define MQ2       A2     // MQ-2 연기·가스
#define LED_R     A3     // KY-011 빨강 (긴급 점멸)
//  SPI: COPI=D11, CIPO=D12, SCK=D13 (TFT+터치 공유) / TFT RST→보드 RESET / T_IRQ→미연결
//  I2C: A4=SDA, A5=SCL → HuskyLens#1 + BH1750  /  Serial1(D0/D1): HuskyLens#2

// ---------- 설정 ----------
#define DHTTYPE      DHT11
#define LUX_DARK     100
#define GAS_BAD      400
#define GAS_SEVERE   700
#define US_NEAR      20     // cm — 이보다 가까우면 통과
#define FALL_HOLD    5000   // 쓰러짐 5초 지속 → 도움요청
#define FALL_ID      2      // HuskyLens#2에서 '쓰러짐(obj2)' ID
#define BUZZER_ACTIVE 1

// ---------- WiFi / 텔레그램 (본인 정보로 채우기) ----------
const char* WIFI_SSID = "여기에_WiFi이름";
const char* WIFI_PASS = "여기에_WiFi비번";
const char* BOT_TOKEN = "여기에_봇토큰";
const char* CHAT_ID   = "여기에_ChatID";
bool wifiOK=false;

HUSKYLENS huskyFire;     // #1 화재 (I2C)
HUSKYLENS huskyPeople;   // #2 사람 (UART)
Adafruit_ILI9341 tft(TFT_CS, TFT_DC);
XPT2046_Touchscreen ts(T_CS);
DHT dht(DHTPIN, DHTTYPE);

int   g_ppl=0, g_gas=0, g_level=0, cctvMode=0, g_inside=0, g_temp=0, g_humi=0;
float g_lux=0;
int   g_box[5][4];
bool  g_fan=false, g_win=true, g_alarm=false, g_light=false;
bool  g_camfire=false, g_fall=false, g_help=false, blinkOn=false;
unsigned long blinkT=0, helpT=0, fallStart=0, dhtT=0;
int   lastLevel=-1, lastPpl=-1, lastGas=-1, lastLux=-1, lastMode=-1, lastTH=-1;
bool  us1_prev=false, us2_prev=false;
unsigned long us1_t=0, us2_t=0;
bool  sentFire=false, sentFall=false, sentHelp=false;

// ===== BH1750 (라이브러리 없이) =====
#define BH1750_ADDR 0x23
void bh1750Begin(){ Wire.beginTransmission(BH1750_ADDR); Wire.write(0x10); Wire.endTransmission(); }
float bh1750Lux(){
  if (Wire.requestFrom(BH1750_ADDR, 2) != 2) return 0;
  uint8_t hi=Wire.read(), lo=Wire.read();
  return (((uint16_t)hi<<8)|lo)/1.2f;
}

// ===== 부저 / 서보 / LED =====
void buzzerOn(){  if(BUZZER_ACTIVE) digitalWrite(BUZZER,HIGH); else tone(BUZZER,2300); }
void buzzerOff(){ if(BUZZER_ACTIVE) digitalWrite(BUZZER,LOW);  else noTone(BUZZER); }
void servoWrite(int angle){
  int pulse=map(angle,0,180,500,2500);
  for(int i=0;i<20;i++){ digitalWrite(SERVO_PIN,HIGH); delayMicroseconds(pulse); digitalWrite(SERVO_PIN,LOW); delay(20); }
}
void setRed(bool on){ digitalWrite(LED_R,on); }

// ===== 초음파 =====
long usDist(int trig,int echo){
  digitalWrite(trig,LOW); delayMicroseconds(2);
  digitalWrite(trig,HIGH); delayMicroseconds(10); digitalWrite(trig,LOW);
  long dur=pulseIn(echo,HIGH,25000);
  if(dur==0) return 999;
  return dur*0.034/2;
}
void updateEntry(){
  bool d1 = usDist(US1_TRIG,US1_ECHO) < US_NEAR;   // 바깥
  bool d2 = usDist(US2_TRIG,US2_ECHO) < US_NEAR;   // 안쪽
  unsigned long now=millis();
  if(d1 && !us1_prev){ us1_t=now; if(us2_t && now-us2_t<1500){ if(g_inside>0)g_inside--; us2_t=0; } } // 안→밖 퇴장
  if(d2 && !us2_prev){ us2_t=now; if(us1_t && now-us1_t<1500){ g_inside++; us1_t=0; } }               // 밖→안 입장
  us1_prev=d1; us2_prev=d2;
}

// ===== HuskyLens =====
void readFire(){                      // #1: 불(obj 무엇이든) → 화재
  g_camfire=false;
  if(huskyFire.request()){
    while(huskyFire.available()){
      HUSKYLENSResult r=huskyFire.read();
      if(r.command==COMMAND_RETURN_BLOCK){ g_camfire=true; }
    }
  }
}
int readPeople(){                     // #2: 서있음/쓰러짐 + 위치
  int n=0; bool fall=false;
  if(huskyPeople.request()){
    while(huskyPeople.available() && n<5){
      HUSKYLENSResult r=huskyPeople.read();
      if(r.command==COMMAND_RETURN_BLOCK){
        if(r.ID==FALL_ID) fall=true;
        g_box[n][0]=r.xCenter; g_box[n][1]=r.yCenter; g_box[n][2]=r.width; g_box[n][3]=r.height; n++;
      }
    }
  }
  if(fall){ if(fallStart==0) fallStart=millis(); g_fall=(millis()-fallStart>=FALL_HOLD); }
  else { fallStart=0; g_fall=false; }
  return n;
}

int computeLevel(){ if(g_camfire || g_gas>=GAS_SEVERE) return 2; if(g_gas>=GAS_BAD) return 1; return 0; }
void applyOutputs(int lv){
  g_fan   = (lv>=1); digitalWrite(MOTOR, g_fan?HIGH:LOW);
  g_win   = (lv==0); servoWrite(g_win?90:0);
  g_alarm = (lv>=2); if(g_alarm) buzzerOn(); else buzzerOff();
}

// ===== 텔레그램 =====
void tgSend(String msg){
  if(!wifiOK) return;
  WiFiSSLClient c;
  if(c.connect("api.telegram.org",443)){
    String url="/bot"+String(BOT_TOKEN)+"/sendMessage?chat_id="+String(CHAT_ID)+"&text="+msg;
    c.print("GET "+url+" HTTP/1.1\r\nHost: api.telegram.org\r\nConnection: close\r\n\r\n");
    unsigned long t=millis(); while(c.connected() && millis()-t<3000){ while(c.available()) c.read(); }
    c.stop();
  }
}

// ===== TFT + 터치 =====
#define HELP_Y 262
const char* levelText(int lv){ return lv==2?"FIRE!":lv==1?"CAUTION":"SAFE"; }
uint16_t levelColor(int lv){ return lv==2?ILI9341_RED:lv==1?ILI9341_ORANGE:ILI9341_GREEN; }
const char* modeText(int m){ return m==2?"LOCK":m==1?"REC":"PRIVACY"; }
void drawRow(int y,const char* label){ tft.setTextColor(0x8410); tft.setTextSize(1); tft.setCursor(12,y); tft.print(label); }
void drawHelpBtn(bool active){
  tft.fillRect(0,HELP_Y,240,320-HELP_Y, active?0x07E0:0xF800);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2); tft.setCursor(30,HELP_Y+18); tft.print(active?"SENT!":"HELP / SOS");
}
void drawTFTStatic(){
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2); tft.setCursor(10,8); tft.print("Smart Tower");
  tft.setTextColor(0x8410); tft.setTextSize(1); tft.setCursor(10,30); tft.print("Classroom Monitoring");
  tft.drawFastHLine(0,46,240,0x4208);
  drawRow(112,"People:"); drawRow(138,"Temp:"); drawRow(164,"Humi:"); drawRow(190,"Gas:"); drawRow(216,"Status:");
  drawHelpBtn(false);
}
void updateTFT(){
  if(g_level!=lastLevel){ tft.fillRect(0,54,240,48,levelColor(g_level)); tft.setTextColor(ILI9341_WHITE); tft.setTextSize(3); tft.setCursor(14,66); tft.print(levelText(g_level)); lastLevel=g_level; }
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
  if(g_inside!=lastPpl){ tft.fillRect(110,108,120,18,ILI9341_BLACK); tft.setCursor(110,108); tft.print(g_inside); tft.print(" in"); lastPpl=g_inside; }
  if(g_temp*100+g_humi!=lastTH){ tft.fillRect(110,134,120,18,ILI9341_BLACK); tft.setCursor(110,134); tft.print(g_temp); tft.print("C");
    tft.fillRect(110,160,120,18,ILI9341_BLACK); tft.setCursor(110,160); tft.print(g_humi); tft.print("%"); lastTH=g_temp*100+g_humi; }
  if(g_gas!=lastGas){ tft.fillRect(110,186,120,18,ILI9341_BLACK); tft.setCursor(110,186); tft.print(g_gas); lastGas=g_gas; }
  tft.fillRect(110,214,130,16,ILI9341_BLACK); tft.setTextSize(1);
  tft.setTextColor(g_fall?ILI9341_RED:0x07E0); tft.setCursor(110,216); tft.print(g_fall?"FALL!":"ok");
}
bool touchHelp(){
  if(!ts.touched()) return false;
  TS_Point p=ts.getPoint();
  // 보정: 아래 줄 주석 해제 → HELP 누를 때 TY값 보고 map 숫자 조정
  // Serial.print("TX:");Serial.print(p.x);Serial.print(" TY:");Serial.println(p.y);
  int sy = map(p.y, 200, 3900, 0, 320);
  return (sy > HELP_Y-15);
}

// ===== USB / 대시보드 =====
String getState(){
  const char* lvStr = g_level==2?"emergency":g_level==1?"caution":"safe";
  String j="{\"ppl\":"; j+=g_ppl;
  j+=",\"inside\":"; j+=g_inside;
  j+=",\"box\":[";
  for(int i=0;i<g_ppl && i<5;i++){ j+="["; j+=g_box[i][0]; j+=","; j+=g_box[i][1]; j+=","; j+=g_box[i][2]; j+=","; j+=g_box[i][3]; j+="]"; if(i<g_ppl-1 && i<4) j+=","; }
  j+="]";
  j+=",\"lux\":";   j+=String(g_lux,0);
  j+=",\"air\":";   j+=g_gas;
  j+=",\"temp\":";  j+=g_temp;
  j+=",\"humi\":";  j+=g_humi;
  j+=",\"mode\":";  j+=cctvMode;
  j+=",\"level\":\"";j+=lvStr; j+="\"";
  j+=",\"fan\":";   j+=(g_fan?1:0);
  j+=",\"window\":";j+=(g_win?1:0);
  j+=",\"alarm\":"; j+=(g_alarm?1:0);
  j+=",\"light\":"; j+=(g_light?1:0);
  j+=",\"fall\":";  j+=(g_fall?1:0);
  j+=",\"help\":";  j+=(g_help?1:0);
  j+="}";
  return j;
}
int setActuator(String cmd){
  if      (cmd=="WIN_OPEN")  { servoWrite(90); g_win=true; }
  else if (cmd=="WIN_CLOSE") { servoWrite(0);  g_win=false; }
  else if (cmd=="FAN_ON")    { digitalWrite(MOTOR,HIGH); g_fan=true; }
  else if (cmd=="FAN_OFF")   { digitalWrite(MOTOR,LOW);  g_fan=false; }
  else if (cmd=="ALARM_ON")  { buzzerOn();  g_alarm=true; }
  else if (cmd=="ALARM_OFF") { buzzerOff(); g_alarm=false; }
  else if (cmd=="RESET_CNT") { g_inside=0; }
  return 1;
}

void setup(){
  Serial.begin(115200); Serial.setTimeout(20);
  pinMode(US1_TRIG,OUTPUT); pinMode(US1_ECHO,INPUT);
  pinMode(US2_TRIG,OUTPUT); pinMode(US2_ECHO,INPUT);
  pinMode(BUZZER,OUTPUT); pinMode(MOTOR,OUTPUT); pinMode(LED_R,OUTPUT);
  pinMode(SERVO_PIN,OUTPUT); servoWrite(90); g_win=true;

  Wire.begin();
  huskyFire.begin(Wire);
  huskyFire.writeAlgorithm(ALGORITHM_OBJECT_CLASSIFICATION);   // 불(obj1~4)
  Serial1.begin(9600);
  huskyPeople.begin(Serial1);
  huskyPeople.writeAlgorithm(ALGORITHM_OBJECT_CLASSIFICATION); // 서있음/쓰러짐
  bh1750Begin();
  dht.begin();
  tft.begin(); tft.setRotation(0); drawTFTStatic();
  ts.begin(); ts.setRotation(0);
  setRed(0);

  if(String(WIFI_SSID)!="여기에_WiFi이름"){
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for(int i=0;i<20 && WiFi.status()!=WL_CONNECTED;i++) delay(500);
    wifiOK = (WiFi.status()==WL_CONNECTED);
  }
}

void loop(){
  // 1) 카메라·센서·출입
  readFire();
  g_ppl = readPeople();
  updateEntry();
  g_lux = bh1750Lux();
  g_gas = analogRead(MQ2);
  if(millis()-dhtT>2000){ dhtT=millis(); float t=dht.readTemperature(), h=dht.readHumidity();
    if(!isnan(t)) g_temp=(int)t; if(!isnan(h)) g_humi=(int)h; }

  // 2) TFT 터치 → 도움 요청
  if(touchHelp()){ if(!g_help){ g_help=true; drawHelpBtn(true); } helpT=millis(); }
  if(g_help && millis()-helpT>3000){ g_help=false; drawHelpBtn(false); }

  // 3) 판정
  int lv=computeLevel();
  if(lv!=g_level){ g_level=lv; applyOutputs(lv); }

  // 4) 긴급 빨강 LED
  if(lv==2){ if(millis()-blinkT>400){blinkOn=!blinkOn;blinkT=millis();} setRed(blinkOn); }
  else setRed(0);
  g_light = ((g_ppl>0)||(g_inside>0)) && (g_lux<LUX_DARK) && lv==0;   // 조명 상태(대시보드 표시)

  // 5) TFT 요약
  updateTFT();

  // 6) 텔레그램 알림 (각 1회)
  if(lv==2 && !sentFire){ tgSend("SmartTower:%20FIRE%20화재%20발생%20-%20대피%20안내"); sentFire=true; } if(lv<2) sentFire=false;
  if(g_fall && !sentFall){ tgSend("SmartTower:%20FALL%20낙상%20감지%20-%20구조%20필요"); sentFall=true; } if(!g_fall) sentFall=false;
  if(g_help && !sentHelp){ tgSend("SmartTower:%20HELP%20도움%20요청"); sentHelp=true; } if(!g_help) sentHelp=false;

  // 7) USB 시리얼
  if(Serial.available()){ String c=Serial.readStringUntil('\n'); c.trim(); if(c.length()) setActuator(c); }
  static unsigned long lastTx=0;
  if(millis()-lastTx>500){ lastTx=millis(); Serial.println(getState()); }

  delay(60);
}
