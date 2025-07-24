
# IoT Water Level Indicator & Motor Controller with Telegram Bot


A robust, feature-rich IoT project for monitoring water tank levels and controlling a water pump motor using an ESP32, ultrasonic sensor, LCD, LEDs, and Telegram Bot integration. Designed for reliability, remote access, and user management.

![MIT License](https://img.shields.io/badge/License-MIT-green.svg)

---


## Features


- **Real-Time Water Level Monitoring:**
  - Uses an ultrasonic sensor for accurate tank level measurement with noise filtering.
- **LCD Display:**
  - 16x2 I2C LCD shows water level, tanker loads, and motor status.
- **LED Indicators:**
  - 6 LEDs provide a quick visual bar-graph of water level (from very low to full).
- **Motor Control:**
  - Automatic and manual (physical switch & Telegram) control of the water pump relay.
- **Telegram Bot Integration:**
  - Remotely turn motor ON/OFF, check water level, and get system status.
  - Admin can add/remove users and list allowed chat IDs.
  - Secure: Only authorized users can control the system.
- **Persistent Settings:**
  - Uses ESP32 NVS to remember allowed users and motor state across reboots.
- **Automatic Reboot:**
  - ESP32 reboots every 15 minutes for reliability.

---



## Hardware Requirements

- ESP32 Development Board
- Ultrasonic Sensor (HC-SR04 or compatible)
- 16x2 I2C LCD Display
- Relay Module (for motor control)
- 6x LEDs (different colors recommended)
- Physical ON/OFF Switch
- Wires, Resistors, Power Supply, etc.



> **Note:**
> 
> After wiring, you must adjust the following parameters in `Water_Level_Indicator.ino` to match your tank and sensor:
> - `water_tank_depth`: Set this to the depth of your water tank (in cm, measured from the sensor to the bottom when empty).
> - `min_distance`: Set this to the minimum distance your ultrasonic sensor reads when the tank is full (in cm).
> - `full_water`: Set this to your tank's full capacity in liters.
> 
> These values are critical for accurate water level calculation. Use your own measurements and sensor readings for best results.

| Component         | ESP32 Pin |
|-------------------|-----------|
| Ultrasonic Echo   | 42        |
| Ultrasonic Trigger| 40        |
| LCD SDA           | 9         |
| LCD SCL           | 10        |
| Motor Relay       | 36        |
| Motor Light LED   | 38        |
| Motor Switch      | 15        |
| Water Overload    | 7         |
| LEDs (6x)         | 39, 37, 35, 21, 20, 19 |

---


## Getting Started


1. **Clone the Repository**

   ```sh
   git clone https://github.com/yourusername/water-level-indicator.git
   ```


2. **Configure WiFi and Telegram**

   Edit these lines in `Water_Level_Indicator.ino`:

   ```cpp
   #define WIFI_SSID "YourWiFiSSID"
   #define WIFI_PASSWORD "YourWiFiPassword"
   #define BOT_TOKEN "YourTelegramBotToken"
   #define ADMIN_CHAT_ID "YourTelegramChatID"
   ```

   - Get your Telegram Bot Token from [@BotFather](https://t.me/BotFather).
   - To find your Chat ID, message your bot `/my_id` and check the serial monitor.


3. **Install Required Libraries**

   - WiFi.h, WiFiClientSecure.h (ESP32 core)
   - UniversalTelegramBot
   - ArduinoJson
   - LiquidCrystal_I2C
   - Preferences.h (ESP32 core)

   Install via Arduino Library Manager or PlatformIO.


4. **Upload the Code**

   - Open `Water_Level_Indicator.ino` in Arduino IDE or VS Code with PlatformIO.
   - Select the correct ESP32 board and port.
   - Upload the code.

---


## Telegram Bot Commands

| Command             | Description                                      |
|---------------------|--------------------------------------------------|
| `/start`            | Show help and available commands                 |
| `/check`            | Get current water level and system status        |
| `/motor_on`         | Turn the motor ON (if conditions allow)          |
| `/motor_off`        | Turn the motor OFF (manual override)             |
| `/my_id`            | Get your Telegram Chat ID                        |
| `/add_user <id>`    | (Admin) Add a user to allowed list               |
| `/remove_user <id>` | (Admin) Remove a user from allowed list          |
| `/list_users`       | (Admin) List all allowed chat IDs                |

---


## System Logic

- **Automatic Motor Control:**
  - Motor turns ON automatically if water > 3000L and no manual override.
  - Motor turns OFF if water < 1000L (critical low).
  - Physical switch always has highest priority (OFF disables motor).
  - Manual override via Telegram `/motor_off` persists until `/motor_on` or switch toggled.
- **User Management:**
  - Only users in the allowed list (stored in NVS) can control the system.
  - Admin can add/remove users via Telegram commands.
- **Data Persistence:**
  - Allowed users and motor state are saved in ESP32 NVS and restored after reboot.

---


## Troubleshooting

- **WiFi Issues:**
  - The ESP32 will attempt to reconnect and reboot if WiFi is lost for 30 seconds.
- **Telegram Issues:**
  - Ensure correct Bot Token and Chat ID.
  - Check serial monitor for debug messages.
- **Sensor Issues:**
  - Check wiring and tank depth configuration.

---


## License

This project is open-source and available under the [MIT License](LICENSE).

---


## Author

- **CHETHAN N** ([CR7578](https://github.com/CR7578))

---


## Acknowledgements

- [Universal Arduino Telegram Bot](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot)
- [ArduinoJson](https://arduinojson.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
