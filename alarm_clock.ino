#include <LiquidCrystal.h>
#include <IRremote.hpp>
#include <Wire.h>
#include <RTClib.h>

// ---------------- PIN MAP ----------------
const int LCD_RS = 7;
const int LCD_E  = 6;
const int LCD_D4 = 5;
const int LCD_D5 = 4;
const int LCD_D6 = 3;
const int LCD_D7 = 2;

const int BUZZER_PIN = 8;
const int RED_PIN    = 9;
const int GREEN_PIN  = 10;
const int BLUE_PIN   = 11;
const int IR_PIN     = 12;

// DS1307:
// SDA -> A4
// SCL -> A5

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
RTC_DS1307 rtc;

// ---------------- REMOTE CODES ----------------
const uint8_t CMD_POWER    = 0x45;
const uint8_t CMD_VOL_UP   = 0x46;
const uint8_t CMD_STOP     = 0x47;
const uint8_t CMD_PREV     = 0x44;
const uint8_t CMD_START    = 0x40;
const uint8_t CMD_NEXT     = 0x43;
const uint8_t CMD_DOWN     = 0x07;
const uint8_t CMD_VOL_DOWN = 0x15;
const uint8_t CMD_UP       = 0x09;
const uint8_t CMD_0        = 0x16;
const uint8_t CMD_EQ       = 0x19;
const uint8_t CMD_REPEAT   = 0x0D;
const uint8_t CMD_1        = 0x0C;
const uint8_t CMD_2        = 0x18;
const uint8_t CMD_3        = 0x5E;
const uint8_t CMD_4        = 0x08;
const uint8_t CMD_5        = 0x1C;
const uint8_t CMD_6        = 0x5A;
const uint8_t CMD_7        = 0x42;
const uint8_t CMD_8        = 0x52;
const uint8_t CMD_9        = 0x4A;

// ---------------- SYSTEM ----------------
bool systemOn = true;
bool rtcReady = false;
bool alarmEnabled = true;
bool alarmActive = false;
bool timerRunning = false;

unsigned long lastIrAccept = 0;
unsigned long introTimer = 0;
unsigned long lastClockRefresh = 0;
unsigned long lastTimerTick = 0;
unsigned long lastAlarmBlink = 0;
unsigned long lastAlarmBeep = 0;

bool alarmLedState = false;
bool alarmSoundState = false;

// saved settings
int buzzerFrequency = 1000;
int rgbMode = 0; // 0 red, 1 green, 2 blue, 3 white

int timerMinutes = 0;
int timerSeconds = 10;

int alarmHour = 7;
int alarmMinute = 0;
bool alarmPM = false;

// timer live countdown
int countdownMinutes = 0;
int countdownSeconds = 0;

// temp edit values
int tempTimerMinutes = 0;
int tempTimerSeconds = 10;

int tempAlarmHour = 7;
int tempAlarmMinute = 0;
bool tempAlarmPM = false;

int tempRgbMode = 0;
int tempBuzzerFrequency = 1000;

int editPosition = 0;

// prevent alarm repeating in same minute
int lastAlarmMinuteTriggered = -1;
int lastAlarmHourTriggered = -1;
bool lastAlarmPMTriggered = false;

// software buzzer square wave
bool buzzerPinState = false;
unsigned long lastBuzzerToggleMicros = 0;
unsigned long buzzerHalfPeriodMicros = 500;

// ---------------- UI ----------------
enum ScreenState {
  SCREEN_INTRO_HELLO,
  SCREEN_INTRO_LOADING,
  SCREEN_MENU,
  SCREEN_CURRENT_TIME,
  SCREEN_SETUP_TIMER,
  SCREEN_SETUP_ALARM,
  SCREEN_ACTIVE_ALARM,
  SCREEN_ACTIVE_TIMER,
  SCREEN_FREQ,
  SCREEN_RGB
};

ScreenState currentScreen = SCREEN_INTRO_HELLO;

const int MENU_COUNT = 5;
const char* menuItems[MENU_COUNT] = {
  "Current Time",
  "Setup Timer",
  "Setup Alarm",
  "Active Alarm",
  "Active Timer"
};
int menuIndex = 0;

// ---------------- LCD CACHE ----------------
char lastLine1[17] = "";
char lastLine2[17] = "";

// ---------------- HELPERS ----------------
void clearLcdCache() {
  lastLine1[0] = '\0';
  lastLine2[0] = '\0';
}

void setLines(const char* l1, const char* l2) {
  if (strcmp(l1, lastLine1) == 0 && strcmp(l2, lastLine2) == 0) return;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);

  strncpy(lastLine1, l1, 16);
  strncpy(lastLine2, l2, 16);
  lastLine1[16] = '\0';
  lastLine2[16] = '\0';
}

void setRGB(bool r, bool g, bool b) {
  digitalWrite(RED_PIN, r);
  digitalWrite(GREEN_PIN, g);
  digitalWrite(BLUE_PIN, b);
}

void applyRGBMode(int mode) {
  switch (mode) {
    case 0: setRGB(HIGH, LOW, LOW); break;
    case 1: setRGB(LOW, HIGH, LOW); break;
    case 2: setRGB(LOW, LOW, HIGH); break;
    case 3: setRGB(HIGH, HIGH, HIGH); break;
    default: setRGB(LOW, LOW, LOW); break;
  }
}

const char* rgbName(int mode) {
  switch (mode) {
    case 0: return "Red";
    case 1: return "Green";
    case 2: return "Blue";
    case 3: return "White";
    default: return "Unknown";
  }
}

void updateBuzzerPeriod() {
  if (buzzerFrequency < 100) buzzerFrequency = 100;
  buzzerHalfPeriodMicros = 1000000UL / (2UL * (unsigned long)buzzerFrequency);
}

bool isDigitCommand(uint8_t cmd) {
  return cmd == CMD_0 || cmd == CMD_1 || cmd == CMD_2 || cmd == CMD_3 || cmd == CMD_4 ||
         cmd == CMD_5 || cmd == CMD_6 || cmd == CMD_7 || cmd == CMD_8 || cmd == CMD_9;
}

int commandToDigit(uint8_t cmd) {
  switch (cmd) {
    case CMD_0: return 0;
    case CMD_1: return 1;
    case CMD_2: return 2;
    case CMD_3: return 3;
    case CMD_4: return 4;
    case CMD_5: return 5;
    case CMD_6: return 6;
    case CMD_7: return 7;
    case CMD_8: return 8;
    case CMD_9: return 9;
    default: return -1;
  }
}

int getDisplayHour12(const DateTime& now) {
  int h = now.hour() % 12;
  if (h == 0) h = 12;
  return h;
}

bool getCurrentPM(const DateTime& now) {
  return now.hour() >= 12;
}

void stopAlarmNow() {
  alarmActive = false;
  alarmLedState = false;
  alarmSoundState = false;
  buzzerPinState = false;
  digitalWrite(BUZZER_PIN, LOW);
  applyRGBMode(rgbMode);
}

void stopTimerNow() {
  timerRunning = false;
  countdownMinutes = timerMinutes;
  countdownSeconds = timerSeconds;
}

void hardShutdown() {
  systemOn = false;
  stopAlarmNow();
  stopTimerNow();
  setRGB(LOW, LOW, LOW);
  lcd.noBlink();
  lcd.noCursor();
  clearLcdCache();
  setLines("SYSTEM OFF", "Power=Enable");
}

void startIntro() {
  systemOn = true;
  stopAlarmNow();
  currentScreen = SCREEN_INTRO_HELLO;
  introTimer = millis();
  lcd.noBlink();
  lcd.noCursor();
  clearLcdCache();
  setLines("Hello, Jay", "");
}

void beginTimerEdit() {
  tempTimerMinutes = timerMinutes;
  tempTimerSeconds = timerSeconds;
  editPosition = 0;
  currentScreen = SCREEN_SETUP_TIMER;
}

void beginAlarmEdit() {
  tempAlarmHour = alarmHour;
  tempAlarmMinute = alarmMinute;
  tempAlarmPM = alarmPM;
  editPosition = 0;
  currentScreen = SCREEN_SETUP_ALARM;
}

void beginFreqEdit() {
  tempBuzzerFrequency = buzzerFrequency;
  currentScreen = SCREEN_FREQ;
}

void beginRgbEdit() {
  tempRgbMode = rgbMode;
  applyRGBMode(tempRgbMode);
  currentScreen = SCREEN_RGB;
}

void editTimerDigit(int digit) {
  if (editPosition == 0) tempTimerMinutes = (digit * 10) + (tempTimerMinutes % 10);
  if (editPosition == 1) tempTimerMinutes = ((tempTimerMinutes / 10) * 10) + digit;
  if (editPosition == 2) tempTimerSeconds = (digit * 10) + (tempTimerSeconds % 10);
  if (editPosition == 3) tempTimerSeconds = ((tempTimerSeconds / 10) * 10) + digit;

  if (tempTimerMinutes > 99) tempTimerMinutes = 99;
  if (tempTimerSeconds > 59) tempTimerSeconds = 59;
}

void editAlarmDigit(int digit) {
  if (editPosition == 0) tempAlarmHour = (digit * 10) + (tempAlarmHour % 10);
  if (editPosition == 1) tempAlarmHour = ((tempAlarmHour / 10) * 10) + digit;
  if (editPosition == 2) tempAlarmMinute = (digit * 10) + (tempAlarmMinute % 10);
  if (editPosition == 3) tempAlarmMinute = ((tempAlarmMinute / 10) * 10) + digit;

  if (tempAlarmHour < 1) tempAlarmHour = 1;
  if (tempAlarmHour > 12) tempAlarmHour = 12;
  if (tempAlarmMinute > 59) tempAlarmMinute = 59;
}

void saveTimerSettings() {
  timerMinutes = tempTimerMinutes;
  timerSeconds = tempTimerSeconds;
  countdownMinutes = timerMinutes;
  countdownSeconds = timerSeconds;
}

void saveAlarmSettings() {
  alarmHour = tempAlarmHour;
  alarmMinute = tempAlarmMinute;
  alarmPM = tempAlarmPM;
  alarmEnabled = true;
}

void startSavedTimer() {
  countdownMinutes = timerMinutes;
  countdownSeconds = timerSeconds;
  if (countdownMinutes == 0 && countdownSeconds == 0) return;
  timerRunning = true;
  lastTimerTick = millis();
}

void moveMenuUp() {
  menuIndex--;
  if (menuIndex < 0) menuIndex = MENU_COUNT - 1;
}

void moveMenuDown() {
  menuIndex++;
  if (menuIndex >= MENU_COUNT) menuIndex = 0;
}

void placeEditCursor() {
  lcd.noCursor();
  lcd.noBlink();
  if (currentScreen == SCREEN_SETUP_TIMER || currentScreen == SCREEN_SETUP_ALARM) {
    lcd.setCursor(editPosition < 2 ? editPosition : editPosition + 1, 1);
    lcd.cursor();
    lcd.blink();
  }
}

// ---------------- RENDER ----------------
void renderScreen() {
  char line1[17];
  char line2[17];

  if (!systemOn) {
    lcd.noBlink();
    lcd.noCursor();
    setLines("SYSTEM OFF", "Power=Enable");
    return;
  }

  switch (currentScreen) {
    case SCREEN_INTRO_HELLO:
      lcd.noBlink();
      lcd.noCursor();
      setLines("Hello, Jay", "");
      break;

    case SCREEN_INTRO_LOADING:
      lcd.noBlink();
      lcd.noCursor();
      setLines("Loading Clock", "");
      break;

    case SCREEN_MENU:
      lcd.noBlink();
      lcd.noCursor();
      snprintf(line1, sizeof(line1), "Menu:");
      snprintf(line2, sizeof(line2), "%s", menuItems[menuIndex]);
      setLines(line1, line2);
      break;

    case SCREEN_CURRENT_TIME:
      lcd.noBlink();
      lcd.noCursor();
      if (rtcReady) {
        DateTime now = rtc.now();
        int hour12 = getDisplayHour12(now);
        bool pm = getCurrentPM(now);
        snprintf(line1, sizeof(line1), "%02d:%02d:%02d %s", hour12, now.minute(), now.second(), pm ? "PM" : "AM");
        snprintf(line2, sizeof(line2), "%02d/%02d/%04d", now.month(), now.day(), now.year());
      } else {
        snprintf(line1, sizeof(line1), "RTC Not Found");
        snprintf(line2, sizeof(line2), "Check A4/A5");
      }
      setLines(line1, line2);
      break;

    case SCREEN_SETUP_TIMER:
      snprintf(line1, sizeof(line1), "Set Timer");
      snprintf(line2, sizeof(line2), "%02d:%02d", tempTimerMinutes, tempTimerSeconds);
      setLines(line1, line2);
      placeEditCursor();
      break;

    case SCREEN_SETUP_ALARM:
      snprintf(line1, sizeof(line1), "Set Alarm");
      snprintf(line2, sizeof(line2), "%02d:%02d %s", tempAlarmHour, tempAlarmMinute, tempAlarmPM ? "PM" : "AM");
      setLines(line1, line2);
      placeEditCursor();
      break;

    case SCREEN_ACTIVE_ALARM:
      lcd.noBlink();
      lcd.noCursor();
      snprintf(line1, sizeof(line1), alarmEnabled ? "Alarm: ON" : "Alarm: OFF");
      snprintf(line2, sizeof(line2), "%02d:%02d %s", alarmHour, alarmMinute, alarmPM ? "PM" : "AM");
      setLines(line1, line2);
      break;

    case SCREEN_ACTIVE_TIMER:
      lcd.noBlink();
      lcd.noCursor();
      if (timerRunning) {
        snprintf(line1, sizeof(line1), "Timer Running");
        snprintf(line2, sizeof(line2), "%02d:%02d", countdownMinutes, countdownSeconds);
      } else {
        snprintf(line1, sizeof(line1), "Saved Timer");
        snprintf(line2, sizeof(line2), "%02d:%02d", timerMinutes, timerSeconds);
      }
      setLines(line1, line2);
      break;

    case SCREEN_FREQ:
      lcd.noBlink();
      lcd.noCursor();
      snprintf(line1, sizeof(line1), "Freq Preview");
      snprintf(line2, sizeof(line2), "%d Hz", tempBuzzerFrequency);
      setLines(line1, line2);
      break;

    case SCREEN_RGB:
      lcd.noBlink();
      lcd.noCursor();
      snprintf(line1, sizeof(line1), "RGB Preview");
      snprintf(line2, sizeof(line2), "%s", rgbName(tempRgbMode));
      setLines(line1, line2);
      break;
  }
}

// ---------------- ALARM + TIMER ----------------
void checkRealAlarm() {
  if (!systemOn || !rtcReady || !alarmEnabled || alarmActive) return;

  DateTime now = rtc.now();
  int currentHour12 = getDisplayHour12(now);
  bool currentPM = getCurrentPM(now);

  if (currentHour12 == alarmHour &&
      now.minute() == alarmMinute &&
      currentPM == alarmPM &&
      !(lastAlarmHourTriggered == currentHour12 &&
        lastAlarmMinuteTriggered == now.minute() &&
        lastAlarmPMTriggered == currentPM)) {

    alarmActive = true;
    lastAlarmHourTriggered = currentHour12;
    lastAlarmMinuteTriggered = now.minute();
    lastAlarmPMTriggered = currentPM;
    lastAlarmBlink = millis();
    lastAlarmBeep = millis();
  }
}

void updateAlarmEffects() {
  if (!alarmActive) return;

  unsigned long nowMs = millis();

  if (nowMs - lastAlarmBlink >= 250) {
    lastAlarmBlink = nowMs;
    alarmLedState = !alarmLedState;
    if (alarmLedState) setRGB(HIGH, HIGH, HIGH);
    else setRGB(LOW, LOW, LOW);
  }

  if (nowMs - lastAlarmBeep >= 180) {
    lastAlarmBeep = nowMs;
    alarmSoundState = !alarmSoundState;
  }
}

void updateBuzzerWave() {
  if (!alarmActive || !alarmSoundState) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerPinState = false;
    return;
  }

  unsigned long nowUs = micros();
  if (nowUs - lastBuzzerToggleMicros >= buzzerHalfPeriodMicros) {
    lastBuzzerToggleMicros = nowUs;
    buzzerPinState = !buzzerPinState;
    digitalWrite(BUZZER_PIN, buzzerPinState ? HIGH : LOW);
  }
}

void updateTimerCountdown() {
  if (!timerRunning || alarmActive || !systemOn) return;

  unsigned long nowMs = millis();
  if (nowMs - lastTimerTick >= 1000) {
    lastTimerTick += 1000;

    if (countdownSeconds == 0) {
      if (countdownMinutes == 0) {
        timerRunning = false;
        alarmActive = true;
        lastAlarmBlink = millis();
        lastAlarmBeep = millis();
        return;
      } else {
        countdownMinutes--;
        countdownSeconds = 59;
      }
    } else {
      countdownSeconds--;
    }

    if (currentScreen == SCREEN_ACTIVE_TIMER) {
      clearLcdCache();
      renderScreen();
    }
  }
}

// ---------------- REMOTE HANDLERS ----------------
void handleMenu(uint8_t cmd) {
  if (cmd == CMD_VOL_UP) moveMenuUp();
  else if (cmd == CMD_VOL_DOWN) moveMenuDown();
  else if (cmd == CMD_START) {
    switch (menuIndex) {
      case 0: currentScreen = SCREEN_CURRENT_TIME; break;
      case 1: beginTimerEdit(); break;
      case 2: beginAlarmEdit(); break;
      case 3: currentScreen = SCREEN_ACTIVE_ALARM; break;
      case 4: currentScreen = SCREEN_ACTIVE_TIMER; break;
    }
  }
}

void handleSetupTimer(uint8_t cmd) {
  if (cmd == CMD_PREV && editPosition > 0) editPosition--;
  else if (cmd == CMD_NEXT && editPosition < 3) editPosition++;
  else if (isDigitCommand(cmd)) editTimerDigit(commandToDigit(cmd));
  else if (cmd == CMD_START) {
    saveTimerSettings();
    currentScreen = SCREEN_ACTIVE_TIMER;
    lcd.noBlink();
    lcd.noCursor();
  }
  else if (cmd == CMD_STOP) {
    currentScreen = SCREEN_MENU;
    lcd.noBlink();
    lcd.noCursor();
  }
}

void handleSetupAlarm(uint8_t cmd) {
  if (cmd == CMD_PREV && editPosition > 0) editPosition--;
  else if (cmd == CMD_NEXT && editPosition < 3) editPosition++;
  else if (isDigitCommand(cmd)) editAlarmDigit(commandToDigit(cmd));
  else if (cmd == CMD_UP || cmd == CMD_DOWN) tempAlarmPM = !tempAlarmPM;
  else if (cmd == CMD_START) {
    saveAlarmSettings();
    currentScreen = SCREEN_ACTIVE_ALARM;
    lcd.noBlink();
    lcd.noCursor();
  }
  else if (cmd == CMD_STOP) {
    currentScreen = SCREEN_MENU;
    lcd.noBlink();
    lcd.noCursor();
  }
}

void handleActiveAlarm(uint8_t cmd) {
  if (cmd == CMD_UP || cmd == CMD_DOWN) {
    alarmEnabled = !alarmEnabled;
  } else if (cmd == CMD_START) {
    beginAlarmEdit();
  } else if (cmd == CMD_STOP) {
    currentScreen = SCREEN_MENU;
  }
}

void handleActiveTimer(uint8_t cmd) {
  if (cmd == CMD_START) {
    if (timerRunning) stopTimerNow();
    else startSavedTimer();
  } else if (cmd == CMD_STOP) {
    stopTimerNow();
    currentScreen = SCREEN_MENU;
  }
}

void handleCurrentTime(uint8_t cmd) {
  if (cmd == CMD_START || cmd == CMD_STOP) currentScreen = SCREEN_MENU;
}

void handleFreq(uint8_t cmd) {
  if (cmd == CMD_PREV || cmd == CMD_DOWN) {
    tempBuzzerFrequency -= 100;
    if (tempBuzzerFrequency < 100) tempBuzzerFrequency = 100;
  }
  else if (cmd == CMD_NEXT || cmd == CMD_UP) {
    tempBuzzerFrequency += 100;
    if (tempBuzzerFrequency > 5000) tempBuzzerFrequency = 5000;
  }
  else if (cmd == CMD_START) {
    buzzerFrequency = tempBuzzerFrequency;
    updateBuzzerPeriod();
    currentScreen = SCREEN_MENU;
  }
  else if (cmd == CMD_STOP) {
    currentScreen = SCREEN_MENU;
  }
}

void handleRgb(uint8_t cmd) {
  if (cmd == CMD_PREV || cmd == CMD_DOWN) {
    tempRgbMode--;
    if (tempRgbMode < 0) tempRgbMode = 3;
    applyRGBMode(tempRgbMode);
  }
  else if (cmd == CMD_NEXT || cmd == CMD_UP) {
    tempRgbMode++;
    if (tempRgbMode > 3) tempRgbMode = 0;
    applyRGBMode(tempRgbMode);
  }
  else if (cmd == CMD_START) {
    rgbMode = tempRgbMode;
    applyRGBMode(rgbMode);
    currentScreen = SCREEN_MENU;
  }
  else if (cmd == CMD_STOP) {
    applyRGBMode(rgbMode);
    currentScreen = SCREEN_MENU;
  }
}

void processCommand(uint8_t cmd) {
  if (cmd == CMD_POWER) {
    if (systemOn) hardShutdown();
    else startIntro();
    return;
  }

  if (!systemOn) return;

  if (alarmActive) {
    if (cmd == CMD_STOP || cmd == CMD_START || cmd == CMD_POWER) {
      stopAlarmNow();
      currentScreen = SCREEN_MENU;
      renderScreen();
    }
    return;
  }

  if (cmd == CMD_EQ) {
    beginFreqEdit();
    renderScreen();
    return;
  }

  if (cmd == CMD_REPEAT) {
    beginRgbEdit();
    renderScreen();
    return;
  }

  switch (currentScreen) {
    case SCREEN_MENU:         handleMenu(cmd); break;
    case SCREEN_CURRENT_TIME: handleCurrentTime(cmd); break;
    case SCREEN_SETUP_TIMER:  handleSetupTimer(cmd); break;
    case SCREEN_SETUP_ALARM:  handleSetupAlarm(cmd); break;
    case SCREEN_ACTIVE_ALARM: handleActiveAlarm(cmd); break;
    case SCREEN_ACTIVE_TIMER: handleActiveTimer(cmd); break;
    case SCREEN_FREQ:         handleFreq(cmd); break;
    case SCREEN_RGB:          handleRgb(cmd); break;
    default: break;
  }

  renderScreen();
}

void readRemote() {
  if (!IrReceiver.decode()) return;

  if (millis() - lastIrAccept < 120) {
    IrReceiver.resume();
    return;
  }

  lastIrAccept = millis();
  uint8_t cmd = IrReceiver.decodedIRData.command;
  processCommand(cmd);
  IrReceiver.resume();
}

void updateIntro() {
  if (!systemOn) return;

  if (currentScreen == SCREEN_INTRO_HELLO && millis() - introTimer >= 900) {
    currentScreen = SCREEN_INTRO_LOADING;
    introTimer = millis();
    clearLcdCache();
    renderScreen();
  }
  else if (currentScreen == SCREEN_INTRO_LOADING && millis() - introTimer >= 900) {
    currentScreen = SCREEN_MENU;
    clearLcdCache();
    renderScreen();
  }
}

// ---------------- SERIAL COMMANDS ----------------
void print2(int v) {
  if (v < 10) Serial.print('0');
  Serial.print(v);
}

void showTimeSerial() {
  if (!rtcReady) {
    Serial.println("RTC not ready");
    return;
  }

  DateTime now = rtc.now();
  Serial.print("RTC: ");
  Serial.print(now.year());
  Serial.print("-");
  print2(now.month());
  Serial.print("-");
  print2(now.day());
  Serial.print(" ");
  print2(now.hour());
  Serial.print(":");
  print2(now.minute());
  Serial.print(":");
  print2(now.second());
  Serial.println();
}

void showSettingsSerial() {
  Serial.println("----- SETTINGS -----");
  Serial.print("Timer: ");
  print2(timerMinutes);
  Serial.print(":");
  print2(timerSeconds);
  Serial.println();

  Serial.print("Alarm: ");
  print2(alarmHour);
  Serial.print(":");
  print2(alarmMinute);
  Serial.print(" ");
  Serial.println(alarmPM ? "PM" : "AM");

  Serial.print("Alarm Enabled: ");
  Serial.println(alarmEnabled ? "ON" : "OFF");

  Serial.print("RGB: ");
  Serial.println(rgbName(rgbMode));

  Serial.print("Freq: ");
  Serial.print(buzzerFrequency);
  Serial.println(" Hz");
}

void showHelp() {
  Serial.println("Commands:");
  Serial.println("HELP");
  Serial.println("SHOWTIME");
  Serial.println("SHOWSETTINGS");
  Serial.println("SETTIME YYYY-MM-DD HH:MM:SS");
  Serial.println("SETTIMER MM:SS");
  Serial.println("SETALARM HH:MM AM");
  Serial.println("SETALARM HH:MM PM");
  Serial.println("STARTTIMER");
  Serial.println("STOPTIMER");
  Serial.println("SETRGB RED|GREEN|BLUE|WHITE");
  Serial.println("SETFREQ N");
  Serial.println("MENU");
}

bool parseSetTime(const char* input) {
  int y, mo, d, h, mi, s;
  if (sscanf(input, "SETTIME %d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
    if (y >= 2000 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 &&
        h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59) {
      rtc.adjust(DateTime(y, mo, d, h, mi, s));
      return true;
    }
  }
  return false;
}

bool parseSetTimer(const char* input) {
  int mm, ss;
  if (sscanf(input, "SETTIMER %d:%d", &mm, &ss) == 2) {
    if (mm >= 0 && mm <= 99 && ss >= 0 && ss <= 59) {
      timerMinutes = mm;
      timerSeconds = ss;
      countdownMinutes = mm;
      countdownSeconds = ss;
      return true;
    }
  }
  return false;
}

bool parseSetAlarm(const char* input) {
  int hh, mm;
  char ap[3];
  if (sscanf(input, "SETALARM %d:%d %2s", &hh, &mm, ap) == 3) {
    if (hh >= 1 && hh <= 12 && mm >= 0 && mm <= 59) {
      if (strcmp(ap, "AM") == 0 || strcmp(ap, "PM") == 0) {
        alarmHour = hh;
        alarmMinute = mm;
        alarmPM = (strcmp(ap, "PM") == 0);
        alarmEnabled = true;
        return true;
      }
    }
  }
  return false;
}

bool parseSetFreq(const char* input) {
  int f;
  if (sscanf(input, "SETFREQ %d", &f) == 1) {
    if (f >= 100 && f <= 5000) {
      buzzerFrequency = f;
      updateBuzzerPeriod();
      return true;
    }
  }
  return false;
}

bool parseSetRgb(const char* input) {
  char color[8];
  if (sscanf(input, "SETRGB %7s", color) == 1) {
    if (strcmp(color, "RED") == 0) rgbMode = 0;
    else if (strcmp(color, "GREEN") == 0) rgbMode = 1;
    else if (strcmp(color, "BLUE") == 0) rgbMode = 2;
    else if (strcmp(color, "WHITE") == 0) rgbMode = 3;
    else return false;

    applyRGBMode(rgbMode);
    return true;
  }
  return false;
}

void handleSerialCommand() {
  if (!Serial.available()) return;

  char input[48];
  size_t n = Serial.readBytesUntil('\n', input, sizeof(input) - 1);
  input[n] = '\0';

  while (n > 0 && (input[n - 1] == '\r' || input[n - 1] == '\n' || input[n - 1] == ' ')) {
    input[n - 1] = '\0';
    n--;
  }

  for (size_t i = 0; i < n; i++) {
    if (input[i] >= 'a' && input[i] <= 'z') input[i] -= 32;
  }

  if (strcmp(input, "HELP") == 0) {
    showHelp();
  }
  else if (strcmp(input, "SHOWTIME") == 0) {
    showTimeSerial();
  }
  else if (strcmp(input, "SHOWSETTINGS") == 0) {
    showSettingsSerial();
  }
  else if (strcmp(input, "STARTTIMER") == 0) {
    startSavedTimer();
    currentScreen = SCREEN_ACTIVE_TIMER;
    Serial.println("Timer started");
  }
  else if (strcmp(input, "STOPTIMER") == 0) {
    stopTimerNow();
    Serial.println("Timer stopped");
  }
  else if (strcmp(input, "MENU") == 0) {
    currentScreen = SCREEN_MENU;
    Serial.println("Menu opened");
  }
  else if (parseSetTime(input)) {
    Serial.println("RTC time updated");
  }
  else if (parseSetTimer(input)) {
    currentScreen = SCREEN_ACTIVE_TIMER;
    Serial.println("Timer saved");
  }
  else if (parseSetAlarm(input)) {
    currentScreen = SCREEN_ACTIVE_ALARM;
    Serial.println("Alarm saved");
  }
  else if (parseSetFreq(input)) {
    Serial.println("Frequency updated");
  }
  else if (parseSetRgb(input)) {
    Serial.println("RGB updated");
  }
  else {
    Serial.println("Unknown command. Type HELP");
  }

  clearLcdCache();
  renderScreen();
}

// ---------------- SETUP + LOOP ----------------
void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  setRGB(LOW, LOW, LOW);

  Serial.begin(9600);
  lcd.begin(16, 2);
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);

  Wire.begin();
  rtcReady = rtc.begin();

  countdownMinutes = timerMinutes;
  countdownSeconds = timerSeconds;
  updateBuzzerPeriod();
  applyRGBMode(rgbMode);
  startIntro();

  Serial.println("Ready");
  showHelp();
}

void loop() {
  updateIntro();
  readRemote();
  handleSerialCommand();
  checkRealAlarm();
  updateAlarmEffects();
  updateBuzzerWave();
  updateTimerCountdown();

  if (systemOn && currentScreen == SCREEN_CURRENT_TIME && millis() - lastClockRefresh >= 1000) {
    lastClockRefresh = millis();
    clearLcdCache();
    renderScreen();
  }

  if (systemOn && currentScreen == SCREEN_ACTIVE_TIMER && timerRunning && millis() - lastClockRefresh >= 250) {
    lastClockRefresh = millis();
    clearLcdCache();
    renderScreen();
  }

  if (alarmActive) {
    lcd.noBlink();
    lcd.noCursor();
    clearLcdCache();
    setLines("*****ALARM****", "STOP to clear");
  }
}