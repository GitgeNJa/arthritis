#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NimBLEDevice.h>

/* =========================================================
   OLED
   ========================================================= */

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

#define SDA_PIN 21
#define SCL_PIN 22
Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

/* =========================================================
   ULTRASONIC
   ========================================================= */

#define TRIG_PIN 5
#define ECHO_PIN 18

#define MIN_GESTURE_DISTANCE 3.0
#define MAX_GESTURE_DISTANCE 20.0
#define TOO_CLOSE_DISTANCE 3.0

/* =========================================================
   OUTPUTS
   ========================================================= */

#define LED_LIGHT_PIN 25
#define LED_FAN_PIN   26
#define LED_HELP_PIN  27
#define LED_ALERT_PIN 14

/* =========================================================
   TIMING
   ========================================================= */

#define ULTRASONIC_WARMUP_TIME 5000UL
#define ULTRASONIC_READ_INTERVAL 150UL

#define REQUIRED_VALID_SAMPLES 2

#define GESTURE_WAIT_TIME 2000UL

#define TOO_CLOSE_ALERT_TIME 5000UL
#define NO_ECHO_ALERT_TIME 2000UL

#define ALERT_DISPLAY_TIME 5000UL
#define GESTURE_DISPLAY_TIME 3000UL

/* =========================================================
   GESTURE SYSTEM
   ========================================================= */

bool gestureArmed = true;

bool showingGesture = false;
unsigned long gestureDisplayStart = 0;

bool gestureWindowActive = false;
unsigned long gestureWindowStart = 0;

/* =========================================================
   ULTRASONIC VARIABLES
   ========================================================= */

bool ultrasonicReady = false;
unsigned long ultrasonicWarmupStart = 0;

float currentDistance = 999.0;
unsigned long lastUltrasonicRead = 0;
int validRangeSamples = 0;
bool gestureRangeConfirmed = false;

/* =========================================================
   ALERT VARIABLES
   ========================================================= */

bool tooCloseTiming = false;
unsigned long tooCloseStartTime = 0;
bool tooCloseAlertDone = false;

bool noEchoTiming = false;
unsigned long noEchoStartTime = 0;
bool noEchoAlertDone = false;

bool alertActive = false;
unsigned long alertStartTime = 0;

/* =========================================================
   PERSISTENT DEVICE STATES
   =========================================================
   These states are independent.
   G1 -> Light
   G2 -> Fan
   G3 -> Help
   G4 -> Not Set
   State remains until the SAME gesture is performed again.
   ========================================================= */

bool lightState = false;
bool fanState = false;
bool helpState = false;
bool notSetState = false;

/* =========================================================
   LINK.ino variable
   ========================================================= */

bool bleConnected = false;
volatile bool newCommandReceived = false;
String receivedCommand = "";

volatile bool newOrientationReceived = false;
String receivedOrientation = "";

float orientX = 0.0;
float orientY = 0.0;
float orientZ = 0.0;
bool hasOrientation = false;

/* =========================================================
   APPLY CURRENT DEVICE STATES
   ========================================================= */

void applyDeviceStates()
{
  digitalWrite(LED_LIGHT_PIN, lightState ? HIGH : LOW);
  digitalWrite(LED_FAN_PIN, fanState ? HIGH : LOW);
  digitalWrite(LED_HELP_PIN, helpState ? HIGH : LOW);
}

void turnOffGestureLEDs()
{
  digitalWrite(LED_LIGHT_PIN, LOW);
  digitalWrite(LED_FAN_PIN, LOW);
  digitalWrite(LED_HELP_PIN, LOW);
  digitalWrite(LED_ALERT_PIN, LOW);
}

void drawBLEStatus()
{
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(118, 0);
  if (bleConnected)
  {
    display.print("C");
  }
  else
  {
    display.print("N");
  }
}

/* =========================================================
   Waiting
   ========================================================= */

void showNoAction()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setCursor(0, 44);
  if (hasOrientation)
  {
    display.print("X:");
    display.print(orientX, 1);
    display.print(" Y:");
    display.print(orientY, 1);
    display.print(" Z:");
    display.print(orientZ, 1);
  }
  else
  {
    display.print("waiting");
  }
  display.display();
}

void processOrientation(String data)
{
  int first = data.indexOf(',');
  int second = data.indexOf(',', first + 1);
  int third = data.indexOf(',', second + 1);

  if (first < 0 || second < 0 || third < 0)
  {
    return;
  }

  orientX = data.substring(first + 1, second).toFloat();
  orientY = data.substring(second + 1, third).toFloat();
  orientZ = data.substring(third + 1).toFloat();
  hasOrientation = true;

  if (!alertActive && !showingGesture)
  {
    showNoAction();
  }
}

/* =========================================================
   SHOW MESSAGE
   ========================================================= */

void showMessage(const char *message)
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print(message);
  display.display();
}

/* =========================================================
   ULTRASONIC DISTANCE
   ========================================================= */

float getDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long duration =
    pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0)
  {
    return 999.0;
  }
  float distance =
    duration * 0.0343 / 2.0;
  return distance;
}

/* =========================================================
   START ALERT
   ========================================================= */

void startAlert()
{
  alertActive = true;
  alertStartTime = millis();
  gestureWindowActive = false;

  // keep light/fan/help states, only turn on the alert LED
  digitalWrite(LED_ALERT_PIN, HIGH);
  showMessage("ALERT");
  uploadEvent("ALERT", 1);
  Serial.println("ALERT TRIGGERED");
}

/* =========================================================
   ALERT TIMER
   ========================================================= */

void handleAlertTimer()
{
  if (!alertActive)
  {
    return;
  }
  if (
    millis() - alertStartTime >=
    ALERT_DISPLAY_TIME
  )
  {
    alertActive = false;
    digitalWrite(LED_ALERT_PIN, LOW);

    // restore device outputs
    applyDeviceStates();
    showNoAction();
    Serial.println("Alert finished");
  }
}

/* =========================================================
   GESTURE DISPLAY TIMER
   ========================================================= */

void handleGestureDisplay()
{
  if (!showingGesture)
  {
    return;
  }
  if (
    millis() - gestureDisplayStart >=
    GESTURE_DISPLAY_TIME
  )
  {
    showingGesture = false;

    // keep light/fan/help states as selected
    applyDeviceStates();
    showNoAction();
  }
}

/* =========================================================
   GESTURE WINDOW
   ========================================================= */

void handleGestureWindow()
{
  if (!gestureWindowActive)
  {
    return;
  }
  if (
    millis() - gestureWindowStart >=
    GESTURE_WAIT_TIME
  )
  {
    gestureWindowActive = false;
    Serial.println("Gesture window expired");
    showNoAction();
  }
}

/* =========================================================
   ULTRASONIC HANDLING
   ========================================================= */

void handleUltrasonic(float distance)
{
  currentDistance = distance;

  // no echo
  if (distance >= 900.0)
  {
    if (!noEchoTiming)
    {
      noEchoTiming = true;
      noEchoStartTime = millis();
      noEchoAlertDone = false;
    }
    if (
      !noEchoAlertDone &&
      millis() - noEchoStartTime >=
      NO_ECHO_ALERT_TIME
    )
    {
      noEchoAlertDone = true;
      startAlert();
    }
    return;
  }
  noEchoTiming = false;
  noEchoAlertDone = false;

  // too close
  if (distance < TOO_CLOSE_DISTANCE)
  {
    if (!tooCloseTiming)
    {
      tooCloseTiming = true;
      tooCloseStartTime = millis();
      tooCloseAlertDone = false;
    }
    if (
      !tooCloseAlertDone &&
      millis() - tooCloseStartTime >=
      TOO_CLOSE_ALERT_TIME
    )
    {
      tooCloseAlertDone = true;
      startAlert();
    }
    return;
  }
  tooCloseTiming = false;
  tooCloseAlertDone = false;

  // valid gesture range
  if (
    distance >= MIN_GESTURE_DISTANCE &&
    distance <= MAX_GESTURE_DISTANCE
  )
  {
    if (
      gestureArmed &&
      !showingGesture &&
      !gestureWindowActive &&
      !alertActive
    )
    {
      validRangeSamples++;
      if (
        validRangeSamples >=
        REQUIRED_VALID_SAMPLES
      )
      {
        validRangeSamples = 0;
        gestureRangeConfirmed = true;
        gestureWindowActive = true;
        gestureWindowStart = millis();
        Serial.println("Gesture window OPEN");
        Serial.println("Waiting for BLE gesture...");
      }
    }
  }
  else
  {
    validRangeSamples = 0;
  }

  // hand removed
  if (distance > MAX_GESTURE_DISTANCE)
  {
    validRangeSamples = 0;
    gestureRangeConfirmed = false;
    gestureWindowActive = false;
    if (!gestureArmed)
    {
      gestureArmed = true;
      Serial.println("GESTURE SYSTEM RE-ARMED");
    }
  }
}

/* =========================================================
   PROCESS GESTURE COMMAND
   ========================================================= */

void processCommand(String command)
{
  command.trim();
  command.toUpperCase();
  Serial.println();
  Serial.print("Processing gesture: ");
  Serial.println(command);

  // basic validation
  if (alertActive)
  {
    Serial.println("Ignored: Alert active");
    return;
  }
  if (!gestureWindowActive)
  {
    Serial.println("Ignored: Gesture window inactive");
    return;
  }
  if (
    command != "G1" &&
    command != "G2" &&
    command != "G3" &&
    command != "G4"
  )
  {
    Serial.println("Unknown gesture");
    return;
  }

  // G1 = light toggle
  if (command == "G1")
  {
    gestureWindowActive = false;
    gestureArmed = false;
    lightState = !lightState;
    digitalWrite(LED_LIGHT_PIN, lightState ? HIGH : LOW);
    if (lightState)
    {
      showMessage("LIGHTS ON");
      uploadEvent("LIGHT", 1);
      Serial.println("LIGHT = ON");
    }
    else
    {
      showMessage("LIGHTS OFF");
      uploadEvent("LIGHT", 0);
      Serial.println("LIGHT = OFF");
    }
    showingGesture = true;
    gestureDisplayStart = millis();
    return;
  }

  // G2 = fan toggle
  if (command == "G2")
  {
    gestureWindowActive = false;
    gestureArmed = false;
    fanState = !fanState;
    digitalWrite(LED_FAN_PIN, fanState ? HIGH : LOW);
    if (fanState)
    {
      showMessage("FAN ON");
      uploadEvent("FAN", 1);
      Serial.println("FAN = ON");
    }
    else
    {
      showMessage("FAN OFF");
      uploadEvent("FAN", 0);
      Serial.println("FAN = OFF");
    }
    showingGesture = true;
    gestureDisplayStart = millis();
    return;
  }

  // G3 = help toggle
  if (command == "G3")
  {
    gestureWindowActive = false;
    gestureArmed = false;
    helpState = !helpState;
    digitalWrite(LED_HELP_PIN, helpState ? HIGH : LOW);
    if (helpState)
    {
      showMessage("NEED HELP");
      uploadEvent("HELP", 1);
      Serial.println("HELP = ON");
    }
    else
    {
      showMessage("HELP OFF");
      uploadEvent("HELP", 0);
      Serial.println("HELP = OFF");
    }
    showingGesture = true;
    gestureDisplayStart = millis();
    return;
  }

  // G4 = not set toggle, no physical LED
  if (command == "G4")
  {
    gestureWindowActive = false;
    gestureArmed = false;
    notSetState = !notSetState;
    if (notSetState)
    {
      showMessage("NOT SET ON");
      uploadEvent("NOT_SET", 1);
      Serial.println("NOT_SET = ON");
    }
    else
    {
      showMessage("NOT SET OFF");
      uploadEvent("NOT_SET", 0);
      Serial.println("NOT_SET = OFF");
    }
    showingGesture = true;
    gestureDisplayStart = millis();
    return;
  }
}

/* =========================================================
   ULTRASONIC UPDATE
   ========================================================= */

void updateUltrasonic()
{
  if (!ultrasonicReady)
  {
    if (
      millis() - ultrasonicWarmupStart >=
      ULTRASONIC_WARMUP_TIME
    )
    {
      ultrasonicReady = true;
      Serial.println("Ultrasonic ready");
    }
    else
    {
      return;
    }
  }
  if (
    millis() - lastUltrasonicRead >=
    ULTRASONIC_READ_INTERVAL
  )
  {
    lastUltrasonicRead = millis();
    float distance = getDistance();
    handleUltrasonic(distance);
  }
}

/* =========================================================
   SETUP
   ========================================================= */

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("================================");
  Serial.println(" ARTHRITIS RECEIVER");
  Serial.println("================================");

  // i2c
  Wire.begin(SDA_PIN, SCL_PIN);

  // oled
  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDR
    )
  )
  {
    Serial.println("OLED initialization failed");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print("NO ACTION");
  drawBLEStatus();
  display.display();

  // ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // led outputs
  pinMode(LED_LIGHT_PIN, OUTPUT);
  pinMode(LED_FAN_PIN, OUTPUT);
  pinMode(LED_HELP_PIN, OUTPUT);
  pinMode(LED_ALERT_PIN, OUTPUT);

  // initial output state
  lightState = false;
  fanState = false;
  helpState = false;
  notSetState = false;
  turnOffGestureLEDs();

  // ultrasonic warmup
  ultrasonicWarmupStart = millis();

  // BLE
  NimBLEDevice::init("ARTHRITIS_RECEIVER");

  // connect to band
  connectToBand();

  // WIFI
  connectWiFi();
  delay(500);
}

/* =========================================================
   LOOP
   ========================================================= */

void loop()
{
  // BLE
  checkBluetooth();

  // WIFI reconnect
  if (WiFi.status() != WL_CONNECTED)
  {
    connectWiFi();
  }

  // ultrasonic
  updateUltrasonic();
  // gesture window
  handleGestureWindow();
  // alert timer
  handleAlertTimer();
  // gesture display timer
  handleGestureDisplay();

  if (newOrientationReceived)
  {
    noInterrupts();
    String data = receivedOrientation;
    newOrientationReceived = false;
    interrupts();
    processOrientation(data);
  }

  // process BLE command
  if (newCommandReceived)
  {
    noInterrupts();
    String command =
      receivedCommand;
    newCommandReceived = false;
    interrupts();
    processCommand(command);
  }

  // BLE status
  drawBLEStatus();
  display.display();
  delay(20);
}
