#include <M5Unified.h>
#include <M5GFX.h>

m5::imu_data_t imuData;

void setup() {
  M5.begin();
  M5.Display.setRotation(3);
  M5.Display.setTextDatum(top_center);
  M5.Display.setFont(&fonts::FreeMonoBold24pt7b);
  M5.Display.clear(WHITE);
  M5.Display.drawString("IMU Realtime Data", M5.Lcd.width() / 2, 0);
}

void loop() {
  M5.Imu.update();
  imuData = M5.Imu.getImuData();

  M5.Display.setCursor(0, 100);
  M5.Display.printf(" Acc X = %6.2f  \n", imuData.accel.x);
  M5.Display.printf(" Acc Y = %6.2f  \n", imuData.accel.y);
  M5.Display.printf(" Acc Z = %6.2f  \n\n", imuData.accel.z);

  M5.Display.printf(" Gyr X = %6.2f  \n", imuData.gyro.x);
  M5.Display.printf(" Gyr Y = %6.2f  \n", imuData.gyro.y);
  M5.Display.printf(" Gyr Z = %6.2f  \n", imuData.gyro.z);

  // delay(1000);
  delay(20);
} 