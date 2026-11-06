#include <string.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>

/* =========================================================
   WiFi / PHP (XAMPP on laptop)
   Set SERVER_PHP_URL to your laptop IP and path to arthritis.php
   Example: http://192.168.43.87/arthritis_patient/arthritis.php
   ========================================================= */

#define ENABLE_SERVER_UPLOAD true

// Must match the hotspot the laptop uses (same as old_base.ino when it worked)
const char *WIFI_SSID = "geNJaspot";
const char *WIFI_PASSWORD = "12345677";

// Laptop IPv4 on that hotspot + path where arthritis.php lives (check XAMPP htdocs)
const char *SERVER_PHP_URL =
  "http://192.168.1.3/arthritis_patient/arthritis.php";

#define WIFI_RETRY_INTERVAL_MS 30000UL

void connectWiFi();
void ensureWiFi();
void uploadServerEvent(const char *event, int state);

static unsigned long lastWifiAttempt = 0;

/* =========================================================
   Switchable Flags
   ========================================================= */

// "false" no exercise
// "true" close hand over ultrasonic sensor for 3 seconds starts or stops the exercise
bool exercise = true;

// "false" accept every command, for debugging
// "true" accept a command only while hand is over the ultrasonic sensor
bool proximity = false;

// "false" uses the fixed flip line
// "true" after connect, 5 seconds rest then 5 seconds flip
// the band has the same switch. Set both the same before upload
bool calibration = true;

// "false" gestures only
// "true" additionally sends gyro X Y Z and gravity Z, in that order
// the band has the same switch. Set both the same before upload
bool showValues = true;

// "false" sends G1-G10 only
// "true" can additionally send G11-G15 (tap, push, pull, nudges)
// The band has the same switch. Set both the same before upload
bool moreCommands = true;

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
   OUTPUTS
   ========================================================= */

#define LED_LIGHT_PIN 25
#define LED_FAN_PIN   26
#define LED_BULB_PIN  27

#define NO_ACTION_TIMEOUT 1500UL

/* =========================================================
   COMMAND
   ========================================================= */

bool bleConnected = false;

volatile bool newCommandReceived = false;
String receivedCommand = "";
String gestureLabel = "";
unsigned long lastCommandMillis = 0;
unsigned long lastLedMillis = 0;
bool showingGesture = false;
bool showingLed = false;
String valueLine = "";
volatile bool newValuesReceived = false;
String receivedCalibration = "";
volatile bool newCalibrationReceived = false;
bool calibrating = false;

// defined in the proximity tab
extern bool alertActive;
extern bool gestureWindowActive;
extern bool gestureArmed;

// defined in the exercise tab
extern bool exerciseActive;

void connectWiFi()
{
  if (!ENABLE_SERVER_UPLOAD)
  {
    return;
  }
  if (strlen(WIFI_SSID) == 0)
  {
    Serial.println("WiFi: set WIFI_SSID in base.ino");
    return;
  }
  Serial.println("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 25000UL)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  lastWifiAttempt = millis();
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("WiFi OK ESP IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
  }
  else
  {
    Serial.println("WiFi failed — fix SSID/pass; uploads skipped until connected");
  }
}

void ensureWiFi()
{
  if (!ENABLE_SERVER_UPLOAD || strlen(WIFI_SSID) == 0)
  {
    return;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }
  if (millis() - lastWifiAttempt < WIFI_RETRY_INTERVAL_MS)
  {
    return;
  }
  Serial.println("WiFi reconnecting...");
  WiFi.disconnect();
  delay(100);
  connectWiFi();
}

void uploadServerEvent(const char *event, int state)
{
  if (!ENABLE_SERVER_UPLOAD || event == nullptr)
  {
    return;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("upload skip: WiFi down (check Serial for WiFi OK / failed)");
    return;
  }

  HTTPClient http;
  String url = String(SERVER_PHP_URL);
  url += "?event=";
  url += event;
  url += "&state=";
  url += String(state);

  Serial.print("HTTP GET ");
  Serial.println(url);

  http.begin(url);
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  int code = http.GET();
  if (code > 0)
  {
    Serial.print("HTTP ");
    Serial.print(code);
    Serial.print(" ");
    Serial.println(http.getString());
  }
  else
  {
    Serial.print("HTTP fail code=");
    Serial.print(code);
    Serial.print(" ");
    Serial.println(http.errorToString(code));
  }
  http.end();
}

void turnOffGestureLEDs()
{
  digitalWrite(LED_LIGHT_PIN, LOW);
  digitalWrite(LED_FAN_PIN, LOW);
  digitalWrite(LED_BULB_PIN, LOW);
}

void drawBLEStatus()
{
  display.fillRect(0, 0, 66, 8, SSD1306_BLACK);
  display.fillRect(118, 0, 10, 8, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("2024ht01506");
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

// Called from BLE disconnect: keep row 1, drop stale live values and main area
void notifyBleDisconnected()
{
  valueLine = "";

  if (calibrating)
  {
    calibrating = false;
  }

  if (alertActive)
  {
    showAlert();
    return;
  }

  if (exerciseActive)
  {
    stopExercise();
    return;
  }

  showingGesture = false;
  showingLed = false;
  turnOffGestureLEDs();
  drawBLEStatus();
  display.fillRect(0, 8, 128, 56, SSD1306_BLACK);
  display.display();
}

void drawValueLine()
{
  if (!showValues || exerciseActive)
  {
    return;
  }
  display.fillRect(0, 44, 128, 8, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 44);
  display.print(valueLine);
}

void showNoAction()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print("NO ACTION");
  drawValueLine();
  display.display();
}

void showGesture()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print(gestureLabel);
  drawValueLine();
  display.display();
}

void processCommand(String command)
{
  if (calibrating || alertActive || exerciseActive)
  {
    return;
  }

  if (proximity && !gestureWindowActive)
  {
    return;
  }

  bool ledX = false;
  bool ledY = false;
  bool ledZ = false;

  if (command == "G1")
  {
    gestureLabel = "wave up";
    ledX = true;
  }
  else if (command == "G2")
  {
    gestureLabel = "wave down";
    ledX = true;
  }
  else if (command == "G3")
  {
    gestureLabel = "twist right";
    ledY = true;
  }
  else if (command == "G4")
  {
    gestureLabel = "twist left";
    ledY = true;
  }
  else if (command == "G5")
  {
    gestureLabel = "sweep right";
    ledZ = true;
  }
  else if (command == "G6")
  {
    gestureLabel = "sweep left";
    ledZ = true;
  }
  else if (command == "G7")
  {
    gestureLabel = "flipped wave up";
    ledX = true;
  }
  else if (command == "G8")
  {
    gestureLabel = "flipped wave down";
    ledX = true;
  }
  else if (command == "G9")
  {
    gestureLabel = "flipped sweep right";
    ledZ = true;
  }
  else if (command == "G10")
  {
    gestureLabel = "flipped sweep left";
    ledZ = true;
  }
  else if (moreCommands && command == "G11")
  {
    gestureLabel = "tap";
  }
  else if (moreCommands && command == "G12")
  {
    gestureLabel = "push";
  }
  else if (moreCommands && command == "G13")
  {
    gestureLabel = "pull";
  }
  else if (moreCommands && command == "G14")
  {
    gestureLabel = "nudge right";
  }
  else if (moreCommands && command == "G15")
  {
    gestureLabel = "nudge left";
  }
  else
  {
    return;
  }

  if (proximity)
  {
    gestureWindowActive = false;
    gestureArmed = false;
  }

  if (ledX || ledY || ledZ)
  {
    digitalWrite(LED_LIGHT_PIN, ledX ? HIGH : LOW);
    digitalWrite(LED_FAN_PIN, ledY ? HIGH : LOW);
    digitalWrite(LED_BULB_PIN, ledZ ? HIGH : LOW);
    lastLedMillis = millis();
    showingLed = true;
  }

  lastCommandMillis = millis();
  showingGesture = true;
  showGesture();

  // arthritis.php maps G1,G2,G7,G8->light; G3,G4,G9,G10->fan; G5,G6->bulb; G11-G15 + ALERT->alert
  uploadServerEvent(command.c_str(), 1);
}

void showCalibration(const char *line, char second)
{
  calibrating = true;
  showingGesture = false;
  showingLed = false;
  turnOffGestureLEDs();
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print(line);
  if (second >= '1' && second <= '5')
  {
    display.setCursor(0, 44);
    display.print(second);
  }
  display.display();
}

void showCalibrationResult(const char *line, const char *detail)
{
  showCalibration(line, '0');
  display.setCursor(0, 44);
  display.print(detail);
  display.display();
}

void processCalibration(String command)
{
  if (exerciseActive)
  {
    cancelExercise();
  }

  if (alertActive)
  {
    if (command == "CD")
    {
      calibrating = false;
    }
    return;
  }

  if (command.startsWith("CN") && command.length() == 3)
  {
    showCalibration("CAL: HOLD STILL", command.charAt(2));
  }
  else if (command.startsWith("CF") && command.length() == 3)
  {
    showCalibration("CAL: FLIP ARM", command.charAt(2));
  }
  else if (command.startsWith("CS"))
  {
    String detail = "N ";
    detail += command.substring(2);
    detail.replace(",", " F ");
    showCalibrationResult("CAL SUCCESS", detail.c_str());
  }
  else if (command.startsWith("CI"))
  {
    String detail = "BAD G ";
    detail += command.substring(2);
    showCalibrationResult("DEFAULT VALUE", detail.c_str());
  }
  else if (command == "CM")
  {
    showCalibrationResult("DEFAULT VALUE", "MOVING");
  }
  else if (command == "CX")
  {
    showCalibrationResult("DEFAULT VALUE", "FLIP TOO SMALL");
  }
  else if (command == "CD")
  {
    calibrating = false;
    showNoAction();
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
  showNoAction();

  // led outputs
  pinMode(LED_LIGHT_PIN, OUTPUT);
  pinMode(LED_FAN_PIN, OUTPUT);
  pinMode(LED_BULB_PIN, OUTPUT);
  turnOffGestureLEDs();

  setupProximity();

  connectWiFi();

  // BLE
  NimBLEDevice::init("ARTHRITIS_RECEIVER");

  // connect to band
  connectToBand();
}

/* =========================================================
   LOOP
   ========================================================= */

void loop()
{
  ensureWiFi();

  // BLE
  checkBluetooth();

  updateUltrasonic();
  handleGestureWindow();
  handleAlertTimer();

  if (newCalibrationReceived)
  {
    noInterrupts();
    String calMessage = receivedCalibration;
    newCalibrationReceived = false;
    interrupts();
    processCalibration(calMessage);
  }

  if (newCommandReceived)
  {
    noInterrupts();
    String command = receivedCommand;
    newCommandReceived = false;
    interrupts();
    if (exerciseActive)
    {
      handleExerciseCommand(command);
    }
    else
    {
      processCommand(command);
    }
  }

  handleExerciseTimer();

  if (
    !alertActive &&
    !exerciseActive &&
    showingLed &&
    millis() - lastLedMillis >= NO_ACTION_TIMEOUT
  )
  {
    showingLed = false;
    turnOffGestureLEDs();
  }

  if (
    !alertActive &&
    !exerciseActive &&
    showingGesture &&
    millis() - lastCommandMillis >= NO_ACTION_TIMEOUT
  )
  {
    showingGesture = false;
    showNoAction();
  }

  if (newValuesReceived)
  {
    noInterrupts();
    newValuesReceived = false;
    interrupts();
    drawValueLine();
  }

  // BLE status
  drawBLEStatus();
  display.display();
  delay(20);
}
