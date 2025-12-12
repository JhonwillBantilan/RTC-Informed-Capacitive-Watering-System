#include <Wire.h>
#include "RTClib.h"
#include <LiquidCrystal_I2C.h>


// 1. PIN & SETTINGS
const int RELAY_PIN = 7;               // D7 → Relay IN
const int MOISTURE_SENSOR_PIN = A0;    // A0 → Sensor OUT

#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// Thresholds (set during calibration)
int WET_THRESHOLD;  // Soil wet → skip watering
int DRY_THRESHOLD;  // Soil dry → start watering

// Timing
const long WATERING_DURATION_MS = 5000;     // 5 seconds pump on
const long CALIBRATION_DELAY_MS = 5000;     // Short wait before reading

// Scheduled times for moisture checks (hour, minute pairs)
struct ScheduledTime {
  int hour;
  int minute;
};

ScheduledTime SCHEDULE[] = {
  {8, 0},
  {12, 0},
  {16, 0},
  {20, 0}
};
const int NUM_SCHEDULED = sizeof(SCHEDULE) / sizeof(SCHEDULE[0]);


// 2. OBJECTS
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);


// 3. HELPER FUNCTIONS
void safePumpOff() {
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("Pump OFF (safety).");
}

// *** MODIFIED: Reduced delays in displayLCD to prevent loop slowdown ***
void displayLCD(const char* line1, const char* line2) {
  lcd.clear();  // Ensure clean slate
  delay(5);     // Reduced delay from 400ms
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  delay(5);     // Reduced delay from 400ms
  Serial.print("LCD: ");
  Serial.print(line1);
  Serial.print(" | ");
  Serial.println(line2);
}

// 4. CALIBRATION FUNCTION
void calibrateSensor() {
  // Dry first, then wet
  displayLCD("Calibration", "Place sensor in");
  displayLCD("Calibration", "DRY soil/air");
  Serial.println("Calibration: Place sensor in DRY soil or air. Waiting 10 seconds...");
  delay(10000);

  int dryReading = analogRead(MOISTURE_SENSOR_PIN);
  char dryStr[17];
  sprintf(dryStr, "Dry: %d", dryReading);
  displayLCD("Dry Reading", dryStr);
  Serial.print("Dry reading: ");
  Serial.println(dryReading);
  delay(2000);

  displayLCD("Calibration", "Place sensor in");
  displayLCD("Calibration", "WET soil/water");
  Serial.println("Calibration: Place sensor in WET soil or water. Waiting 10 seconds...");
  delay(10000);

  int wetReading = analogRead(MOISTURE_SENSOR_PIN);
  char wetStr[17];
  sprintf(wetStr, "Wet: %d", wetReading);
  displayLCD("Wet Reading", wetStr);
  Serial.print("Wet reading: ");
  Serial.println(wetReading);
  delay(2000);

  DRY_THRESHOLD = dryReading - 50;
  WET_THRESHOLD = wetReading + 50;

  if (WET_THRESHOLD >= DRY_THRESHOLD) {
    WET_THRESHOLD = DRY_THRESHOLD - 100;
  }

  char threshStr[17];
  sprintf(threshStr, "W:%d D:%d", WET_THRESHOLD, DRY_THRESHOLD);
  displayLCD("Thresholds Set", threshStr);
  Serial.print("Thresholds - Wet: ");
  Serial.print(WET_THRESHOLD);
  Serial.print(", Dry: ");
  Serial.println(DRY_THRESHOLD);
  delay(3000);

  displayLCD("Calibration", "Complete!");
  Serial.println("Calibration complete.");
  delay(2000);
}

// 5. WATERING SEQUENCE
void waterPlant() {
  safePumpOff();  // Ensure off before starting
  Serial.println("Starting water pump...");
  
  // Turn pump ON
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("Pump ON - Watering started.");
  displayLCD("Watering...", "Pump ON");
  delay(WATERING_DURATION_MS);  // Exactly 5 seconds
  safePumpOff();  // Force off immediately
  delay(800);
  Serial.println("Pump OFF - Watering complete.");
  
  displayLCD("Watering Done", "Pump OFF");
  delay(2000);
}


// 6. SCHEDULING FUNCTIONS
bool isScheduled(DateTime now) {
  int hour = now.hour();
  int minute = now.minute();
  for (int i = 0; i < NUM_SCHEDULED; i++) {
    if (hour == SCHEDULE[i].hour && minute == SCHEDULE[i].minute) {
      return true;
    }
  }
  return false;
}

DateTime getNextScheduledTime(DateTime now) {
  int currentHour = now.hour();
  int currentMinute = now.minute();
  for (int i = 0; i < NUM_SCHEDULED; i++) {
    int h = SCHEDULE[i].hour;
    int m = SCHEDULE[i].minute;
    if (h > currentHour || (h == currentHour && m > currentMinute)) {
      return DateTime(now.year(), now.month(), now.day(), h, m, 0);
    }
  }
  // Next day, first scheduled time
  return DateTime(now.year(), now.month(), now.day() + 1, SCHEDULE[0].hour, SCHEDULE[0].minute, 0);
}

// 7. SETUP 💧
void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  safePumpOff();
  pinMode(MOISTURE_SENSOR_PIN, INPUT);

  Wire.begin();
  Wire.setClock(100000);  // Slow I2C
  lcd.init();
  lcd.backlight();
  delay(500);

  displayLCD("Watering System", "Initializing...");
  delay(2000);

  if (!rtc.begin()) {
    displayLCD("RTC ERROR!", "Check Module");
    Serial.println("Error: RTC not detected!");
    while (1);
  }

  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)) - TimeSpan(0, 0, 0, 0)); 
  
  calibrateSensor();
  displayLCD("System Ready!", "Starting...");
  delay(2000);
}


// 8. LOOP - *** MODIFIED to track seconds and reduce display update frequency ***
void loop() {
  static int lastMinute = -1;  // Track the last checked minute
  static int lastSecond = -1;  // New: Track the last second for display update

  DateTime now = rtc.now();

  if (now.minute() != lastMinute && isScheduled(now)) {
    // Scheduled time: perform moisture check
    lastMinute = now.minute();

    int moisture = analogRead(MOISTURE_SENSOR_PIN);
    char moistStr[17];
    sprintf(moistStr, "Moisture: %d", moisture);
    displayLCD("Checking...", moistStr);
    Serial.print("Moisture: ");
    Serial.println(moisture);
    delay(1000);

    if (moisture >= 1000) {
      displayLCD("Sensor Error!", "Check Probe!");
      Serial.println("Sensor disconnected or in air!");
      safePumpOff();
      delay(5000);
    } else if (moisture < WET_THRESHOLD) {
      Serial.println("Very wet — skipping watering.");
      displayLCD("Very Wet!", "Skipping");
      delay(2000);
    } else if (moisture > DRY_THRESHOLD) {
      Serial.println("Dry — starting watering...");
      displayLCD("Dry!", "Watering...");
      delay(1000);
      waterPlant();
    } else {
      Serial.println("Moisture OK.");
      displayLCD("Moisture OK!", "");
      delay(2000);
    }
  } else {
    // Not scheduled: only update the LCD once per second
    if (now.second() != lastSecond) {
      lastSecond = now.second(); // Update the tracker

      DateTime next = getNextScheduledTime(now);
      char nextStr[17];
      sprintf(nextStr, "Next: %02d:%02d", next.hour(), next.minute());
      
      // Display current RTC Time for verification
      char nowStr[17];
      sprintf(nowStr, "RTC: %02d:%02d:%02d", now.hour(), now.minute(), now.second());
      displayLCD(nowStr, nextStr);
      
      // Print to Serial once when next scheduled time changes
      static DateTime lastPrintedNext = DateTime(2000, 1, 1, 0, 0, 0);
      if (next != lastPrintedNext) {
        lastPrintedNext = next;
        Serial.print("Waiting | Next: ");
        Serial.print(next.hour());
        Serial.print(":");
        Serial.println(next.minute());
      }
    }
  }

  safePumpOff();
  delay(10); // Minimal loop delay added to ensure the loop doesn't run too fast
}