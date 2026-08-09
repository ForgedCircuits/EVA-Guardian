  #include <EEPROMAdapter.h>

  // Battery Management System (BMS) Parameters and Pins
  const int CURRENT_PIN = A0;
  const int VOLTAGE_PIN = A1;
  const int TEMP_PIN = A2;
  const uint16_t EEPROM_START_ADDRESS = 0;

  // ADC Configuration
  const float ADC_REF_VOLTAGE = 3.3;
  const float ADC_MAX_VALUE = 16383.0; // 14-bit ADC (2^14 - 1)

  // Current Sensor (ACS723) Configuration
  const float CURRENT_ZERO_POINT_V = 1.70;
  const float CURRENT_SENSITIVITY_V_PER_A = 1.336; // Calculated sensitivity: 1.336 V/A (1336 mV/A)

  // Voltage Divider Configuration
  const float R1_VOLTAGE_DIVIDER = 6800.0; // 6.8K Ohm
  const float R2_VOLTAGE_DIVIDER = 3000.0; // 3K Ohm
  const float VOLTAGE_MULTIPLIER = (R1_VOLTAGE_DIVIDER + R2_VOLTAGE_DIVIDER) / R2_VOLTAGE_DIVIDER; 

  // Temperature Sensor (NTC) Configuration
  const float NTC_VCC = 5.0; 
  const float NTC_R_FIXED = 3300.0; 
  const float NTC_R_SERIES = 3300.0; 
  const float NTC_NOMINAL_RESISTANCE = 10000.0; 
  const float NTC_NOMINAL_TEMP = 298.15; 
  const float NTC_BETA = 3950.0; 

  // Battery Capacity Details
  const float BATTERY_NOMINAL_CAPACITY_AH = 18.650; // 18650 mAh as requested
  const float BATTERY_MAX_VOLTAGE = 8.4;
  const float BATTERY_MIN_VOLTAGE = 6.0; 

  // Struct for persistent data
  struct BMSState {
    float cycleCount;
    float actualMaxCapacityAh;
    float lastSavedSOC;
    float accumulatedAh; // Tracks current Ah in this cycle
  };

  BMSState persistentState;
  EEPROM::Adapter eepromAdapter = EEPROM::Adapter();

  // Live Variables
  float systemVoltage = 0.0;
  float systemCurrent = 0.0;
  float systemTemperature = 0.0;
  float systemSOC = 0.0;
  float systemSOH = 100.0; 
  bool emmcInitialized = false;

  // Timing variables for non-blocking loop
  unsigned long lastReadTime = 0;
  unsigned long lastPrintTime = 0;
  unsigned long lastSaveTime = 0;
  const unsigned long READ_INTERVAL_MS = 100; // Read sensors every 100ms
  const unsigned long PRINT_INTERVAL_MS = 1000; // Print every 1s
  const unsigned long SAVE_INTERVAL_MS = 60000; // Save state every 60s

  void setup() {
    Serial.begin(115200);
    
    // Force ADC resolution to 14 bits (0-16383)
    // The Arduino framework defaults to 10-bit (0-1023) for backwards compatibility unless we explicitly set it.
    analogReadResolution(14);
    
    initEMMC();
    
    // Give sensors time to stabilize
    delay(1000); 
    lastReadTime = millis();
    
    Serial.println("BMS Initialization Complete.");
  }

  void loop() {
    unsigned long currentTime = millis();
    
    // 1. Read Sensors and Integrate Current
    if (currentTime - lastReadTime >= READ_INTERVAL_MS) {
      unsigned long timeDelta = currentTime - lastReadTime;
      lastReadTime = currentTime;
      
      readSensors();
      integrateCoulombs(timeDelta);
      calculateSOC();
      calculateSOH();
    }
    
    // 2. Print Data
    if (currentTime - lastPrintTime >= PRINT_INTERVAL_MS) {
      lastPrintTime = currentTime;
      printBMSData();
    }
    
    // 3. Save State Periodically
    if (currentTime - lastSaveTime >= SAVE_INTERVAL_MS) {
      lastSaveTime = currentTime;
      saveBMSState();
    }
  }

  void initEMMC() {
    Serial.println("Initializing external EEPROM...");
    
    eepromAdapter.init();
    emmcInitialized = true;
    
    loadBMSState();
  }

  void loadBMSState() {
    // Read struct byte by byte from the external EEPROM
    uint8_t* ptr = (uint8_t*)&persistentState;
    for (size_t i = 0; i < sizeof(persistentState); i++) {
      *ptr = eepromAdapter.readChip(EEPROM_START_ADDRESS + i);
      ptr++;
    }
    
    // Check if EEPROM is uninitialized (often reads as NaN or out of bounds)
    if (isnan(persistentState.actualMaxCapacityAh) || persistentState.actualMaxCapacityAh <= 0.0 || persistentState.actualMaxCapacityAh > 50.0) {
      // Corrupt or fresh EEPROM, set initial values
      persistentState = {0.0, BATTERY_NOMINAL_CAPACITY_AH, 100.0, BATTERY_NOMINAL_CAPACITY_AH}; 
      saveBMSState();
      Serial.println("New BMS state created in external EEPROM.");
    } else {
      Serial.println("BMS state loaded from external EEPROM.");
    }
    
    systemSOC = persistentState.lastSavedSOC;
  }

  void saveBMSState() {
    persistentState.lastSavedSOC = systemSOC;
    
    // Write struct byte by byte to the external EEPROM
    uint8_t* ptr = (uint8_t*)&persistentState;
    for (size_t i = 0; i < sizeof(persistentState); i++) {
      eepromAdapter.writeChip(EEPROM_START_ADDRESS + i, *ptr);
      ptr++;
    }
    
    Serial.println("State saved to external EEPROM.");
  }

  // Filter configuration (0.0 to 1.0, lower is smoother but slower to respond)
  const float FILTER_ALPHA = 0.1;
  bool isFirstRead = true;

  void readSensors() {
    // Current (A0)
    int currentADC = analogRead(CURRENT_PIN);
    float currentVoltage = (currentADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float rawCurrent = (currentVoltage - CURRENT_ZERO_POINT_V) / CURRENT_SENSITIVITY_V_PER_A;
    
    // Voltage (A1)
    int voltageADC = analogRead(VOLTAGE_PIN);
    float pinA1Voltage = (voltageADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float rawVoltage = pinA1Voltage * VOLTAGE_MULTIPLIER;
    
    // Temperature (A2)
    int tempADC = analogRead(TEMP_PIN);
    float pinA2Voltage = (tempADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float rawTemp = -999.0;
    
    if (pinA2Voltage > 0.01) { 
      float rNTC = (NTC_VCC * NTC_R_FIXED / pinA2Voltage) - (NTC_R_FIXED + NTC_R_SERIES);
      float steinhart = rNTC / NTC_NOMINAL_RESISTANCE;     
      steinhart = log(steinhart);                     
      steinhart /= NTC_BETA;                          
      steinhart += 1.0 / NTC_NOMINAL_TEMP;            
      steinhart = 1.0 / steinhart;                    
      rawTemp = steinhart - 273.15;         
    }
    
    // Exponential Moving Average (EMA) Filter
    if (isFirstRead) {
      systemCurrent = rawCurrent;
      systemVoltage = rawVoltage;
      systemTemperature = rawTemp;
      isFirstRead = false;
    } else {
      systemCurrent = (FILTER_ALPHA * rawCurrent) + ((1.0 - FILTER_ALPHA) * systemCurrent);
      systemVoltage = (FILTER_ALPHA * rawVoltage) + ((1.0 - FILTER_ALPHA) * systemVoltage);
      // Don't filter error values
      if (rawTemp > -900.0) {
        systemTemperature = (FILTER_ALPHA * rawTemp) + ((1.0 - FILTER_ALPHA) * systemTemperature);
      }
    }
  }

  void integrateCoulombs(unsigned long timeDeltaMs) {
    // Convert time delta from milliseconds to hours
    float timeDeltaHours = (float)timeDeltaMs / 3600000.0;
    
    // Ah = Current (A) * Time (hours)
    // Negative current means discharging -> decreases accumulated Ah
    // Positive current means charging -> increases accumulated Ah
    persistentState.accumulatedAh += (systemCurrent * timeDeltaHours);
    
    // Clamp accumulated Ah between 0 and actual max capacity
    if (persistentState.accumulatedAh < 0.0) {
      persistentState.accumulatedAh = 0.0;
    }
    if (persistentState.accumulatedAh > persistentState.actualMaxCapacityAh) {
      persistentState.accumulatedAh = persistentState.actualMaxCapacityAh;
    }
  }

  void calculateSOC() {
    // Calculate Resting Voltage (compensate for voltage sag under load)
    // If negative current is discharging, voltage drops. V_rest = V_meas - (I * R_internal)
    // We assume a typical internal resistance. Adjust 0.1 if the SOC still drops too much under load.
    float compensatedVoltage = systemVoltage;
    
    if (systemCurrent < 0.0) {
      // Compensate for voltage sag during discharge
      compensatedVoltage = systemVoltage - (systemCurrent * 0.1); 
    } else if (systemCurrent > 0.0) {
      // Compensate for voltage inflation during charge
      compensatedVoltage = systemVoltage - (systemCurrent * 0.1); 
    }
    
    // Calculate Voltage-based SOC (0% to 100%)
    float voltageSOC = ((compensatedVoltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
    if (voltageSOC > 100.0) voltageSOC = 100.0;
    if (voltageSOC < 0.0) voltageSOC = 0.0;

    // Use pure Voltage-based SOC as requested!
    systemSOC = voltageSOC;
    
    // Keep Coulomb counting running in the background for debugging/Ah tracking
    float ahSOC = (persistentState.accumulatedAh / persistentState.actualMaxCapacityAh) * 100.0;
    if (ahSOC > 100.0) ahSOC = 100.0;
    if (ahSOC < 0.0) ahSOC = 0.0;
  }

  void calculateSOH() {
    // SOH = (Actual Max Capacity / Nominal Capacity) * 100
    
    // Track total Ah throughput to approximate cycles
    static float cycleAhTracker = 0;
    static float lastAccumulatedAh = persistentState.accumulatedAh;
    
    cycleAhTracker += abs(persistentState.accumulatedAh - lastAccumulatedAh);
    lastAccumulatedAh = persistentState.accumulatedAh;
    
    // Every time throughput equals 2 * Nominal Capacity (one full charge and discharge), add a cycle
    if (cycleAhTracker >= (BATTERY_NOMINAL_CAPACITY_AH * 2.0)) {
      persistentState.cycleCount += 1.0;
      cycleAhTracker = 0.0;
      
      // Simulate capacity fade: 0.02% per cycle
      persistentState.actualMaxCapacityAh -= (BATTERY_NOMINAL_CAPACITY_AH * 0.0002); 
      if (persistentState.actualMaxCapacityAh < 0) persistentState.actualMaxCapacityAh = 0;
      
      saveBMSState(); // Save state on cycle update
    }
    
    systemSOH = (persistentState.actualMaxCapacityAh / BATTERY_NOMINAL_CAPACITY_AH) * 100.0;
  }

  void printBMSData() {
    Serial.println("======= BMS STATUS =======");
    Serial.print("Voltage (V):   "); Serial.println(systemVoltage, 2);
    Serial.print("Current (A):   "); Serial.println(systemCurrent, 3);
    Serial.print("Temperature(C):"); Serial.println(systemTemperature, 2);
    Serial.print("SOC (%):       "); Serial.print(systemSOC, 1); 
    Serial.print(" (Ah: "); Serial.print(persistentState.accumulatedAh, 2); Serial.println(")");
    Serial.print("SOH (%):       "); Serial.print(systemSOH, 1);
    Serial.print(" (Cycles: "); Serial.print(persistentState.cycleCount, 1); Serial.println(")");
    Serial.println("==========================");
  }
