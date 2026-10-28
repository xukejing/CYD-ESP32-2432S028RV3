#include <LovyanGFX.hpp>
#include "CYD-ESP32-2432S028RV3.h" 

// 实例化屏幕对象
CYD_ESP32_2432S028RV3 tft;

// ================== 颜色定义 ==================
#define COLOR_BG        TFT_BLACK
#define COLOR_PANEL     0x18E3      // 深灰蓝背景
#define COLOR_BORDER    0x5AEB      // 边框灰
#define COLOR_TEXT      TFT_WHITE
#define COLOR_LABEL     0x8410      // 标签灰
#define COLOR_GREEN     TFT_GREEN
#define COLOR_RED       TFT_RED
#define COLOR_YELLOW    TFT_YELLOW
#define COLOR_CYAN      TFT_CYAN
#define COLOR_PURPLE    0xF81F
#define COLOR_RPM    0x5ff
#define COLOR_TC    0x5ff
#define COLOR_ABS    0xff09
#define COLOR_BB    0xf800
#define COLOR_MAP    0x7e0
#define COLOR_FUEL    TFT_WHITE

// ================== 模拟数据变量 ==================
float speed = 0;
int gear = 1;           // 0 = N, 1~8 = 前进档
int rpm_percent = 0;    // 0~100 百分比 (用于转速条)
int engine_rpm = 0;     // 真实转速值 (如 0~8000)
float tireFL = 27.6, tireFR = 27.8, tireRL = 27.7, tireRR = 27.9;
unsigned long lapTime = 0;
unsigned long bestLapTime = 85000; // 初始最快圈 1:25.000
float fuel = 46.0;
int laps = 17;
int tc = 2, abs_val = 3;
float bb = 62.2;
int map_val = 1;        // 新增 MAP 变量

unsigned long lastUpdate = 0;

// ================== 函数声明 ==================
void drawStaticUI();
void updateData();
void updateUI();

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);        // 横屏
  tft.setBrightness(128);    // 亮度 0-255
  tft.fillScreen(COLOR_BG);
  
  drawStaticUI();
}

void loop() {
  // 20Hz 刷新率，保证数据流畅且不闪烁
  if (millis() - lastUpdate > 50) { 
    lastUpdate = millis();
    updateData();
    updateUI();
  }
}

// ================== 绘制静态界面 ==================
void drawStaticUI() {
  // 1. 左侧轮胎面板
  tft.fillRoundRect(2, 2, 80, 110, 4, COLOR_PANEL);
  tft.drawRoundRect(2, 2, 80, 110, 4, COLOR_BORDER);
  tft.setTextDatum(top_center);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("TIRES", 42, 6);
  
  tft.setTextDatum(top_left);
  tft.drawString("FL", 10, 25);
  tft.drawString("FR", 50, 25);
  tft.drawString("RL", 10, 65);
  tft.drawString("RR", 50, 65);

  // 2. 右侧圈速面板
  tft.fillRoundRect(238, 2, 80, 110, 4, COLOR_PANEL);
  tft.drawRoundRect(238, 2, 80, 110, 4, COLOR_BORDER);
  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("LAP TIMES", 278, 6);
  
  tft.setTextDatum(top_left);
  tft.drawString("CUR", 244, 25);
  tft.drawString("BEST", 244, 65);

  // 3. 底部转速条背景
  tft.fillRect(10, 168, 300, 10, COLOR_PANEL);

  // 4. 底部信息面板
  tft.fillRoundRect(2, 184, 316, 54, 4, COLOR_PANEL);
  tft.drawRoundRect(2, 184, 316, 54, 4, COLOR_BORDER);
  
  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  
  // 重新分配底部面板的X轴位置
	tft.setTextColor(COLOR_TC, COLOR_PANEL);
  tft.drawString("TC", 20, 190);
	tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  tft.drawString("ABS", 70, 190);
	tft.setTextColor(COLOR_BB, COLOR_PANEL);
  tft.drawString("BB", 120, 190);
	tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  tft.drawString("MAP", 170, 190); // 新增 MAP
	tft.setTextColor(COLOR_FUEL, COLOR_PANEL);
  tft.drawString("FUEL", 245, 190);
  tft.drawString("LAPS", 300, 190);
  
  // 5. 速度单位
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.drawString("km/h", 160, 155);
}

// ================== 模拟数据生成 ==================
void updateData() {
  float t = millis() / 1000.0;
  
  // 模拟速度平滑变化 (0 - 245 km/h)
  speed = 125 + 120 * sin(t * 0.5);
  if (speed < 0) speed = 0;
  
  // 根据速度计算档位
  if (speed < 1) gear = 0;
  else if (speed < 30) gear = 1;
  else if (speed < 60) gear = 2;
  else if (speed < 90) gear = 3;
  else if (speed < 120) gear = 4;
  else if (speed < 150) gear = 5;
  else if (speed < 180) gear = 6;
  else if (speed < 220) gear = 7;
  else gear = 8;

  // 模拟转速百分比 (根据档位和速度计算)
  if (gear == 0) {
    rpm_percent = 10 + 5 * sin(t * 5); // 空挡怠速抖动
  } else {
    float baseSpeed = (gear - 1) * 30.0;
    float gearRange = 30.0;
    float progress = (speed - baseSpeed) / gearRange;
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    rpm_percent = 20 + progress * 80; // 20% - 100%
  }

  // 计算实际转速值 (假设最高 8000 RPM)
  engine_rpm = (rpm_percent * 8000) / 100;
  
  // 轮胎温度微小波动
  tireFL = 27.6 + sin(t * 0.1) * 1.5;
  tireFR = 27.8 + cos(t * 0.1) * 1.5;
  tireRL = 27.7 + sin(t * 0.15) * 1.5;
  tireRR = 27.9 + cos(t * 0.15) * 1.5;
  
  // 圈速累加
  lapTime += 50;
  if (lapTime > 90000) { 
    if (bestLapTime == 0 || lapTime < bestLapTime) {
      bestLapTime = lapTime;
    }
    lapTime = 0;
    laps--;
    fuel -= 2.5;
    if (fuel < 0) fuel = 0;
    if (laps < 0) laps = 0;
  }
}

// ================== 刷新动态界面 ==================
void updateUI() {
  char buf[20];
  
  // 1. 更新档位（位置上移至 Y=35）
  tft.fillRect(110, 5, 100, 60, COLOR_BG); 
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::Font6);
  if (gear == 0) {
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.drawString("N", 160, 35);
  } else {
    tft.setTextColor(COLOR_YELLOW, COLOR_BG);
    sprintf(buf, "%d", gear);
    tft.drawString(buf, 160, 35);
  }

  // 2. 新增：档位下方的发动机转速小字 (Font0)
  tft.setFont(&fonts::Font0);
  tft.setTextColor(COLOR_RPM, COLOR_BG);
  sprintf(buf, "%d RPM", engine_rpm);
  tft.drawString(buf, 160, 65);

  // 3. 更新速度
  tft.fillRect(90, 95, 140, 45, COLOR_BG);
  tft.setFont(&fonts::Font6);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  sprintf(buf, "%.0f", speed);
  tft.drawString(buf, 160, 120);

  // 4. 更新转速条
  tft.fillRect(10, 168, 300, 10, COLOR_PANEL);
  int w = (rpm_percent * 300) / 100;
  if (w > 0) {
    if (rpm_percent <= 60) {
      tft.fillRect(10, 168, w, 10, COLOR_GREEN);
    } else if (rpm_percent <= 85) {
      tft.fillRect(10, 168, (60 * 300) / 100, 10, COLOR_GREEN);
      tft.fillRect(10 + (60 * 300) / 100, 168, w - (60 * 300) / 100, 10, COLOR_YELLOW);
    } else {
      tft.fillRect(10, 168, (60 * 300) / 100, 10, COLOR_GREEN);
      tft.fillRect(10 + (60 * 300) / 100, 168, ((85 - 60) * 300) / 100, 10, COLOR_YELLOW);
      tft.fillRect(10 + (85 * 300) / 100, 168, w - (85 * 300) / 100, 10, COLOR_RED);
    }
  }

  // 5. 更新轮胎温度
  tft.setFont(&fonts::Font2);
  tft.setTextDatum(top_left);
  auto drawTire = [](int x, int y, float temp) {
    uint16_t color = COLOR_TEXT;
    if (temp > 30.0) color = COLOR_RED;
    else if (temp > 28.0) color = COLOR_YELLOW;
    tft.fillRect(x, y, 35, 20, COLOR_PANEL);
    tft.setTextColor(color, COLOR_PANEL);
    char b[10];
    sprintf(b, "%.1f", temp);
    tft.drawString(b, x, y);
  };

  drawTire(10, 40, tireFL);
  drawTire(50, 40, tireFR);
  drawTire(10, 80, tireRL);
  drawTire(50, 80, tireRR);

  // 6. 更新圈速
  tft.fillRect(244, 40, 75, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_CYAN, COLOR_PANEL);
  int min = lapTime / 60000;
  int sec = (lapTime % 60000) / 1000;
  int ms = lapTime % 1000;
  sprintf(buf, "%02d:%02d.%03d", min, sec, ms);
  tft.drawString(buf, 244, 40);

  tft.fillRect(244, 80, 75, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_PURPLE, COLOR_PANEL);
  min = bestLapTime / 60000;
  sec = (bestLapTime % 60000) / 1000;
  ms = bestLapTime % 1000;
  sprintf(buf, "%02d:%02d.%03d", min, sec, ms);
  tft.drawString(buf, 244, 80);

  // 7. 更新底部信息（带自定义颜色）
  tft.setTextDatum(top_center);
  
  // TC (亮青色)
  tft.fillRect(5, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  sprintf(buf, "%d", tc);
  tft.drawString(buf, 20, 210);

  // ABS (亮黄色)
  tft.fillRect(55, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  sprintf(buf, "%d", abs_val);
  tft.drawString(buf, 70, 210);

  // BB (亮红色)
  tft.fillRect(100, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  sprintf(buf, "%.1f", bb);
  tft.drawString(buf, 120, 210);

  // MAP (亮绿色 - 新增)
  tft.fillRect(155, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  sprintf(buf, "%d", map_val);
  tft.drawString(buf, 170, 210);

  // FUEL
  tft.fillRect(230, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  sprintf(buf, "%.0f", fuel);
  tft.drawString(buf, 245, 210);

  // LAPS
  tft.fillRect(285, 210, 30, 20, COLOR_PANEL);
  sprintf(buf, "%d", laps);
  tft.drawString(buf, 300, 210);
}