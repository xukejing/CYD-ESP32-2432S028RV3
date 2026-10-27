#pragma once

#include <LovyanGFX.hpp>

class CYD_ESP32_2432S028RV3 : public lgfx::LGFX_Device {
  lgfx::Bus_SPI       _bus;
  lgfx::Panel_ST7789  _panel;   // ← 关键：RV3 是 ST7789
  lgfx::Touch_XPT2046 _touch;
  lgfx::Light_PWM     _light_instance;   // ← PWM 背光

public:
  CYD_ESP32_2432S028RV3() {
    auto bus_cfg = _bus.config();
    bus_cfg.spi_host    = HSPI_HOST;
    bus_cfg.spi_mode    = 0;
    bus_cfg.freq_write  = 40000000;
    bus_cfg.freq_read   = 16000000;
    bus_cfg.dma_channel = 1;
    bus_cfg.pin_sclk    = 14;
    bus_cfg.pin_mosi    = 13;
    bus_cfg.pin_miso    = 12;
    bus_cfg.pin_dc      = 2;
    _bus.config(bus_cfg);
    _panel.setBus(&_bus);

    auto panel_cfg = _panel.config();
    panel_cfg.pin_cs          = 15;
    panel_cfg.pin_rst         = -1;
    panel_cfg.panel_width     = 240;
    panel_cfg.panel_height    = 320;
    panel_cfg.offset_x        = 0;
    panel_cfg.offset_y        = 0;
    panel_cfg.offset_rotation = 0;      // 若方向不对可试 2
    panel_cfg.dummy_read_pixel = 8;
    panel_cfg.readable        = true;
    panel_cfg.invert          = false;   // 对应 TFT_INVERSION_ON
     panel_cfg.rgb_order    = false;  // 红蓝反了再调
    panel_cfg.dlen_16bit      = false;
    panel_cfg.bus_shared      = false;
    _panel.config(panel_cfg);

    auto touch_cfg = _touch.config();
    touch_cfg.x_min      = 200;
    touch_cfg.x_max      = 3700;
    touch_cfg.y_min      = 3700;
    touch_cfg.y_max      = 200;
    touch_cfg.pin_int    = -1;
    touch_cfg.bus_shared = false;
    touch_cfg.offset_rotation = 0;
    touch_cfg.spi_host   = VSPI_HOST;          // 软件 SPI；能用但较慢
    touch_cfg.freq       = 1000000;
    touch_cfg.pin_sclk   = 25;
    touch_cfg.pin_mosi   = 32;
    touch_cfg.pin_miso   = 39;
    touch_cfg.pin_cs     = 33;
    _touch.config(touch_cfg);
    _panel.setTouch(&_touch);

    // ─── 背光 PWM 配置（经典 ESP32）───
    auto light_cfg = _light_instance.config();
    light_cfg.pin_bl      = 21;        // GPIO21
    light_cfg.invert      = false;         // 高电平点亮；若低电平点亮改为 true
    light_cfg.freq        = 44100;         // 避免可见闪烁
    light_cfg.pwm_channel = 7;             // 经典 ESP32 LEDC 通道 0~15，选 7 或 15 避让
    light_cfg.offset      = 0;
    _light_instance.config(light_cfg);
    _panel.setLight(&_light_instance);     // 关键：把 Light 挂到 Panel 上

    setPanel(&_panel);
  }

  // ─── RGB LED (active LOW) ───
  static constexpr uint8_t LED_R = 4;
  static constexpr uint8_t LED_G = 16;
  static constexpr uint8_t LED_B = 17;

  static void initLED() {
    pinMode(LED_R, OUTPUT); digitalWrite(LED_R, HIGH);
    pinMode(LED_G, OUTPUT); digitalWrite(LED_G, HIGH);
    pinMode(LED_B, OUTPUT); digitalWrite(LED_B, HIGH);
  }

  static void setLED(bool r, bool g, bool b) {
    digitalWrite(LED_R, !r);
    digitalWrite(LED_G, !g);
    digitalWrite(LED_B, !b);
  }

  void setBrightness(uint8_t level) {
    _light_instance.setBrightness(level);  // 0=灭, 255=全亮
  }
};