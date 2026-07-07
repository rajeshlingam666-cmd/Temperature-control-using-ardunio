# Smart Temperature Control System

## Problem Statement

In many environments — server rooms, greenhouses, industrial setups, and home appliances — temperature regulation is critical. Uncontrolled temperature rise can damage equipment, reduce efficiency, or create unsafe conditions.

Manual monitoring and intervention is neither reliable nor fast enough to prevent damage. There is a need for an automated system that continuously reads temperature, proportionally controls a cooling fan based on the temperature range, and triggers a buzzer alarm when the temperature exceeds a critical high threshold — all without any human intervention.

---

## Project Description

The **Smart Temperature Control System** is an Arduino-based embedded system that reads real-time temperature from a TMP36 analog sensor and takes automated actions based on user-defined low and high threshold values:

- **Fan speed** is proportionally controlled (PWM) between the low and high threshold
- **Buzzer** activates as an alarm when temperature exceeds the high threshold
- **LCD** displays live temperature, fan duty cycle, and threshold settings
- **Three push buttons** allow the user to configure low and high thresholds on the fly

---

## Features

- Real-time temperature sensing using the TMP36 analog sensor
- Proportional fan speed control via PWM (0–100% duty cycle)
- Buzzer alarm triggered when temperature crosses the high threshold
- 16x2 LCD display showing temperature, fan speed %, and settings
- User-configurable low and high temperature thresholds via SET / UP / DOWN buttons
- Setting Mode on LCD for live threshold adjustment
- Splash screen on startup ("Welcome To Temp Control")
- Simulated and tested on Tinkercad Circuits

---

## Components Used

| Component | Pin | Description |
|---|---|---|
| Arduino Uno | — | Microcontroller board |
| TMP36 Temperature Sensor | A0 | Reads analog temperature voltage |
| 16x2 LCD Display | 2, 3, 4, 5, 6, 7 | Shows temperature, fan %, settings |
| Fan (DC Motor) | Pin 9 (PWM) | Proportional cooling via PWM |
| Buzzer | Pin 13 | Audio alarm above high threshold |
| SET Button | A3 | Cycles through Normal → Set Low → Set High modes |
| UP Button | A4 | Increases selected threshold by 0.1°C |
| DOWN Button | A5 | Decreases selected threshold by 0.1°C |
| Connecting Wires | — | Circuit connections |
| Tinkercad | — | Simulation environment |

---

## Working

### Temperature Reading
The TMP36 sensor outputs an analog voltage. The Arduino converts it using the formula:

```
Voltage   = sensorValue × (5.0 / 1024.0)
Temperature (°C) = (Voltage − 0.5) × 100.0
```

### Fan Control (PWM)
The fan speed is proportionally mapped between the low threshold (`setL_temp`, default 20.5°C) and the high threshold (`setH_temp`, default 60.5°C):

- Below `setL_temp` → Fan is **OFF** (0%)
- Between `setL_temp` and `setH_temp` → Fan runs at **proportional speed** (0–100%)
- Above `setH_temp` → Fan runs at **full speed** (100%)

### Buzzer Alarm
- When temperature **exceeds** `setH_temp` → Buzzer turns **ON**
- When temperature is **below or equal** to `setH_temp` → Buzzer turns **OFF**

### Button Controls
| Button | Action |
|---|---|
| SET (A3) | Cycles: Normal Mode → Set Low Threshold → Set High Threshold → Normal |
| UP (A4) | Increases the currently selected threshold by 0.1°C |
| DOWN (A5) | Decreases the currently selected threshold by 0.1°C |

### LCD Display
| Mode | Row 1 | Row 2 |
|---|---|---|
| Normal (Set=0) | `Temp: XX.X°C` | `Fan: XX%` |
| Setting (Set=1 or 2) | `Setting Mode` | `L: XX.X  H: XX.X` |

---

## Circuit Diagram / Simulation

View the live simulation on Tinkercad:
[Smart Temperature Control - Tinkercad](https://www.tinkercad.com/things/8bkLLqdID3g-fantabulous-snaget-esboo)

---

## Code

```cpp
#include <EEPROM.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

#define TempSensorPin A0
#define bt_set  A3
#define bt_up   A4
#define bt_down A5
#define fan     9
#define buzzer  13

float setL_temp = 20.5;   // Default low threshold
float setH_temp = 60.5;   // Default high threshold
float temperature = 0;

int duty_cycle;
int Set = 0;
int flag = 0;

void setup() {
  pinMode(TempSensorPin, INPUT);
  pinMode(bt_set, INPUT_PULLUP);
  pinMode(bt_up, INPUT_PULLUP);
  pinMode(bt_down, INPUT_PULLUP);
  pinMode(fan, OUTPUT);
  pinMode(buzzer, OUTPUT);

  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Welcome To");
  lcd.setCursor(0, 1);
  lcd.print("Temp Control");
  delay(2000);
  lcd.clear();
}

void loop() {
  int sensorValue = analogRead(TempSensorPin);

  // TMP36 Temperature Formula
  float voltage = sensorValue * (5.0 / 1024.0);
  temperature = (voltage - 0.5) * 100.0;

  int value1 = temperature * 10;
  int value2 = setL_temp * 10;
  int value3 = setH_temp * 10;

  duty_cycle = map(value1, value2, value3, 0, 100);
  if (duty_cycle > 100) duty_cycle = 100;
  if (duty_cycle < 0)   duty_cycle = 0;

  // Fan Control (PWM)
  if (temperature < setL_temp)
    analogWrite(fan, 0);
  else
    analogWrite(fan, map(duty_cycle, 0, 100, 0, 255));

  // Buzzer Alarm
  if (temperature > setH_temp)
    digitalWrite(buzzer, HIGH);
  else
    digitalWrite(buzzer, LOW);

  handleButtons();
  displayTemperature();
  delay(300);
}

void handleButtons() {
  if (digitalRead(bt_set) == LOW) {
    if (flag == 0) {
      flag = 1;
      Set++;
      if (Set > 2) Set = 0;
      delay(200);
    }
  } else { flag = 0; }

  if (digitalRead(bt_up) == LOW) {
    if (Set == 1) setL_temp += 0.1;
    if (Set == 2) setH_temp += 0.1;
    delay(100);
  }

  if (digitalRead(bt_down) == LOW) {
    if (Set == 1) setL_temp -= 0.1;
    if (Set == 2) setH_temp -= 0.1;
    delay(100);
  }
}

void displayTemperature() {
  lcd.clear();
  if (Set == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Temp:");
    lcd.print(temperature, 1);
    lcd.write((byte)223);  // Degree symbol
    lcd.print("C");
    lcd.setCursor(0, 1);
    lcd.print("Fan:");
    lcd.print(duty_cycle);
    lcd.print("%");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Setting Mode");
    lcd.setCursor(0, 1);
    lcd.print("L:");
    lcd.print(setL_temp, 1);
    lcd.print(" H:");
    lcd.print(setH_temp, 1);
  }
}
```

---

## Flow of Execution

```
START
  |
  +--> Setup: Init LCD, Pins, Show Splash Screen
  |
  +--> Loop:
          |
          +--> Read TMP36 Sensor (A0) → Convert to °C
          |
          +--> Calculate Fan Duty Cycle (map between L and H threshold)
          |
          +--> Temp < setL_temp?  --> Fan OFF
          |    Temp in range?     --> Fan ON (proportional PWM)
          |    Temp > setH_temp?  --> Fan 100% + Buzzer ON
          |
          +--> Handle Buttons (SET / UP / DOWN)
          |
          +--> Update LCD Display
                  |
                  +-- Set=0 --> Show Temp + Fan %
                  +-- Set=1 --> Setting Mode (adjust Low Threshold)
                  +-- Set=2 --> Setting Mode (adjust High Threshold)
```

---

## Temperature vs Fan/Buzzer Behavior

| Temperature Range | Fan Speed | Buzzer |
|---|---|---|
| Below `setL_temp` (< 20.5°C) | OFF (0%) | OFF |
| Between `setL_temp` and `setH_temp` | Proportional (0–100%) | OFF |
| Above `setH_temp` (> 60.5°C) | Full Speed (100%) | ON (Alarm) |

---

## Future Scope

- Save threshold settings to EEPROM so they persist after power off
- Add Wi-Fi (ESP8266/ESP32) for remote monitoring and alerts via mobile
- Replace buzzer with a relay-controlled AC fan for real-world use
- Add historical temperature graph logging
- Integrate with home automation platforms like Home Assistant or Blynk

---

## Conclusion

The Smart Temperature Control System showcases a practical embedded systems solution combining sensor input, proportional PWM output, alarm triggering, and a user-friendly interface. It forms a strong foundation for real-world applications in climate control, server cooling, industrial safety monitoring, and smart home automation.
