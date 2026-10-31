#include <WiFi.h>
#include "AsyncUDP.h"
#include <LovyanGFX.hpp>
#include "CYD-ESP32-2432S028RV3.h"

// ===== 2.8寸双USB接口电阻屏2432S028RV3 =====
CYD_ESP32_2432S028RV3 tft;

// ================== 颜色定义 ==================
#define COLOR_BG TFT_BLACK
#define COLOR_PANEL 0x18E3  // 深灰蓝背景
#define COLOR_BORDER 0x5AEB // 边框灰
#define COLOR_TEXT TFT_WHITE
#define COLOR_LABEL TFT_WHITE // 标签灰
#define COLOR_GREEN TFT_GREEN
#define COLOR_RED TFT_RED
#define COLOR_YELLOW TFT_YELLOW
#define COLOR_BLUE TFT_BLUE
#define COLOR_DARKBLUE 0xc
#define COLOR_ORANGE 0xFD20 // 自定义橙色
#define COLOR_CYAN TFT_CYAN
#define COLOR_TC 0x5ff   // 亮蓝
#define COLOR_ABS 0xff09 // 土黄
#define COLOR_BB TFT_RED
#define COLOR_MAP TFT_GREEN
#define COLOR_FUEL TFT_WHITE

// ================== 显示用的数据变量 ==================
float speed = 0; // 速度kmh
int gear = 1;
int rpm_percent = 0; // 转速条显示用 0到100
int engine_rpm = 0;
const float rpm_min_ratio = 0.9; // 转速条最低点按怠速转速乘以rpm_min_ratio

int brake_FL = 150, brake_FR = 155, brake_RL = 120, brake_RR = 125;
int susp_FL = -18, susp_FR = -18, susp_RL = -18, susp_RR = -18;
// 时间显示格式00:00:00.00
long totalms = 0; // 毫秒单位的时间
int h = 0;
int m = 0;
int s = 0;
int ms = 0;
// 赛段完成百分比0.00到100.00
float distProgress = 0.0;
// 底部显示数据 遗憾的是DiRT Rally 2.0 没有油量 TC ABS等参数
float fuel = 18.0;
int laps = 1;
int tc = 0, abs_val = 0;
float bb = 72.2;
int map_val = 1;

// 用于刷新屏幕的计时
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 30;

// 用于每秒串口打印的计时
unsigned long lastPrintMs = 0;
const unsigned long PRINT_INTERVAL_MS = 1000;

// ===== 请修改为你的Wi-Fi信息 =====
const char *ssid = "XXXXXX";
const char *password = "XXXXXX";

// ===== DiRT Rally 2.0 遥测配置 =====
const uint16_t DIRT_PORT = 20778; // 与 hardware_settings_config.xml 中一致
const size_t PACKET_SIZE = 264;   // 66 * 4 bytes
int UDP_TIMEOUT = 0;              // 超时计数 数据丢失时，每间隔PRINT_INTERVAL_MS加1
const int UDP_TIMEOUT_MAX = 5;    // 5s超时
char buf_localIP[19];

// 全局缓冲区：保存“最新一次”收到的完整数据包
uint8_t latestPacket[PACKET_SIZE];
volatile bool newPacketReady = false; // 标志位：是否有新数据待处理

// ===== DiRT Rally 2.0 数据包 =====
// 使用 packed 结构体直接映射 264 字节，避免手动计算偏移
// ESP32 是小端架构，与游戏数据一致，可直接 memcpy
typedef struct __attribute__((packed))
{
  float total_time;
  float lap_time;
  float lap_distance;
  float total_distance; // 注意：实际为归一化进度 0~1，非米
  float position_x, position_y, position_z;
  float speed; // m/s
  float velocity_x, velocity_y, velocity_z;
  float left_dir_x, left_dir_y, left_dir_z;
  float forward_dir_x, forward_dir_y, forward_dir_z;
  float suspension_position_rl, suspension_position_rr;
  float suspension_position_fl, suspension_position_fr;
  float suspension_velocity_rl, suspension_velocity_rr;
  float suspension_velocity_fl, suspension_velocity_fr;
  float wheel_patch_speed_bl, wheel_patch_speed_br;
  float wheel_patch_speed_fl, wheel_patch_speed_fr;
  float throttle_input, steering_input, brake_input, clutch_input;
  float gear; // 0=空挡, -1=倒挡
  float gforce_lateral, gforce_longitudinal;
  float lap;
  float engine_rate; // rad/s
  float native_sli_support;
  float race_position;
  float kers_level, kers_level_max, drs;
  float traction_control, abs;
  float fuel_in_tank, fuel_capacity;
  float in_pits, race_sector;
  float sector_time_1, sector_time_2;
  float brake_temp_rl, brake_temp_rr, brake_temp_fl, brake_temp_fr;
  float tyre_pressure_rl, tyre_pressure_rr;
  float tyre_pressure_fl, tyre_pressure_fr;
  float laps_completed, total_laps, track_length;
  float last_lap_time;
  float max_rpm;  // rad/s
  float idle_rpm; // rad/s
  float max_gears;
} DirtPacket;
static_assert(sizeof(DirtPacket) == 264, "DirtPacket must be 264 bytes"); // 编译期"尺寸自检"
// 数据包放.bss段 不占栈
DirtPacket gDirtPkt;

AsyncUDP udp;

void setup()
{
  Serial.begin(115200);
  // 屏幕初始化显示
  tft.init();
  tft.setRotation(1);
  tft.setBrightness(128);
  tft.fillScreen(COLOR_BG);
  drawStaticUI();
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  // 显示IP地址
  snprintf(buf_localIP, sizeof(buf_localIP), "IP:%s", WiFi.localIP().toString().c_str());
  tft.setTextSize(1); // 1号字体
  tft.setTextDatum(middle_center);
  tft.fillRect(100, 100, 120, 20, COLOR_DARKBLUE);
  tft.setFont(&fonts::Font0);
  tft.setTextColor(COLOR_TEXT, COLOR_DARKBLUE);
  tft.drawString(buf_localIP, 160, 110);

  // 启动UDP监听，绑定到 0.0.0.0:20778，接收任意来源
  if (udp.listen(DIRT_PORT))
  {
    Serial.printf("UDP listening on port %u\n", DIRT_PORT);

    // 注册回调：只做“拷贝数据”，不做解析和打印
    udp.onPacket([](AsyncUDPPacket packet)
                 {
      if (packet.length() == PACKET_SIZE) {
        // 将数据拷贝到全局缓冲区
        memcpy(latestPacket, packet.data(), PACKET_SIZE);
        newPacketReady = true;
      } });
  }
  else
  {
    Serial.println("UDP listen failed!");
  }
}

void loop()
{
  unsigned long now = millis();
  // 每 1 秒检查一次，打印最新数据
  if (now - lastPrintMs >= PRINT_INTERVAL_MS)
  {
    lastPrintMs = now;
    if (newPacketReady == false)
    {
      if (UDP_TIMEOUT < UDP_TIMEOUT_MAX)
      {
        UDP_TIMEOUT = UDP_TIMEOUT + 1;
      }
      if (UDP_TIMEOUT == UDP_TIMEOUT_MAX)
      {
        Serial.println("Waiting for DiRT Rally 2.0 data...");
        // 显示IP地址
        snprintf(buf_localIP, sizeof(buf_localIP), "IP:%s", WiFi.localIP().toString().c_str());
        tft.setTextSize(1); // 1号字体
        tft.setTextDatum(middle_center);
        tft.fillRect(100, 100, 120, 20, COLOR_DARKBLUE);
        tft.setFont(&fonts::Font0);
        tft.setTextColor(COLOR_TEXT, COLOR_DARKBLUE);
        tft.drawString(buf_localIP, 160, 110);
       // delay(1000);
      }
    }
  }
  // 每30ms刷新屏幕
  if (now - lastUpdate >= UPDATE_INTERVAL_MS)
  {
    lastUpdate = now;
    if (newPacketReady)
    {
      UDP_TIMEOUT = 0;
      newPacketReady = false; // 清除标志，等待下一次更新
      parseAndPrint(latestPacket);
    }
  }
}

// ===== 解析并显示 =====
void parseAndPrint(const uint8_t *data)
{
  memcpy(&gDirtPkt, data, PACKET_SIZE);
  // 数据赋值
  speed = gDirtPkt.speed * 3.6f;                                  // m/s->kmh
  engine_rpm = (int)(gDirtPkt.engine_rate * 60.0f / (2.0f * PI)); // rad/s -> RPM
  gear = (int)gDirtPkt.gear;
  if (gDirtPkt.max_rpm > 0 && gDirtPkt.idle_rpm > 0)
    rpm_percent = (int)((gDirtPkt.engine_rate - gDirtPkt.idle_rpm * rpm_min_ratio) / (gDirtPkt.max_rpm - gDirtPkt.idle_rpm * rpm_min_ratio) * 100);
  else
    rpm_percent = 0;
  rpm_percent = constrain(rpm_percent, 0, 100);
  brake_FL = (int)gDirtPkt.brake_temp_fl;
  brake_FR = (int)gDirtPkt.brake_temp_fr;
  brake_RL = (int)gDirtPkt.brake_temp_rl;
  brake_RR = (int)gDirtPkt.brake_temp_rr;
  susp_FL = (int)gDirtPkt.suspension_position_fl;
  susp_FR = (int)gDirtPkt.suspension_position_fr;
  susp_RL = (int)gDirtPkt.suspension_position_rl;
  susp_RR = (int)gDirtPkt.suspension_position_rr;
  // float格式时间，整理成00:00:00.000
  totalms = (long)(gDirtPkt.lap_time * 1000.0f + 0.5f); // 四舍五入
  h = totalms / 3600000L;
  m = (totalms / 60000L) % 60;
  s = (totalms / 1000L) % 60;
  ms = totalms % 100;
  // 赛段完成百分比0.00到100.00
  distProgress = gDirtPkt.total_distance * 100.0f;
  updateUI();
}

// ================== 绘制静态界面 ==================
void drawStaticUI()
{
  // --- 1. 左上面板：刹车 ---
  tft.fillRoundRect(2, 2, 94, 110, 4, COLOR_PANEL);
  tft.drawRoundRect(2, 2, 94, 110, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("BRK TMP", 49, 6);

  tft.setTextDatum(top_left);
  tft.drawString("FL", 6, 26);
  tft.drawString("FR", 53, 26);
  tft.drawString("RL", 6, 66);
  tft.drawString("RR", 53, 66);

  tft.drawRect(36, 38, 6, 22, COLOR_LABEL);
  tft.drawRect(83, 38, 6, 22, COLOR_LABEL);
  tft.drawRect(36, 78, 6, 22, COLOR_LABEL);
  tft.drawRect(83, 78, 6, 22, COLOR_LABEL);

  // --- 2. 左下面板：计时 ---
  tft.fillRoundRect(2, 116, 94, 48, 4, COLOR_PANEL);
  tft.drawRoundRect(2, 116, 94, 48, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("TIME", 49, 118);

  // --- 3. 右上面板：悬挂 ---
  tft.fillRoundRect(224, 2, 94, 110, 4, COLOR_PANEL);
  tft.drawRoundRect(224, 2, 94, 110, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("SUSP", 271, 4);

  tft.setTextDatum(top_left);
  tft.drawString("FL", 228, 24);
  tft.drawRect(260, 22, 8, 30, COLOR_LABEL);
  tft.drawString("FR", 275, 24);
  tft.drawRect(307, 22, 8, 30, COLOR_LABEL);

  tft.drawString("RL", 228, 64);
  tft.drawRect(260, 62, 8, 30, COLOR_LABEL);
  tft.drawString("RR", 275, 64);
  tft.drawRect(307, 62, 8, 30, COLOR_LABEL);

  // --- 4. 右下面板：里程百分比 ---
  tft.fillRoundRect(224, 116, 94, 48, 4, COLOR_PANEL);
  tft.drawRoundRect(224, 116, 94, 48, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("DIST", 271, 118);

  // --- 5. 底部转速条背景 ---
  tft.fillRect(10, 168, 300, 10, COLOR_PANEL);

  // --- 6. 底部信息面板 ---
  tft.fillRoundRect(2, 184, 316, 54, 4, COLOR_PANEL);
  tft.drawRoundRect(2, 184, 316, 54, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  tft.drawString("TC", 20, 190);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  tft.drawString("ABS", 70, 190);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  tft.drawString("BB", 120, 190);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  tft.drawString("MAP", 170, 190);
  tft.setTextColor(COLOR_FUEL, COLOR_PANEL);
  tft.drawString("FUEL", 245, 190);
  tft.drawString("LAPS", 300, 190);

  // 速度单位（下移）
  tft.setFont(&fonts::Font0);
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.drawString("km/h", 160, 155);
}

// ================== 刷新动态界面 ==================
void updateUI()
{
  char buf[24];

  // 1. 更新刹车温度 & 刹车片颜色
  tft.setTextSize(1); // 确保缩放复位
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::Font2);

  auto drawBrake = [](int x, int y, int barX, int barY, int temp)
  {
    tft.fillRect(x, y, 24, 16, COLOR_PANEL);
    tft.fillRect(barX + 1, barY + 1, 4, 20, COLOR_PANEL);

    uint16_t textColor = COLOR_TEXT;
    uint16_t barColor = COLOR_BLUE;

    if (temp > 500)
    {
      textColor = COLOR_RED;
      barColor = COLOR_RED;
    }
    else if (temp > 400)
    {
      textColor = COLOR_RED;
      barColor = COLOR_ORANGE;
    }
    else if (temp > 200)
    {
      barColor = COLOR_YELLOW;
    }
    else if (temp > 100)
    {
      barColor = COLOR_GREEN;
    }

    tft.setTextColor(textColor, COLOR_PANEL);
    char b[10];
    sprintf(b, "%3d", temp);
    tft.drawString(b, x, y);

    tft.fillRect(barX + 1, barY + 1, 4, 20, barColor);
  };

  drawBrake(6, 42, 36, 38, brake_FL);
  drawBrake(53, 42, 83, 38, brake_FR);
  drawBrake(6, 82, 36, 78, brake_RL);
  drawBrake(53, 82, 83, 78, brake_RR);

  // 2. 更新 Time 计时 (字体缩小，格式 00:00:00.00)
  tft.setFont(&fonts::Font2);
  tft.setTextSize(1.0); // 缩小字体确保不超宽
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_CYAN, COLOR_PANEL);
  tft.fillRect(4, 135, 90, 20, COLOR_PANEL);
  sprintf(buf, "%02d:%02d:%02d.%03d", h, m, s, ms);
  tft.drawString(buf, 49, 145);

  // 3. 更新悬挂行程
  tft.setTextSize(1); // 恢复默认大小
  tft.setFont(&fonts::Font2);
  tft.setTextDatum(top_left);
  auto drawSusp = [](int valX, int valY, int barX, int barY, int val)
  {
    tft.fillRect(valX, valY, 32, 16, COLOR_PANEL);
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    char b[10];
    sprintf(b, "%4d", val);
    tft.drawString(b, valX, valY);

    tft.fillRect(barX + 1, barY + 1, 6, 28, COLOR_PANEL);

    uint16_t barColor = COLOR_GREEN;
    if (val > 0)
      barColor = COLOR_BLUE;
    else if (val < -150)
      barColor = COLOR_RED;
    else if (val < -100)
      barColor = COLOR_YELLOW;

    int centerY = barY + 14;
    // 最大高度限制为13，确保永不触顶或触底
    if (val > 0)
    {
      int h = map(val, 0, 110, 0, 13);
      h = constrain(h, 0, 13);
      tft.fillRect(barX + 1, centerY - h, 6, h, barColor);
    }
    else if (val < 0)
    {
      int h = map(abs(val), 0, 190, 0, 13);
      h = constrain(h, 0, 13);
      tft.fillRect(barX + 1, centerY, 6, h, barColor);
    }
  };

  drawSusp(228, 38, 260, 22, susp_FL);
  drawSusp(275, 38, 307, 22, susp_FR);
  drawSusp(228, 78, 260, 62, susp_RL);
  drawSusp(275, 78, 307, 62, susp_RR);

  // 4. 更新里程百分比 (缩小字体，完美容纳)
  tft.setFont(&fonts::Font2); // 缩小为 Font2 防溢出
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_YELLOW, COLOR_PANEL);
  tft.fillRect(226, 135, 90, 20, COLOR_PANEL);
  sprintf(buf, "%06.2f%%", distProgress);
  tft.drawString(buf, 271, 145);

  // 5. 更新档位 (2 倍尺寸)
  // tft.fillRect(120, 5, 80, 95, COLOR_BG); // 扩大清屏区域
  tft.setTextDatum(BC_DATUM);
  tft.setFont(&fonts::Font0); // 使用 Font4 配合 Size(2)
  tft.setTextSize(11);        // 2 倍尺寸

  if (gear == 0)
  {
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.drawString("N", 160, 100);
  }
  else if (gear == -1)
  {
    tft.setTextColor(COLOR_RED, COLOR_BG);
    tft.drawString("R", 160, 100);
  }
  else if (gear > 0 && gear < 10)
  {
    tft.setTextColor(COLOR_YELLOW, COLOR_BG);
    sprintf(buf, "%d", gear);
    tft.drawString(buf, 160, 100);
  }
  tft.setTextSize(1); // 必须复位
  tft.setTextDatum(middle_center);
  // 6. 更新转速小字 (相应下移)
  tft.fillRect(100, 100, 120, 20, COLOR_DARKBLUE);
  tft.setFont(&fonts::Font0);
  tft.setTextColor(COLOR_TEXT, COLOR_DARKBLUE);
  sprintf(buf, "%d RPM", engine_rpm);
  tft.drawString(buf, 160, 110);

  // 7. 更新速度 (缩小字体并下移)
  tft.fillRect(100, 122, 120, 26, COLOR_BG);
  tft.setFont(&fonts::Font4); // 换用 Font4 缩小尺寸
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  sprintf(buf, "%.0f", speed);
  tft.drawString(buf, 160, 135);

  // 8. 更新转速条
  tft.fillRect(10, 168, 300, 10, COLOR_PANEL);
  int w = (rpm_percent * 300) / 100;
  if (w > 0)
  {
    if (rpm_percent <= 60)
    {
      tft.fillRect(10, 168, w, 10, COLOR_GREEN);
    }
    else if (rpm_percent <= 85)
    {
      tft.fillRect(10, 168, (60 * 300) / 100, 10, COLOR_GREEN);
      tft.fillRect(10 + (60 * 300) / 100, 168, w - (60 * 300) / 100, 10, COLOR_YELLOW);
    }
    else
    {
      tft.fillRect(10, 168, (60 * 300) / 100, 10, COLOR_GREEN);
      tft.fillRect(10 + (60 * 300) / 100, 168, ((85 - 60) * 300) / 100, 10, COLOR_YELLOW);
      tft.fillRect(10 + (85 * 300) / 100, 168, w - (85 * 300) / 100, 10, COLOR_RED);
    }
  }

  // 9. 更新底部信息
  tft.setFont(&fonts::Font2);
  tft.setTextDatum(top_center);

  tft.fillRect(5, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  sprintf(buf, "%d", tc);
  tft.drawString(buf, 20, 210);

  tft.fillRect(55, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  sprintf(buf, "%d", abs_val);
  tft.drawString(buf, 70, 210);

  tft.fillRect(100, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  sprintf(buf, "%.1f", bb);
  tft.drawString(buf, 120, 210);

  tft.fillRect(155, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  sprintf(buf, "%d", map_val);
  tft.drawString(buf, 170, 210);

  tft.fillRect(230, 210, 30, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  sprintf(buf, "%.0f", fuel);
  tft.drawString(buf, 245, 210);

  tft.fillRect(285, 210, 30, 20, COLOR_PANEL);
  sprintf(buf, "%d", laps);
  tft.drawString(buf, 300, 210);
}