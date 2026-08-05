#include "FastIMU.h"
#include <Wire.h>
#include "Arduino_RouterBridge.h" // App Lab RPC Bridge

#define IMU_ADDRESS 0x68
#define PERFORM_CALIBRATION 

MPU6500 IMU;
calData calib = { 0 };
AccelData accelData;

#define GRAVITY 9.80665f

// Sampling configuration: 50 Hz (20 ms interval)
#define SAMPLING_FREQ_HZ 100
#define SAMPLING_INTERVAL_MS (1000 / SAMPLING_FREQ_HZ)
const float DT = 1.0f / SAMPLING_FREQ_HZ; 
unsigned long lastSampleTime = 0;

// Exponential Moving Average (EMA) Alpha
const float ALPHA = 0.25f;

// Filter States
float filtAccX = 0.0f, filtAccY = 0.0f, filtAccZ = 0.0f;
bool isFirstSample = true;

void setup() {
  Wire.begin();
  Wire.setClock(400000); // 400kHz I2C clock

  Serial.begin(115200);

  int err = IMU.init(calib, IMU_ADDRESS);

#ifdef PERFORM_CALIBRATION
  delay(1000);
  IMU.calibrateAccelGyro(&calib);
  IMU.init(calib, IMU_ADDRESS);
#endif
}

void loop() {
  unsigned long now = millis();
  if (now - lastSampleTime >= SAMPLING_INTERVAL_MS) {
    lastSampleTime = now;

    IMU.update();
    IMU.getAccel(&accelData);

    // 1. Raw accelerometer (m/s^2) & gyro (deg/s)
    float rawAccX = accelData.accelX * GRAVITY;
    float rawAccY = accelData.accelY * GRAVITY;
    float rawAccZ = accelData.accelZ * GRAVITY;

    // 2. Exponential Moving Average
    if (isFirstSample) {
      filtAccX = rawAccX; filtAccY = rawAccY; filtAccZ = rawAccZ;
      isFirstSample = false;
    } else {
      filtAccX = (ALPHA * rawAccX) + ((1.0f - ALPHA) * filtAccX);
      filtAccY = (ALPHA * rawAccY) + ((1.0f - ALPHA) * filtAccY);
      filtAccZ = (ALPHA * rawAccZ) + ((1.0f - ALPHA) * filtAccZ);
    }

    // 5. Clean Serial flushing (prevents Zephyr hardware serial buffer freeze)
    Serial.print(filtAccX); Serial.print(",");
    Serial.print(filtAccY); Serial.print(",");
    Serial.println(filtAccZ);
  }
}