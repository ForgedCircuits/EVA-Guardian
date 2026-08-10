#include <EEPROMAdapter.h>
#include "matrix_display.h"

// ============================================================
// HARDWARE CONFIGURATION
// ============================================================
const int CURRENT_PIN = A0;
const int VOLTAGE_PIN = A1;
const int TEMP_PIN    = A2;
const uint16_t EEPROM_START_ADDRESS = 0;

// ADC (14-bit)
const float ADC_REF_VOLTAGE = 3.3f;
const float ADC_MAX_VALUE   = 16383.0f;

// Current Sensor (ACS723)
// Sign convention: NEGATIVE = discharge/load, POSITIVE = charging
const float CURRENT_ZERO_POINT_V       = 1.70f;
const float CURRENT_SENSITIVITY_V_PER_A = 1.336f;

// Voltage Divider (R1=6.8K, R2=3K)
const float R1_VOLTAGE_DIVIDER = 6800.0f;
const float R2_VOLTAGE_DIVIDER = 3000.0f;
const float VOLTAGE_MULTIPLIER = (R1_VOLTAGE_DIVIDER + R2_VOLTAGE_DIVIDER) / R2_VOLTAGE_DIVIDER;

// Temperature Sensor (NTC)
const float NTC_VCC               = 5.0f;
const float NTC_R_FIXED           = 3300.0f;
const float NTC_R_SERIES          = 3300.0f;
const float NTC_NOMINAL_RESISTANCE = 10000.0f;
const float NTC_NOMINAL_TEMP      = 298.15f;
const float NTC_BETA              = 3950.0f;

// ============================================================
// BATTERY CONFIGURATION
// ============================================================
const float BATTERY_NOMINAL_CAPACITY_AH = 18.650f; // 18650 mAh
const float BATTERY_MAX_VOLTAGE         = 8.4f;
const float BATTERY_MIN_VOLTAGE         = 6.0f;
const float CHARGE_EFFICIENCY           = 0.98f; // 98% coulombic efficiency during charge

// SOC thresholds
const float REST_CURRENT_THRESHOLD_A = 0.05f;  // < 50mA = at rest
const unsigned long REST_DURATION_MS  = 60000;  // 60 sec rest before OCV trusted

// ============================================================
// OCV LOOKUP TABLE (2S Li-Ion, 8.4V full)
// Maps resting battery voltage -> SOC%
// ============================================================
typedef struct { float voltage; float soc; } OCV_Point_t;

const OCV_Point_t OCV_LUT[] = {
  {6.00f,   0.0f},
  {7.20f,  10.0f},
  {7.40f,  20.0f},
  {7.50f,  30.0f},
  {7.60f,  45.0f},
  {7.80f,  60.0f},
  {8.00f,  75.0f},
  {8.20f,  90.0f},
  {8.40f, 100.0f}
};
const uint8_t OCV_LUT_SIZE = sizeof(OCV_LUT) / sizeof(OCV_LUT[0]);

float get_soc_from_ocv(float voltage) {
  if (voltage <= OCV_LUT[0].voltage)                  return OCV_LUT[0].soc;
  if (voltage >= OCV_LUT[OCV_LUT_SIZE - 1].voltage)   return OCV_LUT[OCV_LUT_SIZE - 1].soc;
  for (uint8_t i = 0; i < OCV_LUT_SIZE - 1; i++) {
    if (voltage >= OCV_LUT[i].voltage && voltage <= OCV_LUT[i+1].voltage) {
      float ratio = (voltage - OCV_LUT[i].voltage) / (OCV_LUT[i+1].voltage - OCV_LUT[i].voltage);
      return OCV_LUT[i].soc + ratio * (OCV_LUT[i+1].soc - OCV_LUT[i].soc);
    }
  }
  return 50.0f;
}

// ============================================================
// PERSISTENT STATE (EEPROM) - SOH only
// ============================================================
struct BMSState {
  float cycleCount;
  float actualMaxCapacityAh;
};

BMSState persistentState;
EEPROM::Adapter eepromAdapter = EEPROM::Adapter();

// ============================================================
// LIVE VARIABLES (RAM only, reset on power cycle)
// ============================================================
float systemVoltage     = 0.0f;
float systemCurrent     = 0.0f;
float systemTemperature = 0.0f;
float systemSOC         = 50.0f; // safe default until boot init
float systemSOH         = 100.0f;
float accumulatedAh     = 0.0f;  // Coulomb counter (Ah)
bool emmcInitialized    = false;

// Timing
unsigned long lastReadTime  = 0;
unsigned long lastPrintTime = 0;
unsigned long lastSaveTime  = 0;
const unsigned long READ_INTERVAL_MS  = 100;
const unsigned long PRINT_INTERVAL_MS = 1000;
const unsigned long SAVE_INTERVAL_MS  = 60000;

// EMA filter
const float FILTER_ALPHA = 0.1f;
bool isFirstRead = true;

// Rest-timer for OCV calibration
static unsigned long rest_timer_ms = 0;

// ============================================================
// HELPER: Read raw battery voltage directly from ADC
// (averaged over N samples for noise rejection)
// ============================================================
float readRawVoltage(int samples = 5) {
  float sum = 0.0f;
  for (int i = 0; i < samples; i++) {
    int adc = analogRead(VOLTAGE_PIN);
    float pinV = ((float)adc / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    sum += pinV * VOLTAGE_MULTIPLIER;
    delay(10);
  }
  return sum / (float)samples;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  analogReadResolution(14);

  initEMMC();
  init_matrix_display();

  // Wait for voltage rails and sensor to stabilize
  Serial.println("Waiting for sensors to stabilize...");
  delay(3000);

  // ---- Boot-time OCV initialization ----
  // Read battery voltage (averaged, no load compensation - just use raw reading)
  float bootVoltage = readRawVoltage(10);
  float bootSOC     = get_soc_from_ocv(bootVoltage);
  accumulatedAh     = (bootSOC / 100.0f) * persistentState.actualMaxCapacityAh;
  systemVoltage     = bootVoltage;
  systemSOC         = bootSOC;

  Serial.println("======= BOOT CALIBRATION =======");
  Serial.print("  Boot Voltage   : "); Serial.print(bootVoltage, 3); Serial.println(" V");
  Serial.print("  Boot SOC (OCV) : "); Serial.print(bootSOC, 1);    Serial.println(" %");
  Serial.print("  Coulomb counter: "); Serial.print(accumulatedAh, 3); Serial.println(" Ah");
  Serial.print("  Max Capacity   : "); Serial.print(persistentState.actualMaxCapacityAh, 3); Serial.println(" Ah");
  Serial.println("================================");

  lastReadTime  = millis();
  lastPrintTime = millis();
  lastSaveTime  = millis();
  Serial.println("BMS ready.");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastReadTime >= READ_INTERVAL_MS) {
    unsigned long timeDelta = currentTime - lastReadTime;
    lastReadTime = currentTime;

    readSensors();
    integrateCoulombs(timeDelta);
    calculateSOC();
    calculateSOH();
  }

  if (currentTime - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = currentTime;
    printBMSData();
    render_soc(systemSOC);
  }

  if (currentTime - lastSaveTime >= SAVE_INTERVAL_MS) {
    lastSaveTime = currentTime;
    saveBMSState();
  }
}

// ============================================================
// EEPROM
// ============================================================
void initEMMC() {
  Serial.println("Initializing EEPROM...");
  eepromAdapter.init();
  emmcInitialized = true;
  loadBMSState();
}

void loadBMSState() {
  uint8_t* ptr = (uint8_t*)&persistentState;
  for (size_t i = 0; i < sizeof(persistentState); i++) {
    *ptr++ = eepromAdapter.readChip(EEPROM_START_ADDRESS + i);
  }

  bool corrupt = isnan(persistentState.actualMaxCapacityAh)
              || (persistentState.actualMaxCapacityAh < 1.0f)
              || (persistentState.actualMaxCapacityAh > 100.0f)
              || isnan(persistentState.cycleCount)
              || (persistentState.cycleCount < 0.0f);

  if (corrupt) {
    persistentState.cycleCount        = 0.0f;
    persistentState.actualMaxCapacityAh = BATTERY_NOMINAL_CAPACITY_AH;
    saveBMSState();
    Serial.println("Fresh EEPROM - default values written.");
  } else {
    Serial.print("EEPROM loaded: MaxCap=");
    Serial.print(persistentState.actualMaxCapacityAh, 3);
    Serial.print(" Ah, Cycles=");
    Serial.println(persistentState.cycleCount, 1);
  }
}

void saveBMSState() {
  uint8_t* ptr = (uint8_t*)&persistentState;
  for (size_t i = 0; i < sizeof(persistentState); i++) {
    eepromAdapter.writeChip(EEPROM_START_ADDRESS + i, *ptr++);
  }
  Serial.println("State saved to EEPROM.");
}

// ============================================================
// SENSOR READING
// ============================================================
void readSensors() {
  // Current (A0)
  int   currentADC      = analogRead(CURRENT_PIN);
  float currentPinV     = ((float)currentADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
  // NEGATIVE = discharge/load, POSITIVE = charging
  float rawCurrent      = (currentPinV - CURRENT_ZERO_POINT_V) / CURRENT_SENSITIVITY_V_PER_A;

  // Voltage (A1)
  int   voltageADC      = analogRead(VOLTAGE_PIN);
  float voltagePinV     = ((float)voltageADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
  float rawVoltage      = voltagePinV * VOLTAGE_MULTIPLIER;

  // Temperature (A2) - Steinhart-Hart NTC
  int   tempADC         = analogRead(TEMP_PIN);
  float tempPinV        = ((float)tempADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
  float rawTemp         = -999.0f;
  if (tempPinV > 0.01f) {
    float rNTC    = (NTC_VCC * NTC_R_FIXED / tempPinV) - (NTC_R_FIXED + NTC_R_SERIES);
    if (rNTC > 0.0f) {
      float s = log(rNTC / NTC_NOMINAL_RESISTANCE);
      s = (s / NTC_BETA) + (1.0f / NTC_NOMINAL_TEMP);
      rawTemp = (1.0f / s) - 273.15f;
    }
  }

  // EMA filter
  if (isFirstRead) {
    systemCurrent     = rawCurrent;
    systemVoltage     = rawVoltage;
    systemTemperature = rawTemp;
    isFirstRead       = false;
  } else {
    systemCurrent     = FILTER_ALPHA * rawCurrent + (1.0f - FILTER_ALPHA) * systemCurrent;
    systemVoltage     = FILTER_ALPHA * rawVoltage + (1.0f - FILTER_ALPHA) * systemVoltage;
    if (rawTemp > -900.0f) {
      systemTemperature = FILTER_ALPHA * rawTemp + (1.0f - FILTER_ALPHA) * systemTemperature;
    }
  }
}

// ============================================================
// COULOMB COUNTING
// Sign convention: negative current = discharge, positive = charge
// ============================================================
void integrateCoulombs(unsigned long timeDeltaMs) {
  float dt_hours = (float)timeDeltaMs / 3600000.0f;

  if (systemCurrent > 0.0f) {
    // Charging: apply coulombic efficiency
    accumulatedAh += systemCurrent * CHARGE_EFFICIENCY * dt_hours;
  } else {
    // Discharging (current is negative): directly subtract
    accumulatedAh += systemCurrent * dt_hours;
  }

  // Hard clamp
  if (accumulatedAh > persistentState.actualMaxCapacityAh) {
    accumulatedAh = persistentState.actualMaxCapacityAh;
  }
  if (accumulatedAh < 0.0f) {
    accumulatedAh = 0.0f;
  }
}

// ============================================================
// SOC CALCULATION (Hybrid: Coulomb Counting + OCV recalibration)
//
// Philosophy:
//  - PRIMARY:   Coulomb counting (integrateCoulombs) tracks SOC every 100ms
//  - SECONDARY: After 60 sec of rest (<50mA), OCV is trusted and resets the counter
//  - No blending filter during active load - avoids pulling SOC toward wrong voltage
// ============================================================
void calculateSOC() {
  bool isAtRest = (fabs(systemCurrent) < REST_CURRENT_THRESHOLD_A);

  if (isAtRest) {
    rest_timer_ms += READ_INTERVAL_MS;
  } else {
    rest_timer_ms = 0; // Any current flow resets the rest timer
  }

  // OCV recalibration: only trust voltage when battery has rested long enough
  if (rest_timer_ms >= REST_DURATION_MS) {
    float ocvSOC = get_soc_from_ocv(systemVoltage);
    accumulatedAh = (ocvSOC / 100.0f) * persistentState.actualMaxCapacityAh;
    rest_timer_ms = 0; // Reset so we don't recalibrate every loop after rest
    Serial.print("[OCV Recal] Voltage="); Serial.print(systemVoltage, 2);
    Serial.print("V OCV_SOC="); Serial.print(ocvSOC, 1);
    Serial.println("%");
  }

  // Final SOC from Coulomb counter
  systemSOC = (accumulatedAh / persistentState.actualMaxCapacityAh) * 100.0f;

  // Safety clamp
  if (systemSOC > 100.0f) systemSOC = 100.0f;
  if (systemSOC <   0.0f) systemSOC =   0.0f;
}

// ============================================================
// SOH CALCULATION (capacity fade via Ah throughput)
// ============================================================
void calculateSOH() {
  static float       cycleAhTracker  = 0.0f;
  static float       lastAh          = -1.0f;

  if (lastAh < 0.0f) { lastAh = accumulatedAh; return; } // init

  cycleAhTracker += fabs(accumulatedAh - lastAh);
  lastAh = accumulatedAh;

  // One full cycle = 2 × nominal capacity (one full charge + full discharge)
  if (cycleAhTracker >= (BATTERY_NOMINAL_CAPACITY_AH * 2.0f)) {
    persistentState.cycleCount++;
    cycleAhTracker = 0.0f;
    // Capacity fade: ~0.02% per cycle
    persistentState.actualMaxCapacityAh -= BATTERY_NOMINAL_CAPACITY_AH * 0.0002f;
    if (persistentState.actualMaxCapacityAh < 0.0f) {
      persistentState.actualMaxCapacityAh = 0.0f;
    }
    saveBMSState();
  }

  systemSOH = (persistentState.actualMaxCapacityAh / BATTERY_NOMINAL_CAPACITY_AH) * 100.0f;
}

// ============================================================
// SERIAL DEBUG OUTPUT
// ============================================================
void printBMSData() {
  Serial.println("======= BMS STATUS =======");
  Serial.print("Voltage  (V)  : "); Serial.println(systemVoltage, 3);
  Serial.print("Current  (A)  : "); Serial.println(systemCurrent, 4);
  Serial.print("Temp     (C)  : "); Serial.println(systemTemperature, 1);
  Serial.print("OCV SOC  (%)  : "); Serial.println(get_soc_from_ocv(systemVoltage), 1);
  Serial.print("Coulombs (Ah) : "); Serial.println(accumulatedAh, 4);
  Serial.print("MaxCap   (Ah) : "); Serial.println(persistentState.actualMaxCapacityAh, 3);
  Serial.print("SOC      (%)  : "); Serial.println(systemSOC, 1);
  Serial.print("SOH      (%)  : "); Serial.println(systemSOH, 1);
  Serial.print("Cycles        : "); Serial.println(persistentState.cycleCount, 1);
  Serial.print("Rest timer(ms): "); Serial.println(rest_timer_ms);
  Serial.println("==========================");
}
