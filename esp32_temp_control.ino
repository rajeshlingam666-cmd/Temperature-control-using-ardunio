/*
 * ESP32 IoT Smart Temperature Control System
 * ============================================
 * Hardware:
 *   - ESP32 DevKit C V4
 *   - DHT22 Temperature Sensor (Data → GPIO 15)
 *   - LCD 16x2 in 4-bit mode (RS→21, EN→22, D4→18, D5→19, D6→23, D7→5)
 *   - Servo Motor / Fan (PWM → GPIO 4)
 *   - Buzzer (Signal → GPIO 13)
 *   - SET Button (GPIO 25, INPUT_PULLUP)
 *   - UP  Button (GPIO 26, INPUT_PULLUP)
 *   - DOWN Button (GPIO 27, INPUT_PULLUP)
 *
 * ThingSpeak:
 *   - Channel ID : 3423267
 *   - Upload every 20 seconds
 *   - Field 1 : Temperature (°C)
 *   - Field 2 : Fan Speed (%)
 *
 * Libraries required (install via Arduino Library Manager):
 *   WiFi         (built-in ESP32 core)
 *   ThingSpeak   by MathWorks
 *   LiquidCrystal (built-in Arduino / ESP32 core)
 *   ESP32Servo   by Kevin Harrington
 *   DHTesp       by Bernd Giesecke
 */

// ─── Library Includes ───────────────────────────────────────────────────────
#include <WiFi.h>
#include <ThingSpeak.h>
#include <LiquidCrystal.h>
#include <ESP32Servo.h>
#include <DHTesp.h>

// ─── WiFi Credentials ───────────────────────────────────────────────────────
const char* ssid     = "Wokwi-GUEST";
const char* password = "";

// ─── ThingSpeak Configuration ───────────────────────────────────────────────
unsigned long channelID   = 3423267;
const char*   writeAPIKey = "YOUR_WRITE_API_KEY_HERE"; // ← Replace with your key

// ─── Pin Definitions ────────────────────────────────────────────────────────
#define DHT_PIN       15
#define SERVO_PIN      4
#define BUZZER_PIN    13
#define BTN_SET       25   // Cycles setting mode
#define BTN_UP        26   // Increases selected threshold
#define BTN_DOWN      27   // Decreases selected threshold

// LCD: RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(21, 22, 18, 19, 23, 5);

// ─── Objects ────────────────────────────────────────────────────────────────
DHTesp   dht;
Servo    fanServo;
WiFiClient wifiClient;

// ─── Temperature Thresholds (editable via buttons) ──────────────────────────
float lowThreshold  = 20.5;   // °C – below this fan is OFF
float highThreshold = 60.5;   // °C – above this fan is full speed + buzzer

// ─── State Machine ──────────────────────────────────────────────────────────
// 0 = Normal, 1 = Set Low Threshold, 2 = Set High Threshold
int settingMode = 0;

// ─── Sensor & Control State ─────────────────────────────────────────────────
float temperature  = 0.0;
int   fanPercent   = 0;        // 0–100 %
int   servoAngle   = 0;        // 0–180 °
bool  dhtError     = false;    // Fix 4: tracks whether the last DHT read failed

// ─── Button Edge Detection + Debounce ───────────────────────────────────────
// Each button tracks its previous stable state and the moment it last changed.
// An action fires only on a confirmed HIGH→LOW transition (falling edge) —
// meaning the button must be pressed AND released before it fires again.
// This eliminates the continuous-repeat problem caused by holding a button.
const unsigned long DEBOUNCE_MS = 50;   // 50 ms is sufficient for mechanical switches

int           btnSetState  = HIGH,  btnUpState  = HIGH,  btnDownState  = HIGH;
unsigned long btnSetChange = 0,     btnUpChange = 0,     btnDownChange = 0;
bool          btnSetArmed  = true,  btnUpArmed  = true,  btnDownArmed  = true;

// ─── ThingSpeak Upload Timing ───────────────────────────────────────────────
unsigned long lastUploadTime = 0;
const unsigned long UPLOAD_INTERVAL_MS = 20000; // 20 seconds

// ─── LCD Refresh Timing ─────────────────────────────────────────────────────
unsigned long lastLCDTime = 0;
const unsigned long LCD_REFRESH_MS = 500;

// ─── DHT Read Timing ────────────────────────────────────────────────────────
unsigned long lastDHTTime = 0;
const unsigned long DHT_INTERVAL_MS = 2000; // DHT22 minimum 2 s between reads

// ────────────────────────────────────────────────────────────────────────────
//  SETUP
// ────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // ── LCD Splash Screen ──────────────────────────────────────────────────
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Welcome To  ");
  lcd.setCursor(0, 1);
  lcd.print("  Temp Control");
  delay(2000);
  lcd.clear();

  // ── DHT22 ──────────────────────────────────────────────────────────────
  dht.setup(DHT_PIN, DHTesp::DHT22);

  // ── Servo ──────────────────────────────────────────────────────────────
  fanServo.attach(SERVO_PIN);
  fanServo.write(0); // Fan OFF at startup

  // ── Buzzer ─────────────────────────────────────────────────────────────
  // tone()/noTone() manage the pin mode internally — no pinMode needed.
  noTone(BUZZER_PIN);   // Ensure buzzer is silent at startup

  // ── Buttons (INPUT_PULLUP → LOW when pressed) ──────────────────────────
  pinMode(BTN_SET,  INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // ── WiFi Connection ────────────────────────────────────────────────────
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed – continuing offline.");
  }

  // ── ThingSpeak ─────────────────────────────────────────────────────────
  ThingSpeak.begin(wifiClient);
}

// ────────────────────────────────────────────────────────────────────────────
//  LOOP
// ────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Read DHT22 every 2 seconds
  if (now - lastDHTTime >= DHT_INTERVAL_MS) {
    lastDHTTime = now;
    TempAndHumidity data = dht.getTempAndHumidity();
    if (isnan(data.temperature)) {
      // Fix 4: Report read failure explicitly instead of silently keeping stale value.
      // The previous temperature is preserved so fan/buzzer logic remains stable.
      Serial.println("DHT Read Failed – retaining last known temperature.");
      dhtError = true;
    } else {
      temperature = data.temperature;
      dhtError    = false;
    }
  }

  handleButtons();
  controlFan();
  controlBuzzer();

  // Print temperature and fan speed to Serial every DHT cycle
  if (now - lastDHTTime < 10) {   // runs once right after a fresh DHT read
    if (!dhtError) {
      Serial.print("Temp: ");
      Serial.print(temperature, 1);
      Serial.print(" C  |  Fan: ");
      Serial.print(fanPercent);
      Serial.println("%");
    }
  }

  // Refresh LCD every 500 ms
  if (now - lastLCDTime >= LCD_REFRESH_MS) {
    lastLCDTime = now;
    updateLCD();
  }

  // Upload to ThingSpeak every 20 seconds
  if (now - lastUploadTime >= UPLOAD_INTERVAL_MS) {
    lastUploadTime = now;
    uploadThingSpeak();
  }
}

// ────────────────────────────────────────────────────────────────────────────
//  EDGE DETECTION HELPER
//  Returns true exactly once per physical press.
//
//  Key fix: rawState (the live pin reading) is separated from lastState
//  (the confirmed stable state). The debounce timer resets on any raw change,
//  but lastState and armed only update AFTER the signal has been stable for
//  the full DEBOUNCE_MS window. This prevents a bouncy release from
//  instantly re-arming and firing a phantom second press.
// ────────────────────────────────────────────────────────────────────────────
bool buttonPressed(int pin, int &lastState, unsigned long &lastChange, bool &armed) {
  unsigned long now     = millis();
  int           reading = digitalRead(pin);

  // Any raw level change restarts the debounce timer
  if (reading != lastState) {
    lastChange = now;
  }

  // Only commit the new state once the signal has been stable long enough
  if ((now - lastChange) >= DEBOUNCE_MS) {

    if (reading == LOW && lastState == HIGH && armed) {
      // Confirmed falling edge: fire once and disarm
      lastState = LOW;
      armed     = false;
      return true;
    }

    if (reading == HIGH && lastState == LOW) {
      // Confirmed rising edge (stable release): update state and re-arm
      lastState = HIGH;
      armed     = true;
    }
  }

  return false;
}

// ────────────────────────────────────────────────────────────────────────────
//  HANDLE BUTTONS
//  SET  → cycles settingMode (0→1→2→0)
//  UP   → increases currently selected threshold by 0.1 °C
//  DOWN → decreases currently selected threshold by 0.1 °C
// ────────────────────────────────────────────────────────────────────────────
void handleButtons() {

  // ── SET button ─────────────────────────────────────────────────────────
  if (buttonPressed(BTN_SET, btnSetState, btnSetChange, btnSetArmed)) {
    settingMode = (settingMode + 1) % 3;  // Cycle 0 → 1 → 2 → 0
    lastLCDTime = 0;                       // Force immediate LCD refresh
    Serial.print("Setting mode: ");
    Serial.println(settingMode);
  }

  // ── UP button ──────────────────────────────────────────────────────────
  if (buttonPressed(BTN_UP, btnUpState, btnUpChange, btnUpArmed)) {
    if (settingMode == 1) {
      lowThreshold = roundf((lowThreshold + 0.1f) * 10.0f) / 10.0f;
      if (lowThreshold >= highThreshold)
        lowThreshold = roundf((highThreshold - 0.1f) * 10.0f) / 10.0f;
      Serial.print("Low threshold: "); Serial.println(lowThreshold, 1);
    } else if (settingMode == 2) {
      highThreshold = roundf((highThreshold + 0.1f) * 10.0f) / 10.0f;
      Serial.print("High threshold: "); Serial.println(highThreshold, 1);
    }
  }

  // ── DOWN button ────────────────────────────────────────────────────────
  if (buttonPressed(BTN_DOWN, btnDownState, btnDownChange, btnDownArmed)) {
    if (settingMode == 1) {
      lowThreshold = roundf((lowThreshold - 0.1f) * 10.0f) / 10.0f;
      Serial.print("Low threshold: "); Serial.println(lowThreshold, 1);
    } else if (settingMode == 2) {
      highThreshold = roundf((highThreshold - 0.1f) * 10.0f) / 10.0f;
      if (highThreshold <= lowThreshold)
        highThreshold = roundf((lowThreshold + 0.1f) * 10.0f) / 10.0f;
      Serial.print("High threshold: "); Serial.println(highThreshold, 1);
    }
  }
}

// ────────────────────────────────────────────────────────────────────────────
//  UPDATE LCD
// ────────────────────────────────────────────────────────────────────────────
void updateLCD() {
  if (settingMode == 0) {
    // ── Normal Mode ────────────────────────────────────────────────────
    // Row 0: "Temp: XX.X C"  — or sensor error notice (Fix 4)
    lcd.setCursor(0, 0);
    if (dhtError) {
      lcd.print("Sensor ERR!     "); // 16 chars, clears any stale content
    } else {
      lcd.print("Temp: ");
      lcd.print(temperature, 1);
      lcd.print(" C  "); // trailing spaces clear stale chars
    }

    // Row 1: "Fan: XX%"
    lcd.setCursor(0, 1);
    lcd.print("Fan: ");
    lcd.print(fanPercent);
    lcd.print("%   "); // trailing spaces
  } else {
    // ── Setting Mode ───────────────────────────────────────────────────
    // Row 0: "Setting Mode" with indicator of which threshold
    lcd.setCursor(0, 0);
    if (settingMode == 1) {
      lcd.print("Set Low Thresh  ");
    } else {
      lcd.print("Set High Thresh ");
    }

    // Row 1: "L:XX.X H:XX.X"
    lcd.setCursor(0, 1);
    lcd.print("L:");
    lcd.print(lowThreshold, 1);
    lcd.print(" H:");
    lcd.print(highThreshold, 1);
    lcd.print("  ");
  }
}

// ────────────────────────────────────────────────────────────────────────────
//  CONTROL FAN (Servo)
//  < lowThreshold  → OFF  (0°)
//  > highThreshold → FULL (180°)
//  Between         → proportional 0°–180°
// ────────────────────────────────────────────────────────────────────────────
void controlFan() {
  if (temperature <= lowThreshold) {
    servoAngle = 0;
    fanPercent = 0;
  } else if (temperature >= highThreshold) {
    servoAngle = 180;
    fanPercent = 100;
  } else {
    // Linear interpolation between thresholds
    float ratio = (temperature - lowThreshold) / (highThreshold - lowThreshold);
    // Fix 3: constrain() guards against any floating-point edge cases
    servoAngle = constrain((int)(ratio * 180.0f), 0, 180);
    fanPercent = constrain((int)(ratio * 100.0f), 0, 100);
  }

  fanServo.write(servoAngle);
}

// ────────────────────────────────────────────────────────────────────────────
//  CONTROL BUZZER
//  ON  when temperature > highThreshold
//  OFF otherwise
//
//  Root cause of silence: wokwi-buzzer is a PASSIVE buzzer.
//  A passive buzzer needs a toggling AC signal to vibrate its diaphragm.
//  digitalWrite(HIGH) just holds the pin steady → zero oscillation → no sound.
//  tone() generates the required PWM square wave at the given frequency.
//  noTone() stops the oscillation and releases the pin.
// ────────────────────────────────────────────────────────────────────────────
void controlBuzzer() {
  if (temperature > highThreshold) {
    tone(BUZZER_PIN, 1000);   // 1 kHz alarm tone — clearly audible in Wokwi
  } else {
    noTone(BUZZER_PIN);        // Stop oscillation, pin goes LOW
  }
}

// ────────────────────────────────────────────────────────────────────────────
//  UPLOAD TO THINGSPEAK
//  Field 1 → Temperature (°C)
//  Field 2 → Fan Speed (%)
// ────────────────────────────────────────────────────────────────────────────
void uploadThingSpeak() {
  // Fix 5: If WiFi dropped since startup, attempt a non-blocking reconnect.
  // WiFi.reconnect() re-uses the credentials from WiFi.begin() in setup().
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak: WiFi lost – attempting reconnect…");
    WiFi.reconnect();
    // Give the radio up to 3 seconds to re-associate before this upload cycle.
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) {
      delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("ThingSpeak: Reconnect failed – skipping this upload.");
      return;
    }
    Serial.println("ThingSpeak: Reconnected!");
  }

  ThingSpeak.setField(1, temperature);
  ThingSpeak.setField(2, fanPercent);

  int httpCode = ThingSpeak.writeFields(channelID, writeAPIKey);

  if (httpCode == 200) {
    Serial.println("ThingSpeak: Upload OK");
  } else {
    Serial.print("ThingSpeak: Upload failed, code = ");
    Serial.println(httpCode);
  }
}
