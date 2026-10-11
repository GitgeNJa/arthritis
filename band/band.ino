#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SD_PIN 8
#define SC_PIN 9

Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);

BLECharacteristic *gestureCharacteristic;
bool deviceConnected = false;

#define SAMPLE_INTERVAL 20UL
#define SEND_INTERVAL 500UL

#define GYRO_THRESHOLD 30.0
#define GYRO_DOMINANT_MARGIN 20.0

unsigned long lastSend = 0;

float peakX = 0.0;
float peakY = 0.0;
float peakZ = 0.0;

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  // I2C
  Wire.begin(SD_PIN, SC_PIN);

  startBle();

  // BNO055
  if (!bno.begin())
  {
    Serial.println("BNO055 NOT FOUND!");
    while (1)
    {
      delay(1000);
    }
  }

  delay(1000);
  // external crystal
  bno.setExtCrystalUse(true);
  // NDOF sensor fusion
  bno.setMode((adafruit_bno055_opmode_t)0x0C);
  delay(100);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" SYSTEM READY");
  Serial.println("======================================");
  Serial.println();
}

// =====================================================
// GYRO
// =====================================================

void readGyro(float &x, float &y, float &z)
{
  // degrees per second
  imu::Vector<3> gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
  x = gyro.x();
  y = gyro.y();
  z = gyro.z();
}

void notePeak(float &peak, float sample)
{
  if (abs(sample) > abs(peak))
  {
    peak = sample;
  }
}

void sendGyro()
{
  float x = peakX;
  float y = peakY;
  float z = peakZ;
  peakX = 0.0;
  peakY = 0.0;
  peakZ = 0.0;

  // keep only the dominant axis, others are sent as 0
  float absX = abs(x);
  float absY = abs(y);
  float absZ = abs(z);

  float largest = max(absX, max(absY, absZ));
  float second;

  if (largest == absX)
    second = max(absY, absZ);
  else if (largest == absY)
    second = max(absX, absZ);
  else
    second = max(absX, absY);

  // too slow or no clear dominant axis
  if (largest < GYRO_THRESHOLD || largest - second < GYRO_DOMINANT_MARGIN)
  {
    x = 0.0;
    y = 0.0;
    z = 0.0;
  }
  else if (largest == absX)
  {
    y = 0.0;
    z = 0.0;
  }
  else if (largest == absY)
  {
    x = 0.0;
    z = 0.0;
  }
  else
  {
    x = 0.0;
    y = 0.0;
  }

  if (!deviceConnected)
  {
    return;
  }

  String message = "O,";
  message += String(x, 1);
  message += ",";
  message += String(y, 1);
  message += ",";
  message += String(z, 1);

  gestureCharacteristic->setValue((uint8_t *)message.c_str(), message.length());
  gestureCharacteristic->notify();
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  float x;
  float y;
  float z;
  readGyro(x, y, z);
  notePeak(peakX, x);
  notePeak(peakY, y);
  notePeak(peakZ, z);

  if (millis() - lastSend >= SEND_INTERVAL)
  {
    lastSend = millis();
    sendGyro();
  }

  delay(SAMPLE_INTERVAL);
}
