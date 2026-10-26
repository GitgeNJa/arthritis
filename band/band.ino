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
// false sends G1-G10 only. true can also send tap, push, and pull.
// The base has the same switch. Set both the same and upload both.
bool moreCommands = false;

#define SAMPLE_INTERVAL 20UL
#define SEND_INTERVAL 500UL
#define GYRO_THRESHOLD 30.0
#define GYRO_DOMINANT_MARGIN 10.0
#define GRAVITY_FLIP_THRESHOLD 5.0
#define LINEAR_THRESHOLD 4.0
#define LINEAR_DOMINANT_MARGIN 2.0

unsigned long lastSend = 0;

float peakX = 0.0;
float peakY = 0.0;
float peakZ = 0.0;

float peakLinearX = 0.0;
float peakLinearY = 0.0;
float peakLinearZ = 0.0;

float stableGz = 0.0;
bool hasStableGravity = false;
bool gravityLocked = false;
bool handMoving = false;

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
  // gyro + accelerometer fusion, magnetometer is not used
  bno.setMode(OPERATION_MODE_IMUPLUS);
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

void readLinear(float &x, float &y, float &z)
{
  // metres per second squared, gravity removed
  imu::Vector<3> linear = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
  x = linear.x();
  y = linear.y();
  z = linear.z();
}

void notePeak(float &peak, float sample)
{
  if (abs(sample) > abs(peak))
  {
    peak = sample;
  }
}

void updateStableGravity(float gyroX, float gyroY, float gyroZ)
{
  handMoving =
    abs(gyroX) >= GYRO_THRESHOLD ||
    abs(gyroY) >= GYRO_THRESHOLD ||
    abs(gyroZ) >= GYRO_THRESHOLD;

  if (handMoving)
  {
    gravityLocked = true;
    return;
  }

  if (gravityLocked)
  {
    return;
  }

  imu::Vector<3> gravity = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
  float gz = gravity.z();

  if (!hasStableGravity)
  {
    stableGz = gz;
    hasStableGravity = true;
    return;
  }

  stableGz = stableGz * 0.85 + gz * 0.15;
}

bool postureFlipped()
{
  return hasStableGravity && stableGz > GRAVITY_FLIP_THRESHOLD;
}

const char *commandFor(float x, float y, float z)
{
  bool flipped = postureFlipped();

  if (x > 0.0)
    return flipped ? "G7" : "G2";
  if (x < 0.0)
    return flipped ? "G8" : "G1";
  if (y > 0.0)
    return "G3";
  if (y < 0.0)
    return "G4";
  if (z > 0.0)
    return flipped ? "G10" : "G5";
  if (z < 0.0)
    return flipped ? "G9" : "G6";
  return nullptr;
}

const char *commandForLinear(float x, float y, float z)
{
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

  if (largest < LINEAR_THRESHOLD || largest - second < LINEAR_DOMINANT_MARGIN)
    return nullptr;

  if (largest == absZ)
    return "G11";
  if (largest == absY && y > 0.0)
    return "G12";
  if (largest == absY && y < 0.0)
    return "G13";
  return nullptr;
}

void sendGyro()
{
  float x = peakX;
  float y = peakY;
  float z = peakZ;
  peakX = 0.0;
  peakY = 0.0;
  peakZ = 0.0;

  float linearX = peakLinearX;
  float linearY = peakLinearY;
  float linearZ = peakLinearZ;
  peakLinearX = 0.0;
  peakLinearY = 0.0;
  peakLinearZ = 0.0;

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

  const char *gyroCommand = commandFor(x, y, z);
  const char *linearCommand = commandForLinear(linearX, linearY, linearZ);

  float gyroScore = 0.0;
  float linearScore = 0.0;
  if (gyroCommand != nullptr)
  {
    gyroScore = max(abs(x), max(abs(y), abs(z))) / GYRO_THRESHOLD;
  }
  if (linearCommand != nullptr)
  {
    linearScore = max(abs(linearX), max(abs(linearY), abs(linearZ))) / LINEAR_THRESHOLD;
  }

  const char *command = gyroCommand;
  if (moreCommands && linearScore > gyroScore)
  {
    command = linearCommand;
  }

  if (!handMoving)
  {
    gravityLocked = false;
  }

  if (!deviceConnected || command == nullptr)
  {
    return;
  }

  gestureCharacteristic->setValue((uint8_t *)command, strlen(command));
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
  updateStableGravity(x, y, z);

  float linearX;
  float linearY;
  float linearZ;
  readLinear(linearX, linearY, linearZ);
  notePeak(peakLinearX, linearX);
  notePeak(peakLinearY, linearY);
  notePeak(peakLinearZ, linearZ);

  if (millis() - lastSend >= SEND_INTERVAL)
  {
    lastSend = millis();
    sendGyro();
  }

  delay(SAMPLE_INTERVAL);
}
