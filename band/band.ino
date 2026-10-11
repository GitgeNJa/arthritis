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

// =====================================================
// GESTURE SETTINGS
// =====================================================

#define ANGLE_THRESHOLD 25.0
#define RETURN_THRESHOLD 10.0
#define GESTURE_DELAY 300
#define ORIENTATION_INTERVAL 500UL
#define CHANGE_THRESHOLD 10.0
#define DOMINANT_MARGIN 5.0

float startHeading;
float startRoll;
bool waitingForReturn = false;
unsigned long lastOrientationSend = 0;

float prevHeading;
float prevRoll;
float prevPitch;
bool hasPrevOrientation = false;

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
  // initial wrist position
  calibrateStartingPosition();

  Serial.println();
  Serial.println("======================================");
  Serial.println(" SYSTEM READY");
  Serial.println("======================================");
  Serial.println();
}

// =====================================================
// GET ORIENTATION
// =====================================================

void getOrientation(float &heading, float &roll, float &pitch)
{
  sensors_event_t event;
  bno.getEvent(&event, Adafruit_BNO055::VECTOR_EULER);
  heading = event.orientation.x;
  roll = event.orientation.y;
  pitch = event.orientation.z;
}

// =====================================================
// ANGLE DIFFERENCE
// =====================================================

float angleDifference(float current, float start)
{
  float difference = current - start;

  if (difference > 180)
    difference -= 360;

  if (difference < -180)
    difference += 360;

  return difference;
}

// =====================================================
// INITIAL POSITION
// =====================================================

void calibrateStartingPosition()
{
  float pitch;
  delay(2000);
  getOrientation(startHeading, startRoll, pitch);
}

// =====================================================
// CHECK RETURN TO NORMAL POSITION
// =====================================================

void checkReturnPosition()
{
  float heading;
  float roll;
  float pitch;

  getOrientation(heading, roll, pitch);

  float headingChange = angleDifference(heading, startHeading);
  float rollChange = angleDifference(roll, startRoll);

  if (
    abs(headingChange) < RETURN_THRESHOLD &&
    abs(rollChange) < RETURN_THRESHOLD
  )
  {
    waitingForReturn = false;
  }
}

// =====================================================
// SEND GESTURE
// =====================================================

void sendGesture(const char *gesture)
{
  if (deviceConnected)
  {
    // set data
    gestureCharacteristic->setValue((uint8_t *)gesture, strlen(gesture));
    // send BLE notification
    gestureCharacteristic->notify();

    Serial.print("COMMAND SENT : ");
    Serial.println(gesture);
  }
  else
  {
    Serial.print("BLE NOT CONNECTED - COMMAND : ");
    Serial.println(gesture);
  }
}

// =====================================================
// SEND ORIENTATION
// =====================================================

void sendOrientation()
{
  if (millis() - lastOrientationSend < ORIENTATION_INTERVAL)
  {
    return;
  }

  lastOrientationSend = millis();

  float heading;
  float roll;
  float pitch;
  getOrientation(heading, roll, pitch);

  float headingChange = 0.0;
  float rollChange = 0.0;
  float pitchChange = 0.0;

  if (hasPrevOrientation)
  {
    headingChange = angleDifference(heading, prevHeading);
    rollChange = angleDifference(roll, prevRoll);
    pitchChange = angleDifference(pitch, prevPitch);
  }

  prevHeading = heading;
  prevRoll = roll;
  prevPitch = pitch;
  hasPrevOrientation = true;

  if (!deviceConnected)
  {
    return;
  }

  // keep only the dominant axis, others are sent as 0
  float absHeading = abs(headingChange);
  float absRoll = abs(rollChange);
  float absPitch = abs(pitchChange);

  float largest = max(absHeading, max(absRoll, absPitch));
  float second;

  if (largest == absHeading)
    second = max(absRoll, absPitch);
  else if (largest == absRoll)
    second = max(absHeading, absPitch);
  else
    second = max(absHeading, absRoll);

  // too small or no clear dominant axis
  if (largest < CHANGE_THRESHOLD || largest - second < DOMINANT_MARGIN)
  {
    headingChange = 0.0;
    rollChange = 0.0;
    pitchChange = 0.0;
  }
  else if (largest == absHeading)
  {
    rollChange = 0.0;
    pitchChange = 0.0;
  }
  else if (largest == absRoll)
  {
    headingChange = 0.0;
    pitchChange = 0.0;
  }
  else
  {
    headingChange = 0.0;
    rollChange = 0.0;
  }

  String message = "O,";
  message += String(headingChange, 1);
  message += ",";
  message += String(rollChange, 1);
  message += ",";
  message += String(pitchChange, 1);

  gestureCharacteristic->setValue((uint8_t *)message.c_str(), message.length());
  gestureCharacteristic->notify();
}

// =====================================================
// GESTURE DETECTION
// =====================================================

void detectGesture()
{
  float heading;
  float roll;
  float pitch;

  getOrientation(heading, roll, pitch);

  float headingChange = angleDifference(heading, startHeading);
  float rollChange = angleDifference(roll, startRoll);

  // wait for wrist to return
  if (waitingForReturn)
  {
    checkReturnPosition();
    return;
  }

  // G1 = left
  if (headingChange < -ANGLE_THRESHOLD)
  {
    sendGesture("G1");
    gestureDetected();
    return;
  }

  // G2 = right
  if (headingChange > ANGLE_THRESHOLD)
  {
    sendGesture("G2");
    gestureDetected();
    return;
  }

  // G3 = down
  if (rollChange < -ANGLE_THRESHOLD)
  {
    sendGesture("G3");
    gestureDetected();
    return;
  }

  // G4 = up
  if (rollChange > ANGLE_THRESHOLD)
  {
    sendGesture("G4");
    gestureDetected();
    return;
  }
}

// =====================================================
// GESTURE COMPLETED
// =====================================================

void gestureDetected()
{
  float pitch;
  delay(GESTURE_DELAY);
  getOrientation(startHeading, startRoll, pitch);
  waitingForReturn = true;
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  //detectGesture();
  sendOrientation();
  delay(20);
}
