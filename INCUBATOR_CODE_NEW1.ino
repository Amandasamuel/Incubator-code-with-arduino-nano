#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <EEPROM.h>

#define BUTTON_UP     10
#define BUTTON_DOWN   3
#define BUTTON_LEFT   4
#define BUTTON_RIGHT  5
#define DHT_PIN       7
#define HUMIDIFIER_PIN 6
#define FAN_PIN       8
#define HEATER_PIN    9
#define MOTOR_PIN     11

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4);

const float TEMP_HYST = 0.8;
const float HUM_HYST = 3.0;

// EEPROM ADDRESSES
const int EEPROM_ADDR_FLAG       = 0;
const int EEPROM_ADDR_TEMP_MIN   = 1;
const int EEPROM_ADDR_TEMP_MAX   = 5;
const int EEPROM_ADDR_HUM_MIN    = 9;
const int EEPROM_ADDR_HUM_MAX    = 13;
const int EEPROM_ADDR_ROT_HOURS  = 17;
const int EEPROM_ADDR_ROT_DAYS   = 19;
const int EEPROM_ADDR_INCUBATION = 21;

const byte EEPROM_VALID = 0xA5;

float tempMin = 36.5, tempMax = 37.5, humMin = 45.0, humMax = 55.0;
uint16_t rotationHours = 2;
uint16_t rotationDays = 18;

bool incubationRunning = false;
DateTime incubationStart;
unsigned long lastRotationMillis = 0;

float currentTemp = NAN, currentHum = NAN;

// MENU ENUM
enum MenuState {
  MENU_HOME,
  MENU_SET_TEMP_MIN,
  MENU_SET_TEMP_MAX,
  MENU_SET_HUM_MIN,
  MENU_SET_HUM_MAX,
  MENU_SET_ROT_HOURS,
  MENU_SET_ROT_DAYS,
  MENU_START_STOP
};
MenuState menu = MENU_HOME;

// BUTTON HANDLING
const int BUTTONS[4] = {BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT};
bool buttonState[4] = {HIGH, HIGH, HIGH, HIGH};
bool lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceTime[4] = {0};
const unsigned long DEBOUNCE_DELAY = 50;

// ---------------- EEPROM FUNCTIONS ----------------
void writeFloatToEEPROM(int addr, float val) {
  byte *p = (byte*)(void*)&val;
  for (int i=0;i<4;i++) EEPROM.update(addr+i, p[i]);
}
float readFloatFromEEPROM(int addr) {
  float val = 0.0;
  byte *p = (byte*)(void*)&val;
  for (int i=0;i<4;i++) p[i] = EEPROM.read(addr+i);
  return val;
}
void saveSettings() {
  EEPROM.update(EEPROM_ADDR_FLAG, EEPROM_VALID);
  writeFloatToEEPROM(EEPROM_ADDR_TEMP_MIN, tempMin);
  writeFloatToEEPROM(EEPROM_ADDR_TEMP_MAX, tempMax);
  writeFloatToEEPROM(EEPROM_ADDR_HUM_MIN, humMin);
  writeFloatToEEPROM(EEPROM_ADDR_HUM_MAX, humMax);
  EEPROM.update(EEPROM_ADDR_ROT_HOURS, rotationHours);
  EEPROM.update(EEPROM_ADDR_ROT_DAYS, rotationDays);
  EEPROM.update(EEPROM_ADDR_INCUBATION, incubationRunning);
  Serial.println("✅ Settings saved.");
}
void loadSettings() {
  if (EEPROM.read(EEPROM_ADDR_FLAG) == EEPROM_VALID) {
    tempMin = readFloatFromEEPROM(EEPROM_ADDR_TEMP_MIN);
    tempMax = readFloatFromEEPROM(EEPROM_ADDR_TEMP_MAX);
    humMin  = readFloatFromEEPROM(EEPROM_ADDR_HUM_MIN);
    humMax  = readFloatFromEEPROM(EEPROM_ADDR_HUM_MAX);
    rotationHours = EEPROM.read(EEPROM_ADDR_ROT_HOURS);
    rotationDays = EEPROM.read(EEPROM_ADDR_ROT_DAYS);
    incubationRunning = EEPROM.read(EEPROM_ADDR_INCUBATION);
    Serial.println("✅ Settings loaded.");
  } else {
    Serial.println("⚠ Using default settings.");
  }
}

// ---------------- HELPERS ----------------
bool readButton(int index) {
  int reading = digitalRead(BUTTONS[index]);
  if (reading != lastButtonState[index]) {
    lastDebounceTime[index] = millis();
  }
  if ((millis() - lastDebounceTime[index]) > DEBOUNCE_DELAY) {
    if (reading != buttonState[index]) {
      buttonState[index] = reading;
      if (buttonState[index] == LOW) {
        return true;
      }
    }
  }
  lastButtonState[index] = reading;
  return false;
}

String fmt2(int v){ return (v<10?"0":"")+String(v); }

void controlEnvironment() {
  if (!isnan(currentTemp)) {
    if (currentTemp < tempMin - TEMP_HYST) {
      digitalWrite(HEATER_PIN, HIGH);
      digitalWrite(FAN_PIN, LOW);
    } else if (currentTemp > tempMax + TEMP_HYST) {
      digitalWrite(HEATER_PIN, LOW);
      digitalWrite(FAN_PIN, HIGH);
    }
  }
  if (!isnan(currentHum)) {
    if (currentHum < humMin - HUM_HYST) {
      digitalWrite(HUMIDIFIER_PIN, HIGH);
    } else if (currentHum > humMax + HUM_HYST) {
      digitalWrite(HUMIDIFIER_PIN, LOW);
    }
  }
}

// ---------------- ROTATION LOGIC ----------------
void checkRotation() {
  if (!incubationRunning) return;

  DateTime now = rtc.now();
  TimeSpan elapsed = now - incubationStart;
  int daysPassed = elapsed.days();

  if (daysPassed >= rotationDays) return; // stop rotation after set days

  unsigned long interval = (unsigned long)rotationHours * 3600000UL;
  if (millis() - lastRotationMillis >= interval) {
    lastRotationMillis = millis();
    Serial.println("♻ Rotating eggs...");
    digitalWrite(MOTOR_PIN, HIGH);
    delay(2000); // rotate duration
    digitalWrite(MOTOR_PIN, LOW);
  }
}

// ---------------- MAIN SETUP ----------------
void setup() {
  Serial.begin(9600);
  Wire.begin();
  lcd.init(); lcd.backlight();
  dht.begin();
  rtc.begin();

  for (int i=0;i<4;i++) pinMode(BUTTONS[i], INPUT_PULLUP);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);

  loadSettings();

  lcd.clear();
  lcd.print("Incubator Ready");
  delay(1000);
  lcd.clear();
}

// ---------------- MAIN LOOP ----------------
void loop() {
  bool up = readButton(0);
  bool down = readButton(1);
  bool left = readButton(2);
  bool right = readButton(3);

  currentTemp = dht.readTemperature();
  currentHum = dht.readHumidity();
  controlEnvironment();
  checkRotation();

  if (menu == MENU_HOME) {
    DateTime now = rtc.now();
    lcd.setCursor(0,0);
    lcd.print("T:"); lcd.print(currentTemp,1); lcd.print((char)223); lcd.print("C ");
    lcd.print("H:"); lcd.print(currentHum,0); lcd.print("%   ");

    lcd.setCursor(0,1);
    lcd.print("MinT:"); lcd.print(tempMin,1); lcd.print(" MaxT:"); lcd.print(tempMax,1);

    lcd.setCursor(0,2);
    lcd.print("Rot:"); lcd.print(rotationHours); lcd.print("h ");
    lcd.print("Days:"); lcd.print(rotationDays);

    lcd.setCursor(0,3);
    lcd.print("Incub:");
    lcd.print(incubationRunning ? "ON " : "OFF");
    lcd.print(" ");
    lcd.print(fmt2(now.hour())); lcd.print(":"); lcd.print(fmt2(now.minute()));

    if (right) { menu = MENU_SET_TEMP_MIN; lcd.clear(); }
  }

  else if (menu == MENU_SET_TEMP_MIN) {
    lcd.setCursor(0,0); lcd.print("Set Min Temp:");
    lcd.setCursor(0,1); lcd.print(tempMin,1); lcd.print("C   ");
    if (up) tempMin += 0.1;
    if (down) tempMin -= 0.1;
    if (right) { menu = MENU_SET_TEMP_MAX; lcd.clear(); }
    if (left) { saveSettings(); menu = MENU_HOME; lcd.clear(); }
  }

  else if (menu == MENU_SET_TEMP_MAX) {
    lcd.setCursor(0,0); lcd.print("Set Max Temp:");
    lcd.setCursor(0,1); lcd.print(tempMax,1); lcd.print("C   ");
    if (up) tempMax += 0.1;
    if (down) tempMax -= 0.1;
    if (right) { menu = MENU_SET_HUM_MIN; lcd.clear(); }
    if (left) { menu = MENU_SET_TEMP_MIN; lcd.clear(); }
  }

  else if (menu == MENU_SET_HUM_MIN) {
    lcd.setCursor(0,0); lcd.print("Set Min Humidity:");
    lcd.setCursor(0,1); lcd.print(humMin,1); lcd.print("%   ");
    if (up) humMin++;
    if (down) humMin--;
    if (right) { menu = MENU_SET_HUM_MAX; lcd.clear(); }
    if (left) { menu = MENU_SET_TEMP_MAX; lcd.clear(); }
  }

  else if (menu == MENU_SET_HUM_MAX) {
    lcd.setCursor(0,0); lcd.print("Set Max Humidity:");
    lcd.setCursor(0,1); lcd.print(humMax,1); lcd.print("%   ");
    if (up) humMax++;
    if (down) humMax--;
    if (right) { menu = MENU_SET_ROT_HOURS; lcd.clear(); }
    if (left) { menu = MENU_SET_HUM_MIN; lcd.clear(); }
  }

  else if (menu == MENU_SET_ROT_HOURS) {
    lcd.setCursor(0,0); lcd.print("Set Rotation Time:");
    lcd.setCursor(0,1); lcd.print(rotationHours); lcd.print(" hour(s)");
    if (up) rotationHours++;
    if (down && rotationHours > 1) rotationHours--;
    if (right) { menu = MENU_SET_ROT_DAYS; lcd.clear(); }
    if (left) { menu = MENU_SET_HUM_MAX; lcd.clear(); }
  }

else if (menu == MENU_SET_ROT_DAYS) {
  lcd.setCursor(0,0);
  lcd.print("Set Rotation Days");
  lcd.setCursor(0,1);
  lcd.print(rotationDays); lcd.print(" days");

  if (up && rotationDays < 30) rotationDays++;
  if (down && rotationDays > 12) rotationDays--;

  // Clamp the value just in case
  if (rotationDays < 12) rotationDays = 12;
  if (rotationDays > 30) rotationDays = 30;

  if (right) { menu = MENU_START_STOP; lcd.clear(); }
  if (left)  { menu = MENU_SET_ROT_HOURS; lcd.clear(); }
}

  else if (menu == MENU_START_STOP) {
    lcd.setCursor(0,0); lcd.print("Start/Stop Incubat.");
    lcd.setCursor(0,1); lcd.print("Status: ");
    lcd.print(incubationRunning ? "RUNNING " : "STOPPED");
    if (up || down) {
      incubationRunning = !incubationRunning;
      if (incubationRunning) {
        incubationStart = rtc.now();
        Serial.println("🕐 Incubation started!");
      } else {
        Serial.println("🛑 Incubation stopped!");
      }
    }
    if (left) { saveSettings(); menu = MENU_HOME; lcd.clear(); }
  }

  delay(100);
}
