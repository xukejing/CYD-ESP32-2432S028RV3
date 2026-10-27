#include <CYD-ESP32-2432S028RV3.h>

CYD_ESP32_2432S028RV3 tft;

void setup() {
  Serial.begin(115200);
  delay(500);

  tft.init();
  tft.setRotation(0); 
    CYD_ESP32_2432S028RV3::initLED();
  tft.setBrightness(128);
  // --- 触摸校准开始 ---
  uint16_t calData[8]; // 用于存储校准数据的数组
  Serial.println("Please tap the arrows/points as instructed...");
  // 参数: 校准数据数组, 前景色, 背景色, 标记大小
  tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
  Serial.println("Calibration done!");
  Serial.println(calData[0]);
  Serial.println(calData[1]);
  Serial.println(calData[2]);
  Serial.println(calData[3]);
  Serial.println(calData[4]);
  Serial.println(calData[5]);
  Serial.println(calData[6]);
  Serial.println(calData[7]);
  // --- 触摸校准结束 ---
/*     uint16_t TouchCalibData[8]; // 用于存储校准数据的数组
TouchCalibData[0]=260;
TouchCalibData[1]=3910;
TouchCalibData[2]=226;
TouchCalibData[3]=378;
TouchCalibData[4]=3806;
TouchCalibData[5]=3899;
TouchCalibData[6]=3815;
TouchCalibData[7]=372; */



 // Serial.println("Ready.");
}

void loop() {

}
