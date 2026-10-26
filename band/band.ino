#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// "false" sends G1-G10 only
// "true" can additionally send tap, push and pull
// the base has the same switch. Set both the same before upload
bool moreCommands = false;

// "false" gestures only
// "true" additionally sends gyro X Y Z and gravity Z, in that order
// the base has the same switch. Set both the same before upload
bool showValues = false;

// "false" uses the fixed flip line
// "true" after connect, 5 seconds rest then 5 seconds flip
// the base has the same switch. Set both the same before upload
bool calibration = true;

#define SD_PIN 8
#define SC_PIN 9

Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);

BLECharacteristic *gestureCharacteristic;
bool deviceConnected = false;

#define SAMPLE_INTERVAL 20UL
#define SEND_INTERVAL 500UL
#define GYRO_THRESHOLD 30.0
#define GYRO_DOMINANT_MARGIN 10.0
#define GRAVITY_FLIP_THRESHOLD 4.0
#define CAL_HOLD_MS 5000UL
#define CAL_SETTLE_MS 2000UL
#define CAL_RESULT_MS 3000UL
#define CAL_MOVE_DPS 80.0
#define CAL_MOVE_SAMPLES 5
#define CAL_MIN_SEPARATION 2.0
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

int calPhase = 0;
bool calTiming = false;
unsigned long calStillStart = 0;
float calSum = 0.0;
int calCount = 0;
int calMoveStreak = 0;
int calShownSecond = -1;
float normalGz = 0.0;
float flippedGz = 0.0;
int flipFails = 0;
bool flipEnabled = false;

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
  if (calibration && flipEnabled && hasStableGravity && calPhase == 3)
  {
    return abs(stableGz - flippedGz) < abs(stableGz - normalGz);
  }
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

int narrow(float value)
{
  if (value > 999.0)
    return 999;
  if (value < -999.0)
    return -999;
  return (int)value;
}

void sendGyro()
{
  float x = peakX;
  float y = peakY;
  float z = peakZ;
  float valueX = x;
  float valueY = y;
  float valueZ = z;
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

  if (!deviceConnected)
  {
    return;
  }

  if (!showValues)
  {
    if (command == nullptr)
    {
      return;
    }
    gestureCharacteristic->setValue((uint8_t *)command, strlen(command));
    gestureCharacteristic->notify();
    return;
  }

  char message[28];
  if (command == nullptr)
  {
    snprintf(
      message,
      sizeof(message),
      "V%4d %4d %4d %4.1f",
      narrow(valueX),
      narrow(valueY),
      narrow(valueZ),
      stableGz
    );
  }
  else
  {
    snprintf(
      message,
      sizeof(message),
      "%s %4d %4d %4d %4.1f",
      command,
      narrow(valueX),
      narrow(valueY),
      narrow(valueZ),
      stableGz
    );
  }
  gestureCharacteristic->setValue((uint8_t *)message, strlen(message));
  gestureCharacteristic->notify();
}

void notifyText(const char *message)
{
  if (!deviceConnected)
  {
    return;
  }
  gestureCharacteristic->setValue((uint8_t *)message, strlen(message));
  gestureCharacteristic->notify();
}

float readGravityZ()
{
  imu::Vector<3> gravity = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
  return gravity.z();
}

void resetCalHold()
{
  calTiming = false;
  calSum = 0.0;
  calCount = 0;
  calMoveStreak = 0;
  calShownSecond = -1;
}

void beginCalPhase(int phase)
{
  calPhase = phase;
  resetCalHold();
  if (phase == 1)
  {
    flipFails = 0;
    flipEnabled = false;
  }
}

void sendCalSecond(int second)
{
  if (second == calShownSecond)
  {
    return;
  }
  calShownSecond = second;
  char message[4];
  message[0] = 'C';
  message[1] = (calPhase == 1) ? 'N' : 'F';
  message[2] = '0' + second;
  message[3] = '\0';
  notifyText(message);
}

void finishCalHold(float average)
{
  if (calPhase == 1)
  {
    normalGz = average;
    beginCalPhase(2);
    return;
  }

  if (abs(average - normalGz) < CAL_MIN_SEPARATION)
  {
    flipFails++;
    resetCalHold();
    if (flipFails >= 3)
    {
      flipEnabled = false;
      calPhase = 4;
      calStillStart = millis();
      notifyText("CX");
    }
    return;
  }

  flippedGz = average;
  stableGz = average;
  hasStableGravity = true;
  gravityLocked = false;
  flipEnabled = true;
  peakX = 0.0;
  peakY = 0.0;
  peakZ = 0.0;
  lastSend = millis();
  calPhase = 3;
  notifyText("CD");
}

void runCalibration(float gyroX, float gyroY, float gyroZ)
{
  bool moving =
    abs(gyroX) >= CAL_MOVE_DPS ||
    abs(gyroY) >= CAL_MOVE_DPS ||
    abs(gyroZ) >= CAL_MOVE_DPS;
  float gz = readGravityZ();

  if (moving)
  {
    calMoveStreak++;
  }
  else
  {
    calMoveStreak = 0;
  }

  unsigned long elapsed = 0;
  if (calTiming)
  {
    elapsed = millis() - calStillStart;
  }

  if (
    calTiming &&
    elapsed >= CAL_SETTLE_MS &&
    calMoveStreak >= CAL_MOVE_SAMPLES
  )
  {
    resetCalHold();
    elapsed = 0;
  }

  if (!calTiming)
  {
    calTiming = true;
    calStillStart = millis();
    calSum = 0.0;
    calCount = 0;
    elapsed = 0;
  }
  else if (elapsed >= CAL_HOLD_MS && calCount > 0)
  {
    finishCalHold(calSum / calCount);
    return;
  }
  else if (elapsed >= CAL_SETTLE_MS)
  {
    calSum += gz;
    calCount++;
  }

  if (calPhase != 1 && calPhase != 2)
  {
    return;
  }

  int remain = 5;
  if (calTiming)
  {
    elapsed = millis() - calStillStart;
    if (elapsed < CAL_HOLD_MS)
    {
      remain = 5 - (int)(elapsed / 1000UL);
    }
    if (remain < 1)
    {
      remain = 1;
    }
  }
  sendCalSecond(remain);
}

bool handleCalibration(float gyroX, float gyroY, float gyroZ)
{
  if (!deviceConnected)
  {
    calPhase = 0;
    resetCalHold();
    flipFails = 0;
    flipEnabled = false;
    return false;
  }

  if (calPhase == 0)
  {
    beginCalPhase(1);
  }

  if (calPhase == 4)
  {
    if (millis() - calStillStart >= CAL_RESULT_MS)
    {
      calPhase = 3;
      flipEnabled = false;
      peakX = 0.0;
      peakY = 0.0;
      peakZ = 0.0;
      lastSend = millis();
      gravityLocked = false;
      notifyText("CD");
    }
    return true;
  }

  if (calPhase != 3)
  {
    runCalibration(gyroX, gyroY, gyroZ);
    return true;
  }

  return false;
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
  if (calibration && handleCalibration(x, y, z))
  {
    delay(SAMPLE_INTERVAL);
    return;
  }
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
