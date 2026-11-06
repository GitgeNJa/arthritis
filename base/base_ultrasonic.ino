/* =========================================================
   PROXIMITY
   ========================================================= */

#define TRIG_PIN 5
#define ECHO_PIN 18
#define LED_ALERT_PIN 14

#define MIN_GESTURE_DISTANCE 3.0
#define MAX_GESTURE_DISTANCE 20.0
#define TOO_CLOSE_DISTANCE 3.0

#define ULTRASONIC_READ_INTERVAL 150UL
#define REQUIRED_VALID_SAMPLES 2
#define GESTURE_WAIT_TIME 2000UL
#define EXERCISE_TRIGGER_TIME 3000UL
#define CLOSE_ALERT_TIME 10000UL
#define ALERT_DISPLAY_TIME 3000UL

bool gestureArmed = true;
bool gestureWindowActive = false;
unsigned long gestureWindowStart = 0;

unsigned long lastUltrasonicRead = 0;
int validRangeSamples = 0;

bool closeTiming = false;
unsigned long closeStartTime = 0;
bool closeExerciseDone = false;
bool closeAlertDone = false;

bool alertActive = false;
unsigned long alertStartTime = 0;

void setupProximity()
{
  if (!proximity && !exercise)
  {
    return;
  }

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_ALERT_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(LED_ALERT_PIN, LOW);
}

float getDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0)
  {
    return 999.0;
  }

  return duration * 0.0343 / 2.0;
}

void showAlert()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print("ALERT");
  drawValueLine();
  display.display();
}

void startAlert()
{
  alertActive = true;
  alertStartTime = millis();
  gestureWindowActive = false;
  showingGesture = false;
  showingLed = false;

  // leave the light, fan, and bulb LEDs as they are
  stopExercise();
  digitalWrite(LED_ALERT_PIN, HIGH);
  showAlert();
  Serial.println("ALERT TRIGGERED");
}

void handleAlertTimer()
{
  if (!alertActive)
  {
    return;
  }

  if (millis() - alertStartTime >= ALERT_DISPLAY_TIME)
  {
    alertActive = false;
    digitalWrite(LED_ALERT_PIN, LOW);
    if (!calibrating)
    {
      showNoAction();
    }
    Serial.println("Alert finished");
  }
}

void handleGestureWindow()
{
  if (!proximity || !gestureWindowActive)
  {
    return;
  }

  if (millis() - gestureWindowStart >= GESTURE_WAIT_TIME)
  {
    gestureWindowActive = false;
    Serial.println("Gesture window expired");
    if (!showingGesture && !alertActive && !exerciseActive)
    {
      showNoAction();
    }
  }
}

void handleUltrasonic(float distance)
{
  if (calibrating)
  {
    closeTiming = false;
    closeExerciseDone = false;
    closeAlertDone = false;
    return;
  }

  if (distance >= 900.0 || distance < TOO_CLOSE_DISTANCE)
  {
    if (!closeTiming)
    {
      closeTiming = true;
      closeStartTime = millis();
      closeExerciseDone = false;
      closeAlertDone = false;
    }

    unsigned long held = millis() - closeStartTime;

    if (
      exercise &&
      !closeExerciseDone &&
      held >= EXERCISE_TRIGGER_TIME
    )
    {
      closeExerciseDone = true;
      if (!alertActive)
      {
        if (exerciseActive)
        {
          stopExercise();
        }
        else
        {
          startExercise();
        }
      }
    }

    if (
      proximity &&
      !closeAlertDone &&
      held >= CLOSE_ALERT_TIME
    )
    {
      closeAlertDone = true;
      startAlert();
    }
    return;
  }

  closeTiming = false;
  closeExerciseDone = false;
  closeAlertDone = false;

  if (!proximity)
  {
    return;
  }

  if (
    distance >= MIN_GESTURE_DISTANCE &&
    distance <= MAX_GESTURE_DISTANCE
  )
  {
    if (
      gestureArmed &&
      !showingGesture &&
      !gestureWindowActive &&
      !alertActive &&
      !exerciseActive
    )
    {
      validRangeSamples++;
      if (validRangeSamples >= REQUIRED_VALID_SAMPLES)
      {
        validRangeSamples = 0;
        gestureWindowActive = true;
        gestureWindowStart = millis();
        Serial.println("Gesture window OPEN");
      }
    }
  }
  else
  {
    validRangeSamples = 0;
  }

  if (distance > MAX_GESTURE_DISTANCE)
  {
    validRangeSamples = 0;
    gestureWindowActive = false;
    if (!gestureArmed)
    {
      gestureArmed = true;
      Serial.println("GESTURE SYSTEM RE-ARMED");
    }
  }
}

void updateUltrasonic()
{
  if (!proximity && !exercise)
  {
    return;
  }

  if (millis() - lastUltrasonicRead >= ULTRASONIC_READ_INTERVAL)
  {
    lastUltrasonicRead = millis();
    handleUltrasonic(getDistance());
  }
}
