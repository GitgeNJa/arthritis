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
#define GYRO_DOMINANT_MARGIN 10.0
#define CAL_HOLD_MS 5000UL
#define CAL_SETTLE_MS 2000UL
#define CAL_RESULT_MS 3000UL
#define CAL_MOVE_DPS 80.0
#define CAL_MOVE_SAMPLES 5
#define CAL_MIN_SEPARATION 2.0

unsigned long lastSend = 0;

float peakX = 0.0;
float peakY = 0.0;
float peakZ = 0.0;

float stableGz = 0.0;
bool hasStableGravity = false;
bool gravityLocked = false;
bool handMoving = false;

int calPhase = 0;
unsigned long calStillStart = 0;
bool calTiming = false;
float calSum = 0.0;
int calCount = 0;
int calMoveStreak = 0;
unsigned long lastCalSend = 0;
float normalGz = 0.0;
float flippedGz = 0.0;
int calShownSecond = -1;
int flipFails = 0;
bool flipEnabled = false;
unsigned long lastReadingSend = 0;
const char *pendingCommand = nullptr;

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

void resetCalHold()
{
  calTiming = false;
  calSum = 0.0;
  calCount = 0;
  calShownSecond = -1;
}

void notifyText(const char *message)
{
  if (!deviceConnected || gestureCharacteristic == nullptr)
  {
    return;
  }

  gestureCharacteristic->setValue((uint8_t *)message, strlen(message));
  gestureCharacteristic->notify();
}

int rounded(float value)
{
  if (value >= 0.0)
  {
    return (int)(value + 0.5f);
  }
  return (int)(value - 0.5f);
}

void putSigned(char *dest, int value, int digits)
{
  int limit = 1;
  for (int i = 0; i < digits; i++)
  {
    limit *= 10;
  }
  if (value > limit - 1)
  {
    value = limit - 1;
  }
  if (value < 1 - limit)
  {
    value = 1 - limit;
  }
  if (value < 0)
  {
    dest[0] = '-';
    value = -value;
  }
  else
  {
    dest[0] = '+';
  }
  for (int i = digits; i >= 1; i--)
  {
    dest[i] = '0' + (value % 10);
    value /= 10;
  }
}

void sendReading(float gz, int gx, int gy, int gzz)
{
  char message[21];
  message[0] = 'W';
  putSigned(message + 1, rounded(gz * 10.0f), 3);
  putSigned(message + 5, gx, 4);
  putSigned(message + 10, gy, 4);
  putSigned(message + 15, gzz, 4);
  message[20] = '\0';
  notifyText(message);
}

void maybeSendGravity(float gz)
{
  if (millis() - lastCalSend < 150)
  {
    return;
  }
  if (millis() - lastReadingSend < SEND_INTERVAL)
  {
    return;
  }
  lastReadingSend = millis();
  sendReading(gz, 0, 0, 0);
}

float readGravityZ()
{
  imu::Vector<3> gravity = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
  return gravity.z();
}

void beginCalPhase(int phase)
{
  calPhase = phase;
  resetCalHold();
  calMoveStreak = 0;
  if (phase == 1)
  {
    flipFails = 0;
    flipEnabled = false;
  }
}

void sendCalSecond(int second)
{
  if (second == calShownSecond && millis() - lastCalSend < SEND_INTERVAL)
  {
    return;
  }

  calShownSecond = second;
  lastCalSend = millis();
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
  maybeSendGravity(gz);
}

bool postureFlipped()
{
  if (!flipEnabled || !hasStableGravity || calPhase != 3)
  {
    return false;
  }

  float fromNormal = abs(stableGz - normalGz);
  float fromFlipped = abs(stableGz - flippedGz);
  return fromFlipped < fromNormal;
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

void sendGyro()
{
  float rawX = peakX;
  float rawY = peakY;
  float rawZ = peakZ;
  float x = rawX;
  float y = rawY;
  float z = rawZ;
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

  const char *command = commandFor(x, y, z);

  if (!handMoving)
  {
    gravityLocked = false;
  }

  float gz = hasStableGravity ? stableGz : readGravityZ();
  sendReading(gz, rounded(rawX), rounded(rawY), rounded(rawZ));
  if (command != nullptr)
  {
    pendingCommand = command;
  }
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

  if (!deviceConnected)
  {
    calPhase = 0;
    resetCalHold();
    calMoveStreak = 0;
    flipFails = 0;
    flipEnabled = false;
    pendingCommand = nullptr;
    delay(SAMPLE_INTERVAL);
    return;
  }

  if (pendingCommand != nullptr)
  {
    notifyText(pendingCommand);
    pendingCommand = nullptr;
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
    delay(SAMPLE_INTERVAL);
    return;
  }

  if (calPhase != 3)
  {
    runCalibration(x, y, z);
    delay(SAMPLE_INTERVAL);
    return;
  }

  notePeak(peakX, x);
  notePeak(peakY, y);
  notePeak(peakZ, z);
  updateStableGravity(x, y, z);

  if (millis() - lastSend >= SEND_INTERVAL)
  {
    lastSend = millis();
    sendGyro();
  }

  delay(SAMPLE_INTERVAL);
}
