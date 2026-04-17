/*
  SMART CHAMBER - Single Screen Controller (ADS1115 + LuminOx)
  - Pressure: ADS1115 + 4-20mA sensor → shown as whole mbar only
  - Oxygen: LuminOx optical sensor
  - SD card saves/restores the last set pressure
  - Automatic pressure maintenance (relay turns back ON if pressure drops)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_ADS1X15.h>
#include <LuminOx.h>

// ───────────────────────────────────────────────
// PINS
// ───────────────────────────────────────────────
#define BTN_BACK        32
#define BTN_UP          33
#define BTN_DOWN        25
#define RELAY_PRESSURE  27
#define BUZZER_PIN      23
#define DHTPIN          4
#define DHTTYPE         DHT22

// SD Card (HSPI)
#define SD_CS_PIN       15
SPIClass sdSPI(HSPI);

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 4);

// ADS1115
Adafruit_ADS1115 ads;

// LuminOx
HardwareSerial SensorSerial(2);
LuminOx ox(SensorSerial);

// DHT
DHT dht(DHTPIN, DHTTYPE);

// ───────────────────────────────────────────────
// PRESSURE CALIBRATION (your values)
// ───────────────────────────────────────────────
const float SHUNT_RESISTOR    = 120.0;
const float ZERO_CURRENT_MA   = 5.99;
const float FULL_SCALE_MA     = 20.0;
const float MAX_PRESSURE_KPA  = 20.0;

const int ADC_SAMPLES   = 20;
const int FILTER_WINDOW = 8;           // smoothing

float pressureBuffer[FILTER_WINDOW];
int bufferPos = 0;
bool bufferFull = false;

// ───────────────────────────────────────────────
// VARIABLES
// ───────────────────────────────────────────────
int setPressure_mbar = 0;      // whole number
int actualPressure_mbar = 0;
float oxygen = 20.9;
float humidity = 0.0;

unsigned long lastBtnTime = 0;
unsigned long lastPressureRead = 0;
unsigned long lastDHTRead = 0;

bool lastBACK = HIGH, lastUP = HIGH, lastDOWN = HIGH;

// ───────────────────────────────────────────────
// HELPERS
// ───────────────────────────────────────────────
void beep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(30000);
  digitalWrite(BUZZER_PIN, LOW);
}

// ── Pressure with ADS1115 ───────────────────────
float readAverageVoltage() {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += ads.readADC_SingleEnded(0);
  }
  float adc = sum / (float)ADC_SAMPLES;
  return adc * 0.1875 / 1000.0;   // GAIN_ONE → volts
}

float voltageToCurrent(float voltage) {
  return (voltage / SHUNT_RESISTOR) * 1000.0;
}

float currentToPressure(float mA) {
  if (mA <= ZERO_CURRENT_MA) return 0.0;
  float kPa = ((mA - ZERO_CURRENT_MA) * MAX_PRESSURE_KPA) / (FULL_SCALE_MA - ZERO_CURRENT_MA);
  return constrain(kPa, 0.0, MAX_PRESSURE_KPA);
}

float updateAverage(float newP_kPa) {
  pressureBuffer[bufferPos] = newP_kPa;
  bufferPos = (bufferPos + 1) % FILTER_WINDOW;
  if (bufferPos == 0) bufferFull = true;
  float sum = 0.0;
  int count = bufferFull ? FILTER_WINDOW : bufferPos;
  for (int i = 0; i < count; i++) sum += pressureBuffer[i];
  return sum / count;
}

int readPressure_mbar() {
  float v = readAverageVoltage();
  float mA = voltageToCurrent(v);
  float kPa = currentToPressure(mA);
  float avg_kPa = updateAverage(kPa);
  return (int)(avg_kPa * 10.0 + 0.5);   // round to nearest whole mbar
}

// ── SD Card Functions (unchanged) ───────────────
void saveSetPressureToSD() {
  SD.remove("/setpressure.csv");
  File file = SD.open("/setpressure.csv", FILE_WRITE);
  if (file) {
    file.print("SetPressure,");
    file.println(setPressure_mbar);
    file.close();
    Serial.println("Set pressure saved to SD card");
  }
}

void loadSetPressureFromSD() {
  if (SD.exists("/setpressure.csv")) {
    File file = SD.open("/setpressure.csv", FILE_READ);
    if (file) {
      String line = file.readStringUntil('\n');
      file.close();
      int commaPos = line.indexOf(',');
      String valStr = (commaPos != -1) ? line.substring(commaPos + 1) : line;
      setPressure_mbar = constrain(valStr.toInt(), 0, 300);
      Serial.print("Restored set pressure: ");
      Serial.print(setPressure_mbar);
      Serial.println(" mbar");
    }
  }
}

// ── LCD Functions ───────────────────────────────
void showMain() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("PRESSURE CTRL   ");
  lcd.setCursor(0, 1); lcd.print("Set:           ");
  lcd.setCursor(0, 2); lcd.print("Act:           ");
  lcd.setCursor(0, 3); lcd.print("O2:     H:     ");
}

void refreshMain() {
  // Set Pressure (whole number)
  lcd.setCursor(5, 1);
  lcd.print("     ");
  lcd.setCursor(5, 1);
  lcd.print(setPressure_mbar);
  lcd.print(" mbar ");

  // Actual Pressure (whole number only)
  lcd.setCursor(5, 2);
  lcd.print("     ");
  lcd.setCursor(5, 2);
  lcd.print(actualPressure_mbar);
  lcd.print(" mbar ");

  // O2 + Humidity
  lcd.setCursor(3, 3);
  lcd.print(oxygen, 1);
  lcd.print("% H:");
  lcd.print(humidity, 0);
  lcd.print("%  ");
}

// ── Sensors ─────────────────────────────────────
void readSensors() {
  // LuminOx Oxygen
  LuminOxReading r;
  if (ox.readAll(r) && r.valid) {
    oxygen = r.o2_percent;
  }

  // DHT22 Humidity
  float h = dht.readHumidity();
  if (!isnan(h)) humidity = h;
}

// ── Serial Status ───────────────────────────────
void printSerialStatus() {
  Serial.print("Set: "); Serial.print(setPressure_mbar);
  Serial.print(" mbar | Act: "); Serial.print(actualPressure_mbar);
  Serial.print(" mbar | O2: "); Serial.print(oxygen, 1);
  Serial.print("% | Hum: "); Serial.print(humidity, 0);
  Serial.print("% | Relay: ");
  Serial.println(digitalRead(RELAY_PRESSURE) ? "ON" : "OFF");
}

// ── Button & Serial Handlers (unchanged logic) ──
void readButtons() {
  if (millis() - lastBtnTime < 120) return;

  bool back = digitalRead(BTN_BACK);
  bool up   = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);

  if (back == LOW && lastBACK == HIGH) { beep(); digitalWrite(RELAY_PRESSURE, LOW); }
  if (up   == LOW && lastUP   == HIGH) { 
    beep(); 
    setPressure_mbar = min(setPressure_mbar + 1, 300);
    refreshMain();
    saveSetPressureToSD();
  }
  if (down == LOW && lastDOWN == HIGH) { 
    beep(); 
    setPressure_mbar = max(setPressure_mbar - 1, 0);
    refreshMain();
    saveSetPressureToSD();
  }

  lastBACK = back; lastUP = up; lastDOWN = down;
  lastBtnTime = millis();
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("SET ")) {
    int val = cmd.substring(4).toInt();
    setPressure_mbar = constrain(val, 0, 300);
    Serial.print("Set pressure changed to: "); Serial.println(setPressure_mbar);
    refreshMain();
    saveSetPressureToSD();
  }
  else if (cmd.equalsIgnoreCase("STATUS") || cmd.equalsIgnoreCase("STAT")) {
    printSerialStatus();
  }
  else if (cmd.equalsIgnoreCase("HELP")) {
    Serial.println("Commands: SET <number>, STATUS, HELP, RESET");
  }
  else if (cmd.equalsIgnoreCase("RESET")) {
    setPressure_mbar = 0;
    digitalWrite(RELAY_PRESSURE, LOW);
    refreshMain();
    saveSetPressureToSD();
  }
}

// ───────────────────────────────────────────────
// SETUP
// ───────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("SMART CHAMBER Started (ADS1115 + LuminOx)");

  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(RELAY_PRESSURE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PRESSURE, LOW);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  // ADS1115
  if (!ads.begin()) {
    Serial.println("ADS1115 not found!");
  }
  ads.setGain(GAIN_ONE);

  // LuminOx
  SensorSerial.begin(9600, SERIAL_8N1, 16, 17);
  ox.setDebug(false);
  ox.begin();

  dht.begin();
  analogReadResolution(12);   // not used now but kept for safety

  // SD Card
  sdSPI.begin(14, 12, 13);
  if (SD.begin(SD_CS_PIN, sdSPI)) {
    Serial.println("SD Card OK");
    loadSetPressureFromSD();
  } else {
    Serial.println("SD Card failed");
  }

  // Clear filter buffer
  for (int i = 0; i < FILTER_WINDOW; i++) pressureBuffer[i] = 0.0;

  showMain();
  refreshMain();
}

// ───────────────────────────────────────────────
// LOOP
// ───────────────────────────────────────────────
void loop() {
  readButtons();
  handleSerialCommands();

  unsigned long now = millis();

  // Pressure reading & control every 800ms
  if (now - lastPressureRead >= 800) {
    actualPressure_mbar = readPressure_mbar();
    lastPressureRead = now;

    // Automatic maintenance with small hysteresis
    const int HYSTERESIS = 0.5;
    if (setPressure_mbar > 0) {
      if (actualPressure_mbar < (setPressure_mbar - HYSTERESIS)) {
        digitalWrite(RELAY_PRESSURE, HIGH);
      } else if (actualPressure_mbar >= setPressure_mbar) {
        digitalWrite(RELAY_PRESSURE, LOW);
      }
    } else {
      digitalWrite(RELAY_PRESSURE, LOW);
    }

    refreshMain();
    printSerialStatus();
  }

  // DHT + LuminOx every 2 seconds
  if (now - lastDHTRead >= 2000) {
    readSensors();
    lastDHTRead = now;
    refreshMain();
  }
}
