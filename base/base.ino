#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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
volatile bool newCalibrationReceived = false;
String receivedCalibration = "";
String gestureLabel = "";
unsigned long lastCommandMillis = 0;
bool showingGesture = false;
volatile bool newReadingReceived = false;
String receivedReading = "";
String gravityLine = "";
String gyroLine = "";
String statusLine = "NO ACTION";
bool calibrationCountdown = false;
char calibrationSecond = ' ';

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

int readSigned(const String &reading, int offset, int digits)
{
  int value = 0;
  for (int i = 1; i <= digits; i++)
  {
    value = value * 10 + (reading.charAt(offset + i) - '0');
  }
  if (reading.charAt(offset) == '-')
  {
    value = -value;
  }
  return value;
}

void drawScreen()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(gravityLine);
  drawBLEStatus();
  display.setCursor(0, 22);
  display.print(statusLine);
  display.setCursor(0, 44);
  if (calibrationCountdown)
  {
    display.print(calibrationSecond);
  }
  else
  {
    display.print(gyroLine);
  }
  display.display();
}

void showNoAction()
{
  statusLine = "NO ACTION";
  calibrationCountdown = false;
  drawScreen();
}

void showCalibration(const char *line, char second)
{
  statusLine = line;
  calibrationCountdown = (second >= '1' && second <= '5');
  calibrationSecond = second;
  drawScreen();
}

void processCalibration(String command)
{
  showingGesture = false;
  turnOffGestureLEDs();

  if (command.startsWith("CN") && command.length() == 3)
  {
    showCalibration("CAL: HOLD STILL", command.charAt(2));
  }
  else if (command.startsWith("CF") && command.length() == 3)
  {
    showCalibration("CAL: FLIP ARM", command.charAt(2));
  }
  else if (command == "CX")
  {
    showCalibration("FLIP TOO SMALL", '0');
  }
  else if (command == "CD")
  {
    showNoAction();
  }
}

void showGesture()
{
  statusLine = gestureLabel;
  calibrationCountdown = false;
  drawScreen();
}

void processReading(String reading)
{
  if (reading.length() != 20 || reading.charAt(0) != 'W')
  {
    return;
  }

  int tenths = readSigned(reading, 1, 3);
  int mag = tenths;
  if (mag < 0)
  {
    mag = -mag;
  }
  char grav[12];
  snprintf(grav, sizeof(grav), "Gz %s%d.%d", tenths < 0 ? "-" : "", mag / 10, mag % 10);
  gravityLine = grav;

  if (!calibrationCountdown)
  {
    int gx = readSigned(reading, 5, 4);
    int gy = readSigned(reading, 10, 4);
    int gzz = readSigned(reading, 15, 4);
    char gyro[24];
    snprintf(gyro, sizeof(gyro), "x%d y%d z%d", gx, gy, gzz);
    gyroLine = gyro;
  }

  drawScreen();
}

void processCommand(String command)
{
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
  else
  {
    return;
  }

  digitalWrite(LED_LIGHT_PIN, ledX ? HIGH : LOW);
  digitalWrite(LED_FAN_PIN, ledY ? HIGH : LOW);
  digitalWrite(LED_HELP_PIN, ledZ ? HIGH : LOW);
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

  if (newCalibrationReceived)
  {
    noInterrupts();
    String command = receivedCalibration;
    newCalibrationReceived = false;
    interrupts();
    processCalibration(command);
  }

  if (newReadingReceived)
  {
    noInterrupts();
    String reading = receivedReading;
    newReadingReceived = false;
    interrupts();
    processReading(reading);
  }

  if (newCommandReceived)
  {
    noInterrupts();
    String command = receivedCommand;
    newCommandReceived = false;
    interrupts();
    processCommand(command);
  }

  if (
    showingGesture &&
    millis() - lastCommandMillis >= NO_ACTION_TIMEOUT
  )
  {
    showingGesture = false;
    turnOffGestureLEDs();
    showNoAction();
  }

  // BLE status
  drawBLEStatus();
  display.display();
  delay(20);
}