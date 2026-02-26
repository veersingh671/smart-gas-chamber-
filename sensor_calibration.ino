/* code for pressure sensor calibration 
output 4-20ma 
scale 0-20Kpa
operating voltage 24V
shunt resistance 120 ohm to 165 ohm change in code 
esp32 gpio 34 */


#define ADC_PIN 34
#define ADC_RESOLUTION 4095.0
#define ADC_REF_VOLTAGE 3.3
#define SHUNT_RESISTOR 165.0   // Ohms

// Pressure range
#define PRESSURE_MIN 0.0
#define PRESSURE_MAX 20.0

// Current range
#define CURRENT_MIN 4.0
#define CURRENT_MAX 20.0

// Error thresholds
#define CURRENT_LOW_FAULT 3.5
#define CURRENT_HIGH_FAULT 21.0

// Calibration variables
float zeroOffset = 0.0;
float spanGain = 1.0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  Serial.println("=== Pressure Transmitter Test System ===");
  Serial.println("Initializing...");
  delay(2000);

}

void loop() {
  float voltage = readVoltage();
  float current = calculateCurrent(voltage);
  float pressure = calculatePressure(current);
  printDiagnostics(voltage, current, pressure);
  checkFaults(current);
  delay(1000);
}

/*READ VOLTAGE */
float readVoltage() {
  int adcValue = analogRead(ADC_PIN);
  float voltage = (adcValue / ADC_RESOLUTION) * ADC_REF_VOLTAGE;
  return voltage;
}

/*VOLTAGE → CURRENT*/
float calculateCurrent(float voltage) {
  float current_mA = (voltage / SHUNT_RESISTOR) * 1000.0;
  return current_mA;
}

/*CURRENT → PRESSURE*/
float calculatePressure(float current) {

  // Apply calibration
  float calibratedCurrent = (current - zeroOffset) * spanGain;

  float pressure = ((calibratedCurrent - CURRENT_MIN) *
                   (PRESSURE_MAX - PRESSURE_MIN) /
                   (CURRENT_MAX - CURRENT_MIN)) + PRESSURE_MIN;

  return pressure;
}

/*DIAGNOSTICS*/
void printDiagnostics(float voltage, float current, float pressure) {
  Serial.println("----- Measurement Report -----");
  Serial.print("Voltage (V): ");
  Serial.println(voltage, 3);
  Serial.print("Current (mA): ");
  Serial.println(current, 3);
  Serial.print("Pressure (kPa): ");
  Serial.println(pressure, 3);
  Serial.println("-----------");
}

/*  FAULT DETECTION */
void checkFaults(float current) {

  if (current < CURRENT_LOW_FAULT) {
    Serial.println("ERROR: Loop Current Too Low (Possible Open Circuit)");
  }
  else if (current > CURRENT_HIGH_FAULT) {
    Serial.println("ERROR: Loop Current Too High (Possible Short Circuit)");
  }
}

/*  CALIBRATION ROUTINE */

void calibrateZero(float measuredCurrent) {
  zeroOffset = measuredCurrent - CURRENT_MIN;
  Serial.println("Zero Calibration Completed.");
}

void calibrateSpan(float measuredCurrent) {
  spanGain = (CURRENT_MAX - CURRENT_MIN) /
             (measuredCurrent - CURRENT_MIN);
  Serial.println("Span Calibration Completed.");
}
