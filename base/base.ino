#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBLEDevice.h>

// false keeps G1-G10. true also shows tap, push, and pull.
// The band has the same switch. Set both the same and upload both.
bool moreCommands = false;

// false: accept every command, for debugging.
// true: accept a command only while the right hand is over the sensor.
bool proximity = false;

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
#define LED_HELP_PIN  27

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

// defined in the proximity tab
extern bool alertActive;
extern bool gestureWindowActive;
extern bool gestureArmed;

void turnOffGestureLEDs()
{
  digitalWrite(LED_LIGHT_PIN, LOW);
  digitalWrite(LED_FAN_PIN, LOW);
  digitalWrite(LED_HELP_PIN, LOW);
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

void showNoAction()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print("NO ACTION");
  display.display();
}

void showGesture()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 44);
  display.print(gestureLabel);
  display.display();
}

void processCommand(String command)
{
  if (proximity && (alertActive || !gestureWindowActive))
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
    digitalWrite(LED_HELP_PIN, ledZ ? HIGH : LOW);
    lastLedMillis = millis();
    showingLed = true;
  }

  lastCommandMillis = millis();
  showingGesture = true;
  showGesture();
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
  pinMode(LED_HELP_PIN, OUTPUT);
  turnOffGestureLEDs();

  setupProximity();

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
  // BLE
  checkBluetooth();

  updateUltrasonic();
  handleGestureWindow();
  handleAlertTimer();

  if (newCommandReceived)
  {
    noInterrupts();
    String command = receivedCommand;
    newCommandReceived = false;
    interrupts();
    processCommand(command);
  }

  if (
    showingLed &&
    millis() - lastLedMillis >= NO_ACTION_TIMEOUT
  )
  {
    showingLed = false;
    turnOffGestureLEDs();
  }

  if (
    !alertActive &&
    showingGesture &&
    millis() - lastCommandMillis >= NO_ACTION_TIMEOUT
  )
  {
    showingGesture = false;
    showNoAction();
  }

  // BLE status
  drawBLEStatus();
  display.display();
  delay(20);
}