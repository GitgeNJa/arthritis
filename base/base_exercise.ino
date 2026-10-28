/* =========================================================
   EXERCISE
   ========================================================= */

#define EXERCISE_STILL_MS 10000UL
#define EXERCISE_STILL_SECONDS 10
#define EXERCISE_SUCCESS_MS 3000UL

bool exerciseActive = false;
int exercisePhase = 0;
int exerciseStep = 0;
int exerciseShownSecond = -1;
unsigned long exercisePhaseStart = 0;

void showExerciseStill(int secondLeft)
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print("HOLD STILL");
  display.setCursor(0, 44);
  display.print(secondLeft);
  display.display();
}

void drawExerciseWord(int x, int y, const char *word, bool done)
{
  int width = strlen(word) * 6;
  if (done)
  {
    display.fillRect(x, y, width, 8, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  else
  {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(x, y);
  display.print(word);
}

void showExerciseMoves()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  if (exercisePhase == 3)
  {
    drawExerciseWord(0, 22, "twist left", exerciseStep > 0);
    drawExerciseWord(0, 44, "flipped sweep left", false);
  }
  else
  {
    drawExerciseWord(0, 22, "wave up", exerciseStep > 0);
    drawExerciseWord(0, 44, "sweep right", false);
  }
  display.display();
}

void showExerciseSuccess()
{
  display.clearDisplay();
  drawBLEStatus();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print("SUCCESS");
  display.display();
}

void cancelExercise()
{
  exerciseActive = false;
  exercisePhase = 0;
  exerciseStep = 0;
}

void stopExercise()
{
  if (!exerciseActive)
  {
    return;
  }

  cancelExercise();
  gestureWindowActive = false;
  showingGesture = false;
  Serial.println("Exercise stopped");
  if (!alertActive)
  {
    showNoAction();
  }
}

void startExercise()
{
  if (!exercise || exerciseActive || calibrating || alertActive)
  {
    return;
  }

  exerciseActive = true;
  exercisePhase = 1;
  exerciseStep = 0;
  exercisePhaseStart = millis();
  exerciseShownSecond = EXERCISE_STILL_SECONDS;
  gestureWindowActive = false;
  showingGesture = false;
  showingLed = false;
  showExerciseStill(EXERCISE_STILL_SECONDS);
  Serial.println("Exercise started");
}

void handleExerciseCommand(String command)
{
  if (!exerciseActive || alertActive)
  {
    return;
  }

  if (exercisePhase == 1)
  {
    exercisePhaseStart = millis();
    exerciseShownSecond = EXERCISE_STILL_SECONDS;
    showExerciseStill(EXERCISE_STILL_SECONDS);
    return;
  }

  if (exercisePhase != 2 && exercisePhase != 3)
  {
    return;
  }

  bool correct = false;
  if (exercisePhase == 2)
  {
    correct = exerciseStep == 0 ? command == "G1" : command == "G5";
  }
  else
  {
    correct = exerciseStep == 0 ? command == "G4" : command == "G10";
  }

  if (!correct)
  {
    exerciseStep = 0;
    showExerciseMoves();
    return;
  }

  exerciseStep++;
  if (exerciseStep < 2)
  {
    showExerciseMoves();
    return;
  }

  if (exercisePhase == 2)
  {
    exercisePhase = 3;
    exerciseStep = 0;
    showExerciseMoves();
    return;
  }

  exercisePhase = 4;
  exercisePhaseStart = millis();
  showExerciseSuccess();
  Serial.println("Exercise success");
}

void handleExerciseTimer()
{
  if (!exerciseActive)
  {
    return;
  }

  if (calibrating || alertActive)
  {
    cancelExercise();
    return;
  }

  if (exercisePhase == 1)
  {
    unsigned long elapsed = millis() - exercisePhaseStart;
    if (elapsed >= EXERCISE_STILL_MS)
    {
      exercisePhase = 2;
      exerciseStep = 0;
      showExerciseMoves();
      return;
    }

    int secondLeft = EXERCISE_STILL_SECONDS - (int)(elapsed / 1000UL);
    if (secondLeft != exerciseShownSecond)
    {
      exerciseShownSecond = secondLeft;
      showExerciseStill(secondLeft);
    }
    return;
  }

  if (
    exercisePhase == 4 &&
    millis() - exercisePhaseStart >= EXERCISE_SUCCESS_MS
  )
  {
    stopExercise();
  }
}
