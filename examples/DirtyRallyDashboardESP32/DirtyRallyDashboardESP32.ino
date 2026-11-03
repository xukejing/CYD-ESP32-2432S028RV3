#include <WiFi.h>
#include "AsyncUDP.h"
#include <LovyanGFX.hpp>
#include "CYD-ESP32-2432S028RV3.h"
#include "Gear_100.h"

// ===== 2.8寸双USB接口电阻屏2432S028RV3 =====
CYD_ESP32_2432S028RV3 tft;

// ================== 颜色定义 ==================
#define COLOR_BG TFT_BLACK
#define COLOR_PANEL TFT_BLACK  // 深灰蓝背景0x20E7
#define COLOR_BORDER TFT_WHITE // 边框
#define COLOR_TEXT TFT_WHITE
#define COLOR_LABEL TFT_WHITE // 标签
#define COLOR_GREEN TFT_GREEN
#define COLOR_RED TFT_RED
#define COLOR_YELLOW TFT_YELLOW
#define COLOR_BLUE TFT_BLUE
#define COLOR_DARKBLUE 0xc
#define COLOR_ORANGE 0xFD20
#define COLOR_CYAN TFT_CYAN
#define COLOR_TC 0x5ff
#define COLOR_ABS 0xff09
#define COLOR_BB TFT_RED
#define COLOR_MAP TFT_GREEN
#define COLOR_FUEL TFT_WHITE

// ================== 显示用的数据变量 ==================
float speed = 0; // 速度 km/h
int gear = 1;
int rpm_percent = 0; // 转速条 0~100
int engine_rpm = 0;
const float rpm_min_ratio = 0.9; // 转速条最低点按怠速乘以此比例

int brake_FL = 150, brake_FR = 155, brake_RL = 120, brake_RR = 125;
int susp_FL = -18, susp_FR = -18, susp_RL = -18, susp_RR = -18;

long totalms = 0; // 毫秒时间
int hour = 0, m = 0, s = 0, ms = 0;

float dist_cur = 0.0;
float dist_total = 0.0;
float distProgress = 0.0; // 赛段完成百分比 0.00~100.00

float fuel = 18.0;
int laps = 1;
int tc = 0, abs_val = 0;
float bb = 72.2;
int map_val = 1;

// 屏幕刷新计时
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 30;

// 每秒串口打印计时
unsigned long lastPrintMs = 0;
const unsigned long PRINT_INTERVAL_MS = 1000;

// ===== Wi-Fi 信息 =====
// 使用SmartConfig 不需要定义了

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
  tft.setRotation(1); // 320x240
  tft.setBrightness(255);
  tft.fillScreen(COLOR_BG);
  drawStaticUI();
  delay(100);
  tft.initLED(); // 把背板RGB灯熄灭

  // Wi-Fi 先尝试自动连接，失败则进入 SmartConfig
  if (!AutoConfig())
  {
    startSmartConfig();
  }
  // 防休眠提高udp接收速率 在 Wi-Fi 初始化并连接 AP 之前或之后调用
  WiFi.setSleep(false);

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

  brake_FL = constrain((int)gDirtPkt.brake_temp_fl, -40, 990);
  brake_FR = constrain((int)gDirtPkt.brake_temp_fr, -40, 990);
  brake_RL = constrain((int)gDirtPkt.brake_temp_rl, -40, 990);
  brake_RR = constrain((int)gDirtPkt.brake_temp_rr, -40, 990);

  susp_FL = (int)gDirtPkt.suspension_position_fl;
  susp_FR = (int)gDirtPkt.suspension_position_fr;
  susp_RL = (int)gDirtPkt.suspension_position_rl;
  susp_RR = (int)gDirtPkt.suspension_position_rr;

  // float格式时间，整理成00:00:00.000
  totalms = (long)(gDirtPkt.lap_time * 1000.0f + 0.5f); // 四舍五入
  hour = totalms / 3600000L;
  m = (totalms / 60000L) % 60;
  s = (totalms / 1000L) % 60;
  ms = totalms % 1000;

  dist_cur = gDirtPkt.lap_distance;
  dist_total = gDirtPkt.track_length;
  // 赛段完成百分比0.00到100.00
  distProgress = gDirtPkt.total_distance * 100.0f;
  updateUI();
}

// ================== 绘制静态界面 ==================
void drawStaticUI()
{
   tft.setTextSize(1);
  // --- 1. 左上面板：刹车 ---
  tft.fillRoundRect(0, 0, 96, 100, 4, COLOR_PANEL);
  tft.drawRoundRect(0, 0, 96, 100, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("BRK TMP", 48, 4);

  tft.setTextDatum(top_left);
  tft.drawString("FL", 4, 24);
  tft.drawString("FR", 52, 24);
  tft.drawString("RL", 4, 66);
  tft.drawString("RR", 52, 66);

  tft.drawRect(36, 24, 9, 30, COLOR_LABEL);
  tft.drawRect(84, 24, 9, 30, COLOR_LABEL);
  tft.drawRect(36, 66, 9, 30, COLOR_LABEL);
  tft.drawRect(84, 66, 9, 30, COLOR_LABEL);

  // --- 2. 左下面板：计时 ---
  tft.fillRoundRect(0, 104, 96, 60, 4, COLOR_PANEL);
  tft.drawRoundRect(0, 104, 96, 60, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("TIME", 48, 106);

  // --- 3. 右上面板：悬挂 ---
  tft.fillRoundRect(223, 0, 96, 100, 4, COLOR_PANEL);
  tft.drawRoundRect(223, 0, 96, 100, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("SUSP", 271, 4);

  tft.setTextDatum(top_left);
  tft.drawString("FL", 227, 24);
  tft.drawRect(259, 24, 9, 30, COLOR_LABEL);
  tft.drawString("FR", 275, 24);
  tft.drawRect(307, 24, 9, 30, COLOR_LABEL);

  tft.drawString("RL", 227, 66);
  tft.drawRect(259, 66, 9, 30, COLOR_LABEL);
  tft.drawString("RR", 275, 66);
  tft.drawRect(307, 66, 9, 30, COLOR_LABEL);

  // --- 4. 右下面板：里程百分比 ---
  tft.fillRoundRect(223, 104, 96, 60, 4, COLOR_PANEL);
  tft.drawRoundRect(223, 104, 96, 60, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("SESSION", 271, 106);

  // --- 5. 底部转速条背景 ---
  tft.fillRect(1, 169, 310, 10, COLOR_PANEL);
  tft.drawRect(0, 168, 320, 12, COLOR_BORDER);

  // --- 6. 底部信息面板 ---
  tft.fillRoundRect(0, 184, 320, 56, 4, COLOR_PANEL);
  tft.drawRoundRect(0, 184, 320, 56, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  tft.drawString("TC", 30, 190);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  tft.drawString("ABS", 80, 190);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  tft.drawString("BB", 130, 190);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  tft.drawString("MAP", 180, 190);
  tft.setTextColor(COLOR_FUEL, COLOR_PANEL);
  tft.drawString("FUEL", 230, 190);
  tft.drawString("LAPS", 280, 190);

  // 速度单位（下移）
  tft.setFont(&fonts::Font0);
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.drawString("km/h", 160, 155);

  // 中部转速值的背景色
  tft.fillRect(100, 100, 120, 20, COLOR_DARKBLUE);
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
    // 数值区仅清除 x~x+32，避免擦掉右侧条
    tft.fillRect(x, y, 32, 16, COLOR_PANEL);
    //tft.fillRect(barX + 1, barY + 1, 7, 28, COLOR_PANEL);//新颜色直接刷新覆盖即可 注释掉

    uint16_t textColor = COLOR_TEXT;
    uint16_t barColor = 0x5159;
    if (temp > 400) {
      textColor = 0x8935;
      barColor = 0x8935;
    } else if (temp > 300) {
      textColor = 0xe1c6;
      barColor = 0xe1c6;
    } else if (temp > 200) {
      textColor = 0xfc40;
      barColor = 0xfc40;
    } else if (temp > 120) {
      textColor = 0xd623;
      barColor = 0xd623;
    } else if (temp > 80) {
      barColor = 0x1db4;
    }

    tft.setTextColor(textColor, COLOR_PANEL);
    char b[10];
    sprintf(b, "%03d", temp);
    tft.drawString(b, x, y);

    tft.fillRect(barX + 1, barY + 1, 7, 28, barColor);
  };

  drawBrake(4, 40, 36, 24, brake_FL);
  drawBrake(52, 40, 84, 24, brake_FR);
  drawBrake(4, 82, 36, 66, brake_RL);
  drawBrake(52, 82, 84, 66, brake_RR);

  // 2. 更新 Time 计时 (字体缩小，格式 00:00:00.00)
  tft.setFont(&fonts::DejaVu12);
  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_CYAN, COLOR_PANEL);
  //tft.fillRect(1, 126, 94, 13, COLOR_PANEL);
  sprintf(buf, "%02d:%02d.%03d", m, s, ms);
  tft.drawString(buf, 48, 126);

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

    //tft.fillRect(barX + 1, barY + 1, 7, 28, COLOR_PANEL);

    uint16_t barColor=0xc81f;
    if (val > 150) barColor = COLOR_RED;
    else if (val > 120) barColor = 0xfb20;
    else if (val > 90) barColor = 0xfe60;
    else if (val > 60) barColor = 0xffe0;
    else if (val > 30) barColor = 0xcfe0;
    else if (val > 0) barColor = 0x67e0;
    else if (val > -30) barColor = COLOR_GREEN;
    else if (val > -60) barColor = 0x7f3;
    else if (val > -90) barColor = 0x7ff;
    else if (val > -120) barColor = 0x33f;
    else if (val > -150) barColor = 0x1f;
    else if (val > -180) barColor = 0x981f;
    else barColor=0xc81f;

    int centerY = barY + 15;
    if (val > 0) {
      int h = constrain(map(val, 0, 190, 0, 14), 0, 14);
      tft.fillRect(barX + 1, centerY - h, 7, h, barColor);
      tft.fillRect(barX + 1, centerY - 14, 7, 14 - h, COLOR_PANEL);
      tft.fillRect(barX + 1, centerY, 7, 14, COLOR_PANEL);
    } else if (val < 0) {
      int h = constrain(map(abs(val), 0, 190, 0, 14), 0, 14);
      tft.fillRect(barX + 1, centerY, 7, h, barColor);
      tft.fillRect(barX + 1, centerY + h, 7, 14 - h, COLOR_PANEL);
      tft.fillRect(barX + 1, centerY - 14, 7, 14, COLOR_PANEL);
    }
  };

  drawSusp(227, 40, 259, 24, susp_FL);
  drawSusp(275, 40, 307, 24, susp_FR);
  drawSusp(227, 82, 259, 66, susp_RL);
  drawSusp(275, 82, 307, 66, susp_RR);

  // 4. 更新里程百分比 (缩小字体，完美容纳)
  tft.setFont(&fonts::Font2); // 缩小为 Font2 防溢出
  tft.setTextDatum(top_left);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  tft.fillRect(227, 122, 82, 16, COLOR_PANEL);//清除旧公里数显示
  sprintf(buf, "%.2f%/%.2f", dist_cur / 1000.0, dist_total / 1000.0);
  tft.drawString(buf, 227, 122);
  tft.setTextColor(COLOR_GREEN, COLOR_PANEL);
  sprintf(buf, "%06.2f%%", distProgress);
  tft.drawString(buf, 227, 144);
  int prograss_i = (int)(distProgress * 0.88f);
  tft.fillRect(227, 138, prograss_i, 4, COLOR_GREEN);
  tft.fillRect(227 + prograss_i, 138, 88 - prograss_i, 4, 0x18c);

  // 5. 更新档位 (2 倍尺寸)
  // tft.fillRect(100, 0, 120, 100, COLOR_BG); //// 字符宽度相同 刷新时会覆盖旧的 不需要清档位区 这句可以注释掉
  tft.setTextDatum(BC_DATUM);
  tft.loadFont(Gear_100_data, lgfx::IFont::font_type_t::ft_lvgl);

  if (gear == 0)
  {
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.drawString("N", 160, 100);
  }
  else if (gear == -1)
  {
    tft.setTextColor(COLOR_ORANGE, COLOR_BG);
    tft.drawString("R", 160, 100);
  }
  else if (gear > 0 && gear < 10)
  {
    tft.setTextColor(COLOR_YELLOW, COLOR_BG);
    sprintf(buf, "%d", gear);
    tft.drawString(buf, 160, 100);
  }
  tft.unloadFont();

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
  // tft.fillRect(1, 169, 318, 10, COLOR_PANEL);/已改进填充代码,不需要预先刷背景色
  int w = (rpm_percent * 318) / 100;
  if (w > 0)
  {
    if (rpm_percent <= 60)
    {
      tft.fillRect(1, 169, w, 10, COLOR_GREEN);
      tft.fillRect(1 + w, 169, 318 - w, 10, COLOR_PANEL);
    }
    else if (rpm_percent <= 85)
    {
      tft.fillRect(1, 169, 190, 10, COLOR_GREEN);
      tft.fillRect(1 + 190, 169, w - 190, 10, COLOR_YELLOW);
      tft.fillRect(1 + w, 169, 318 - w, 10, COLOR_PANEL);
    }
    else
    {
      tft.fillRect(1, 169, 190, 10, COLOR_GREEN);
      tft.fillRect(1 + 190,169, 80, 10, COLOR_YELLOW);
      tft.fillRect(1 + 270 , 169,w - 270, 10, COLOR_RED);
      tft.fillRect(1+w, 169, 318-w, 10, COLOR_PANEL);
    }
  }

  // 9. 更新底部信息
  tft.setFont(&fonts::Font2);
  tft.setTextDatum(top_center);

  tft.fillRect(5, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  sprintf(buf, "%d", tc);
  tft.drawString(buf, 30, 210);

  tft.fillRect(55, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  sprintf(buf, "%d", abs_val);
  tft.drawString(buf, 80, 210);

  tft.fillRect(100, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  sprintf(buf, "%.1f", bb);
  tft.drawString(buf, 130, 210);

  tft.fillRect(155, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  sprintf(buf, "%d", map_val);
  tft.drawString(buf, 180, 210);

  tft.fillRect(210, 210, 40, 20, COLOR_PANEL);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  sprintf(buf, "%.0f", fuel);
  tft.drawString(buf, 230, 210);

  tft.fillRect(260, 210, 40, 20, COLOR_PANEL);
  sprintf(buf, "%d", laps);
  tft.drawString(buf, 280, 210);
}

// 尝试自动连接（最多等待 10 秒）
bool AutoConfig() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // 使用 NVS 中保存的凭证
  Serial.print("Connecting to WiFi");
  for (int i = 0; i < 20; i++) {  // 20 * 500ms = 10s 超时
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nAuto connect timeout.");
  return false;
}

// 启动 SmartConfig 等待手机 App 配网
void startSmartConfig() {
  WiFi.mode(WIFI_STA);  // SmartConfig 需要在 STA 模式下运行
  Serial.println("\nWaiting for SmartConfig...");
  WiFi.beginSmartConfig();  // 启动 SmartConfig

  // 等待手机 App 发送配网信息（可加入超时，这里等待直到成功）
  while (!WiFi.smartConfigDone()) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nSmartConfig received.");
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());

  // 等待连接到目标 AP
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}