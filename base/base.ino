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

/* =========================================================
   ORIENTATION
   ========================================================= */

bool bleConnected = false;

volatile bool newOrientationReceived = false;
String receivedOrientation = "";

float orientX = 0.0;
float orientY = 0.0;
float orientZ = 0.0;
bool hasOrientation = false;

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

  // X, Y, Z motion on LED1, LED2, and LED3
  digitalWrite(LED_LIGHT_PIN, orientX != 0.0 ? HIGH : LOW);
  digitalWrite(LED_FAN_PIN, orientY != 0.0 ? HIGH : LOW);
  digitalWrite(LED_HELP_PIN, orientZ != 0.0 ? HIGH : LOW);

  showNoAction();
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

  if (newOrientationReceived)
  {
    noInterrupts();
    String data = receivedOrientation;
    newOrientationReceived = false;
    interrupts();
    processOrientation(data);
  }

  // BLE status
  drawBLEStatus();
  display.display();
  delay(20);
}
