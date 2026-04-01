#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>

LiquidCrystal_I2C lcd(0x27, 16, 4);

#define BTN_BACK        32
#define BTN_UP          33
#define BTN_DOWN        25
#define RELAY_PRESSURE  27
#define BUZZER_PIN      23
#define PRESSURE_ADC_PIN 39
#define SHUNT_RESISTOR  120.0
#define PRESSURE_MAX_KPA 20.0
#define LOOP_MIN_MA     4.0
#define LOOP_MAX_MA     20.0
#define O2_PIN          36
#define O2_AIR_PERCENT  20.9
#define O2_AIR_VOLTAGE  0.394
#define DHTPIN          4
#define DHTTYPE         DHT22

#define SD_CS_PIN       15          // Connect SD card: CS=15, SCK=14, MOSI=13, MISO=12
SPIClass sdSPI(HSPI);

DHT dht(DHTPIN, DHTTYPE);

int setPressure = 0;      
int actualPressure = 0;  

float oxygen = 0.0;
float humidity = 0.0;

unsigned long lastBtnTime = 0;
unsigned long lastPressureRead = 0;
unsigned long lastDHTRead = 0;

bool lastBACK = HIGH, lastUP = HIGH, lastDOWN = HIGH;

void beep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(30000);
  digitalWrite(BUZZER_PIN, LOW);
}

int readPressure_mbar() {
  const int samples = 20;
  long sum = 0;

  for (int i = 0; i < samples; i++) {
    sum += analogRead(PRESSURE_ADC_PIN);
    delayMicroseconds(150);
  }

  float adc = sum / (float)samples;
  float voltage = (adc / 4095.0) * 3.3;
  float current_mA = (voltage / SHUNT_RESISTOR) * 1000.0;

  current_mA = constrain(current_mA, LOOP_MIN_MA, LOOP_MAX_MA);

  float pressure_kPa = (current_mA - LOOP_MIN_MA) * (PRESSURE_MAX_KPA / (LOOP_MAX_MA - LOOP_MIN_MA));
  return (int)(pressure_kPa * 10.0);   // → mbar
}

// ====================== SD CARD SAVE / LOAD ======================
void saveSetPressureToSD() {
  SD.remove("/setpressure.csv");                    // overwrite cleanly
  File file = SD.open("/setpressure.csv", FILE_WRITE);
  if (file) {
    file.print("SetPressure,");
    file.println(setPressure);
    file.close();
    Serial.println("Set pressure saved to SD card → setpressure.csv");
  } else {
    Serial.println("Failed to save to SD card");
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
      setPressure = constrain(valStr.toInt(), 0, 200);

      Serial.print("✓ Restored last set pressure from SD card: ");
      Serial.print(setPressure);
      Serial.println(" mbar");
    }
  } else {
    Serial.println("No previous set pressure found on SD card → starting with 0");
  }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  Serial.println("SMART CHAMBER System Started!");
  Serial.println("Type HELP for serial commands.");

  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  pinMode(RELAY_PRESSURE, OUTPUT);
  pinMode(BUZZER_PIN,     OUTPUT);

  digitalWrite(RELAY_PRESSURE, LOW);   // everything OFF at start

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  dht.begin();
  analogReadResolution(12);

  // ====================== SD CARD INIT ======================
  sdSPI.begin(14, 12, 13);                     // HSPI: SCK=14 (freed), MISO=12, MOSI=13
  if (SD.begin(SD_CS_PIN, sdSPI)) {
    Serial.println("SD Card initialized successfully.");
    loadSetPressureFromSD();
  } else {
    Serial.println("SD Card failed to initialize! (Check wiring / card inserted)");
  }

  lcd.clear();
  showMain();
  refreshMain();          // Show restored values
}

void loop() {
  readButtons();

  unsigned long now = millis();

  // Pressure reading & auto-control every 1 second (stable reading)
  if (now - lastPressureRead >= 1000) {
    actualPressure = readPressure_mbar();
    lastPressureRead = now;

    // Automatic pressure control
    if (actualPressure < setPressure && setPressure > 0) {
      digitalWrite(RELAY_PRESSURE, HIGH);
    } else {
      digitalWrite(RELAY_PRESSURE, LOW);
    }

    refreshMain();
    printSerialStatus();
  }


  if (now - lastDHTRead >= 2000) {
    readSensors();
    lastDHTRead = now;
    refreshMain();
  }

  handleSerialCommands();
}

void readButtons() {
  if (millis() - lastBtnTime < 120) return;

  bool back = digitalRead(BTN_BACK);
  bool up   = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);

  if (back == LOW && lastBACK == HIGH) { beep(); handleBACK(); }
  if (up   == LOW && lastUP   == HIGH) { beep(); handleUP();   }
  if (down == LOW && lastDOWN == HIGH) { beep(); handleDOWN(); }

  lastBACK = back;
  lastUP   = up;
  lastDOWN = down;

  lastBtnTime = millis();
}

void handleBACK() {
  digitalWrite(RELAY_PRESSURE, LOW);
}

void handleUP() {
  setPressure = min(setPressure + 1, 200);
  refreshMain();
  saveSetPressureToSD();         
}

void handleDOWN() {
  setPressure = max(setPressure - 1, 0);
  refreshMain();
  saveSetPressureToSD();          
}

void readSensors() {
  float v = analogRead(O2_PIN) * 3.3 / 4095.0;
  oxygen = constrain((v / O2_AIR_VOLTAGE) * O2_AIR_PERCENT, 0, 25);

  float h = dht.readHumidity();
  if (!isnan(h)) humidity = h;
}

void showMain() {
  lcd.setCursor(0, 0); lcd.print("PRESSURE CTRL  ");
  lcd.setCursor(0, 1); lcd.print("Set:           ");
  lcd.setCursor(0, 2); lcd.print("Act:           ");
  lcd.setCursor(0, 3); lcd.print("O2:     H:    ");
}

void refreshMain() {
  // Set Pressure
  lcd.setCursor(5, 1);
  lcd.print("   "); 
  lcd.setCursor(5, 1);
  lcd.print(setPressure);
  lcd.print(" mbar ");

  // Actual Pressure
  lcd.setCursor(5, 2);
  lcd.print("   ");
  lcd.setCursor(5, 2);
  lcd.print(actualPressure);
  lcd.print(" mbar ");

  // O2 & Humidity
  lcd.setCursor(0, 3);
  lcd.print("O2:");
  lcd.print(oxygen, 1);
  lcd.print("% H:");
  lcd.print(humidity, 0);
  lcd.print("% ");
}

void printSerialStatus() {
  Serial.print("Set: ");
  Serial.print(setPressure);
  Serial.print(" mbar | Act: ");
  Serial.print(actualPressure);
  Serial.print(" mbar | O2: ");
  Serial.print(oxygen, 1);
  Serial.print("% | Hum: ");
  Serial.print(humidity, 0);
  Serial.print("% | Pressure Relay: ");
  Serial.println(digitalRead(RELAY_PRESSURE) ? "ON" : "OFF");
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("SET ")) {
    int val = cmd.substring(4).toInt();
    setPressure = constrain(val, 0, 200);
    Serial.print("✓ Set pressure changed to: ");
    Serial.print(setPressure);
    Serial.println(" mbar");
    refreshMain();
    saveSetPressureToSD();               // save immediately
  }
  else if (cmd.equalsIgnoreCase("STATUS") || cmd.equalsIgnoreCase("STAT")) {
    printSerialStatus();
  }
  else if (cmd.equalsIgnoreCase("HELP")) {
    Serial.println("=== SMART CHAMBER Serial Commands ===");
    Serial.println("SET <number>     → Set target pressure (0-200 mbar)");
    Serial.println("STATUS / STAT    → Show live readings");
    Serial.println("HELP             → This help");
    Serial.println("RESET            → Reset set pressure to 0");
    Serial.println("SD card auto-saves/restores set pressure on power loss.");
  }
  else if (cmd.equalsIgnoreCase("RESET")) {
    setPressure = 0;
    digitalWrite(RELAY_PRESSURE, LOW);
    Serial.println("✓ System reset (set pressure = 0)");
    refreshMain();
    saveSetPressureToSD();
  }
  else {
    Serial.println("Unknown command. Type HELP");
  }
}
