/*
 * Project Name: ESP32 Gas / Vacuum Chamber Controller
 * Author        : <chiranjee veer singh>
 * Platform      : Custom ESP32 Development Board
 * MCU          : ESP32-WROOM-32
 *
 * Description:
 * This project implements a complete gas/vacuum chamber control system
 * using a custom-designed ESP32 development board. The board integrates
 * all required peripherals, including power regulation, relay driver,
 * and sensor interfaces, making it a standalone solution.
 *
 * Sensors Used:
 * - DHT22: Measures temperature and humidity inside the chamber
 * - Oxygen (O₂) Sensor: Measures oxygen concentration in percentage
 * - Pressure Sensor (4–20mA): Measures chamber pressure via shunt resistor
 *
 * Hardware Features:
 * - Onboard relay for vacuum pump/gas valve control
 * - 9V to 5V onboard power regulation
 * - ESP32 3.3V regulated supply
 * - Dedicated ADC inputs for analog sensors
 * - Push buttons for user interaction (UP / DOWN / OK / BACK)
 * - Buzzer for key press feedback
 * - I2C interface for 16x4 LCD display
 *
 * Functional Overview:
 * - Real-time pressure, oxygen, temperature, and humidity monitoring
 * - Manual vacuum chamber ON/OFF control
 * - Set-pressure mode with automatic relay control
 * - Relay turns OFF when actual pressure equals set pressure
 * - Continuous real-time sensor updates (non-blocking)
 *
 * Communication:
 * - Serial Monitor for debugging and testing
 * - I2C for LCD (SDA: 21, SCL: 22)
 *
 * Notes:
 * - Designed for industrial and laboratory gas chamber applications
 * - ADC readings are averaged for noise reduction
 * - Pressure conversion is based on a calibrated 4–20mA input
 *
 * License:
 * Open-source / Personal / Educational use
 */

/*
  SMART PRESSURE CHAMBER CONTROLLER - REFINED VERSION 2.2
  Fixes:
  - Set pressure UP/DOWN now changes by 1 mbar (not 5)
  - No blinking on MONITOR screen: status only updates when state changes
  - Minor LCD write optimization
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Adafruit_ADS1X15.h>
#include <LuminOx.h>

#define BTN_OK      35
#define BTN_BACK    32
#define BTN_UP      33
#define BTN_DOWN    25
#define RELAY_VACUUM   14
#define RELAY_PRESSURE 27
#define BUZZER_PIN     23
#define DHT_PIN     5
#define DHT_TYPE    DHT22

LiquidCrystal_I2C lcd(0x27, 16, 4);
Adafruit_ADS1115 ads;
HardwareSerial SensorSerial(2);
LuminOx ox(SensorSerial);
DHT dht(DHT_PIN, DHT_TYPE);


const float SHUNT_RESISTOR    = 120.0;
const float ZERO_CURRENT_MA   = 5.99;
const float FULL_SCALE_MA     = 20.0;
const float MAX_PRESSURE_KPA  = 20.0;

const int ADC_SAMPLES   = 20;
const int FILTER_WINDOW = 10;

const float KPA_TO_MBAR = 10.0;

enum Screen { HOME, VACUUM, SET_PRESSURE, MONITOR_PRESSURE };
Screen currentScreen = HOME;
int menuIndex = 0;

bool vacuumRelayState = false;
bool pressureControlActive = false;
bool lastControlActive = false;          

float setPressure_mbar = 50.0;
float actualPressure_mbar = 0.0;
float oxygen = 20.9;
float humidity = 0.0;

bool lastOK = HIGH, lastBACK = HIGH, lastUP = HIGH, lastDOWN = HIGH;

float pressureBuffer[FILTER_WINDOW];
int bufferPos = 0;
bool bufferFull = false;

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 800;

void beep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(40);
  digitalWrite(BUZZER_PIN, LOW);
}

void clearLine(int row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
}

float readAverageVoltage() {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += ads.readADC_SingleEnded(0);
  }
  return (sum / (float)ADC_SAMPLES) * 0.1875 / 1000.0;
}

float voltageToCurrent(float v) { return (v / SHUNT_RESISTOR) * 1000.0; }

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
  int cnt = bufferFull ? FILTER_WINDOW : bufferPos;
  for (int i = 0; i < cnt; i++) sum += pressureBuffer[i];
  return sum / cnt;
}

//  Read sensors
void readSensors() {
  float v = readAverageVoltage();
  float mA = voltageToCurrent(v);
  float raw_kPa = currentToPressure(mA);
  float avg_kPa = updateAverage(raw_kPa);
  actualPressure_mbar = avg_kPa * KPA_TO_MBAR;

  LuminOxReading r;
  if (ox.readAll(r) && r.valid) oxygen = r.o2_percent;

  float h = dht.readHumidity();
  if (!isnan(h)) humidity = h;
}

void controlPressure() {
  if (!pressureControlActive) return;

  const float HYSTERESIS_MBAR = 3.0;   //  tune this value (2.0 to 5.0 usually good)

  float lower_threshold = setPressure_mbar - HYSTERESIS_MBAR;
  if (lower_threshold < 0) lower_threshold = 0.0;
  if (actualPressure_mbar <= lower_threshold) {
    if (!digitalRead(RELAY_PRESSURE)) {           // only if currently OFF
      digitalWrite(RELAY_PRESSURE, HIGH);
      pressureControlActive = true;               // keep mode active
    }
  }
  // Turn OFF if at or above setpoint
  else if (actualPressure_mbar >= setPressure_mbar) {
    if (digitalRead(RELAY_PRESSURE)) {            // only if currently ON
      digitalWrite(RELAY_PRESSURE, LOW);
    }
  }

  if (actualPressure_mbar > 300.0) {
    digitalWrite(RELAY_PRESSURE, LOW);
    pressureControlActive = false;
  }
}

void updateMonitor() {
  if (pressureControlActive != lastControlActive) {
    lcd.setCursor(3, 0);
    if (pressureControlActive) {
      lcd.print("PRESSURE CONTROL");
    } else {
      lcd.print("TARGET REACHED! ");
    }
    lastControlActive = pressureControlActive;
  }

  lcd.setCursor(0, 1);
  lcd.print("Set : ");
  lcd.print((int)setPressure_mbar);  // show as whole number
  lcd.print(" mbar   ");

  // Actual pressure
  lcd.setCursor(0, 2);
  lcd.print("Act : ");
  lcd.print(actualPressure_mbar, 1);
  lcd.print(" mbar   ");

  // O2 + Humidity
  lcd.setCursor(0, 3);
  lcd.print("O2:");
  lcd.print(oxygen, 1);
  lcd.print("%  H:");
  lcd.print(humidity, 1);
  lcd.print("%     ");
}

void showHome() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(" SMART CHAMBER ");
  lcd.setCursor(0, 1); lcd.print(menuIndex == 0 ? "> Vacuum Mode" : "  Vacuum Mode");
  lcd.setCursor(0, 2); lcd.print(menuIndex == 1 ? "> Set Pressure" : "  Set Pressure");
}

void showVacuum() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(" VACUUM MODE ");
  lcd.setCursor(0, 1); lcd.print("Relay: "); lcd.print(vacuumRelayState ? "ON " : "OFF");
  lcd.setCursor(0, 3); lcd.print("");
}

void showSetPressure() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(" SET PRESSURE ");
  lcd.setCursor(0, 1); lcd.print("Set : "); lcd.print((int)setPressure_mbar); lcd.print(" mbar");
  lcd.setCursor(0, 2); lcd.print("OK=Start");
  lcd.setCursor(0, 3); lcd.print("UP/DN BACK");
}

void showMonitor() {
  lcd.clear();
  lastControlActive = !pressureControlActive;  
  updateMonitor();
}

void handleOK() {
  if (currentScreen == HOME) {
    currentScreen = (menuIndex == 0) ? VACUUM : SET_PRESSURE;
    currentScreen == VACUUM ? showVacuum() : showSetPressure();
  }
  else if (currentScreen == VACUUM) {
    vacuumRelayState = !vacuumRelayState;
    digitalWrite(RELAY_VACUUM, vacuumRelayState);
    showVacuum();
  }
  else if (currentScreen == SET_PRESSURE) {
    pressureControlActive = true;
    digitalWrite(RELAY_PRESSURE, HIGH);
    currentScreen = MONITOR_PRESSURE;
    showMonitor();
  }
}

void handleBACK() {
  pressureControlActive = false;
  digitalWrite(RELAY_PRESSURE, LOW);
  digitalWrite(RELAY_VACUUM, LOW);
  currentScreen = HOME;
  showHome();
}

void handleUP() {
  if (currentScreen == HOME) {
    menuIndex = (menuIndex == 0) ? 1 : 0;
    showHome();
  }
  else if (currentScreen == SET_PRESSURE || currentScreen == MONITOR_PRESSURE) {
    setPressure_mbar += 1.0;             
    if (setPressure_mbar > 300.0) setPressure_mbar = 300.0;
    if (currentScreen == SET_PRESSURE) showSetPressure();
    else {
      if (setPressure_mbar > actualPressure_mbar && !pressureControlActive) {
        pressureControlActive = true;
        digitalWrite(RELAY_PRESSURE, HIGH);
      }
      updateMonitor();
    }
  }
}

void handleDOWN() {
  if (currentScreen == HOME) {
    menuIndex = (menuIndex == 0) ? 1 : 0;
    showHome();
  }
  else if (currentScreen == SET_PRESSURE || currentScreen == MONITOR_PRESSURE) {
    setPressure_mbar -= 1.0;               // ← changed to 1 mbar step
    if (setPressure_mbar < 0.0) setPressure_mbar = 0.0;
    if (currentScreen == SET_PRESSURE) showSetPressure();
    else updateMonitor();
  }
}

void readButtons() {
  bool ok = digitalRead(BTN_OK);
  bool back = digitalRead(BTN_BACK);
  bool up = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);

  if (ok == LOW && lastOK == HIGH)   { beep(); handleOK();   }
  if (back == LOW && lastBACK == HIGH) { beep(); handleBACK(); }
  if (up == LOW && lastUP == HIGH)   { beep(); handleUP();   }
  if (down == LOW && lastDOWN == HIGH) { beep(); handleDOWN(); }

  lastOK = ok; lastBACK = back; lastUP = up; lastDOWN = down;

  delay(50);
}

void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  pinMode(RELAY_VACUUM, OUTPUT);
  pinMode(RELAY_PRESSURE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_VACUUM, LOW);
  digitalWrite(RELAY_PRESSURE, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(21, 22);
  Wire.setClock(400000);

  lcd.init();
  lcd.backlight();

  if (!ads.begin()) {
    Serial.println("ADS1115 ERROR!");
    while (true) delay(1000);
  }
  ads.setGain(GAIN_ONE);

  SensorSerial.begin(9600, SERIAL_8N1, 16, 17);
  ox.setDebug(false);
  ox.begin();

  dht.begin();

  for (int i = 0; i < FILTER_WINDOW; i++) pressureBuffer[i] = 0.0;

  Serial.println("\n=== CHAMBER READY v2.2 (1 mbar steps, no blink) ===\n");
  showHome();
}

void loop() {
  readButtons();

  if (currentScreen == MONITOR_PRESSURE) {
    if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
      readSensors();
      controlPressure();
      updateMonitor();
      lastSensorRead = millis();
    }
  }
}
