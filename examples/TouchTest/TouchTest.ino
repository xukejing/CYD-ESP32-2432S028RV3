#include <CYD-ESP32-2432S028RV3.h>

CYD_ESP32_2432S028RV3 tft;


void setup() {
  Serial.begin(115200);
  delay(500);
  CYD_ESP32_2432S028RV3::initLED();

  tft.init();
  tft.setRotation(1);  
    // 定义并应用已有的校准数据
  uint16_t TouchCalibData[8] = {226, 3910, 226, 378, 3806, 3899, 3815, 372};
  tft.setTouchCalibrate(TouchCalibData);

  tft.setBrightness(128);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Touch Test");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println("by Evgeniy - Shitov");
  tft.println("Touch the screen!");
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);

  Serial.println("Ready.");
}

void loop() {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    tft.fillCircle(x, y, 4, TFT_RED);
    Serial.printf("x=%u y=%u\n", x, y);
    delay(1);
  }
}
