#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <unordered_map> // For calculateMode function
#include <ESP.h> // Required for ESP.restart()
#include <Preferences.h> // Required for Non-Volatile Storage (NVS)
#include <vector> // For storing allowed chat IDs
#include <sstream> // For parsing chat IDs from string (Added for convenience)

// --- WiFi Credentials ---
#define WIFI_SSID "WIFI_SSID" // Replace with your WiFi SSID
#define WIFI_PASSWORD "WIFI_PASSWORD" // Replace with your WiFi Password

// --- Telegram Bot Token ---
// !!! IMPORTANT: Replace with your actual bot token obtained from @BotFather !!!
#define BOT_TOKEN "Bot_Token" // Replace with your actual bot token

// --- Admin Chat ID ---
// This is the primary admin who can add/remove other users.
#define ADMIN_CHAT_ID "Admin_chat_ID" // Your specific admin chat ID

// --- Telegram Bot Setup ---
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
long lastTimeBotRan;
int botRequestDelay = 1000; // Delay between checking for new messages (milliseconds)
long lastUpdateID = 0; // Variable to store the last processed update ID

// --- LCD Setup ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // 16 columns and 2 rows
#define SDA_PIN 9 // Custom GPIO pin for SDA
#define SCL_PIN 10 // Custom GPIO pin for SCL

// --- Ultrasonic Sensor Pins ---
#define ECHOPIN 42 // Pin to receive echo pulse
#define TRIGPIN 40 // Pin to send trigger pulse

// --- Other Hardware Pins ---
const uint8_t led_pins[] = {39, 37, 35, 21, 20, 19}; // LED indicator pins
const uint8_t motor_relay = 36; // Relay pin
const uint8_t motor_light = 38; // Motor Light off pin
const uint8_t motor_switch = 15; // Motor Switch pin
const uint8_t water_overload = 7; // Water overload pin (Note: This pin isn't used in your current logic but is defined)

// --- Water Tank Parameters ---
int water_tank_depth = 306; // Depth of water tank in cm
int min_distance = 20; // Minimum distance sensor can read (full water) in cm
long full_water = 98000; // Full water capacity in liters

// --- Load Sizes ---
int big_load_size = 12000; // Big Load size in liters
int small_load_size = 6000; // Small Load size in liters

// --- Sensor Reading Variables ---
int readings[15]; // Array to store 10 ultrasonic readings for better accuracy
int previousMode = -1; // Initialize with a default invalid value for mode filter

// --- Display & Motor Control Variables ---
unsigned long previousMillis = 0; // Store last time the LCD display changed
const long interval = 2000; // Interval in milliseconds for LCD toggle (2 seconds)
bool displayFlag = false; // Toggle flag to switch LCD display content
bool motorStatus = false; // Motor starts off (false = OFF, true = ON)

// --- Manual Motor Override Variable ---
// This flag is true when the motor has been explicitly turned OFF by the physical switch or Telegram command.
// When true, the motor will stay OFF, overriding automatic control, until explicitly turned ON.
// This is primarily for the Telegram control to know if it's an override.
// The physical switch acts as a direct master enable/disable now.
bool manualMotorOverrideOff = false;

// --- ESP32 Reboot Timer ---
unsigned long lastRebootMillis = 0;
const long rebootInterval = 900 * 1000; // 15 minutes in milliseconds

// --- Global Variables for Water Level and Loads (to be accessed by Telegram) ---
long currentWaterLiters = 0;
long waterLitersAfterOffset = 0;
int currentDistanceCm = 0;
double currentSmallLoads = 0.0;
double currentBigLoads = 0.0;

// --- Variables for Motor Switch LCD Feedback ---
// This static variable helps prevent continuous LCD updates if the switch state doesn't change
static int lastKnownSwitchState = -1; // Initialize with an invalid state

// New enum for motor off reasons for LCD display and control logic
enum MotorOffReason {
  NONE,           // Motor is ON, or can be turned ON (no specific off reason applies)
  SWITCH_OFF,     // Motor is off because the physical switch is in the OFF position
  BOT_OFF,        // Motor was turned off by a Telegram /motor_off command
  LOW_WATER_OFF,  // Motor was automatically turned off due to critically low water level (below 1000L)
  // AUTO_OFF       // Motor is off because water level is not sufficient for auto-ON (between 1000L and 3000L)
};

MotorOffReason motorOffReason = NONE; // Global variable to track why motor is off

// --- NVS Preferences Object ---
Preferences preferences; // Create a Preferences object

// --- Allowed Chat IDs (managed by admin) ---
std::vector<String> allowedChatIDs;

// --- Function Prototypes ---
void handleNewMessages(int numNewMessages);
void motor_control(bool relay);
int calculateMode(int arr[], int size);
void blinkLED(int ledPin);
void readUltrasonicSensor(); // Function to encapsulate sensor reading and calculations
void saveAllowedChatIDs();
void loadAllowedChatIDs();
bool isChatIDAllowed(const String& chatID);

// --- Custom Functions (moved here to ensure definition before call in loop()) ---

// Function to save allowed chat IDs to NVS
void saveAllowedChatIDs() {
  String chatIDsString = "";
  for (size_t i = 0; i < allowedChatIDs.size(); ++i) {
    chatIDsString += allowedChatIDs[i];
    if (i < allowedChatIDs.size() - 1) {
      chatIDsString += ","; // Use a delimiter
    }
  }
  preferences.putString("allowedChatIDs", chatIDsString);
  Serial.println("INFO: Saved allowed chat IDs to NVS.");
}

// Function to load allowed chat IDs from NVS
void loadAllowedChatIDs() {
  String chatIDsString = preferences.getString("allowedChatIDs", ""); // Default to empty string
  allowedChatIDs.clear(); // Clear existing IDs

  if (chatIDsString.length() > 0) {
    std::stringstream ss(chatIDsString.c_str()); // Convert Arduino String to C-style string for std::stringstream
    std::string segment_std; // Declare a std::string to hold each segment
    while (std::getline(ss, segment_std, ',')) { // Use std::getline with std::string
      allowedChatIDs.push_back(String(segment_std.c_str())); // Convert std::string back to Arduino String
    }
  }
  Serial.println("INFO: Loaded allowed chat IDs from NVS.");
  // Print loaded IDs for debugging
  Serial.print("INFO: Currently allowed chat IDs: ");
  for (const String& id : allowedChatIDs) {
    Serial.print(id + " ");
  }
  Serial.println();
}

// Function to check if a chat ID is allowed
bool isChatIDAllowed(const String& chatID) {
  for (const String& allowedID : allowedChatIDs) {
    if (allowedID == chatID) {
      return true;
    }
  }
  return false;
}

// Function to handle new messages from Telegram
void handleNewMessages(int numNewMessages) {
  Serial.print("DEBUG: Handling ");
  Serial.print(numNewMessages);
  Serial.println(" new messages.");

  for (int i = 0; i < numNewMessages; i++) {
    String chatID = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;

    Serial.print("DEBUG: From: ");
    Serial.print(fromName);
    Serial.print(", Chat ID: ");
    Serial.print(chatID);
    Serial.print(", Text: ");
    Serial.println(text);

    // Check if the sender is allowed
    if (!isChatIDAllowed(chatID) && text != "/my_id") {
      bot.sendMessage(chatID, "Unauthorized access. Your Chat ID is not allowed.", "");
      Serial.print("DEBUG: Unauthorized access attempt from Chat ID: ");
      Serial.println(chatID);
      continue; // Skip processing for unauthorized users
    }

if (text == "/my_id") {
      Serial.println("DEBUG: /my_id command detected.");
      // The original response message string (commented out for this test)
      // String responseMessage = "Your Telegram Chat ID is: `" + chatID + "`\n(You can forward this to the admin to request access.)";
      // Serial.print("DEBUG: Prepared response message: '");
      // Serial.print(responseMessage);
      // Serial.println("'");
      
      Serial.print("DEBUG: Attempting to send SIMPLE plain text message to chat ID: "); // Updated debug message
      Serial.println(chatID);

      // --- CRITICAL TEST: Send a simple plain text message without MarkdownV2 ---
      // Comment out the original line and uncomment the line below:
      // bool sendSuccess = bot.sendMessage(chatID, responseMessage, "MarkdownV2"); // Original line
      bool sendSuccess = bot.sendMessage(chatID, chatID, ""); // <-- USE THIS LINE FOR THE TEST

      Serial.print("DEBUG: bot.sendMessage for /my_id (SIMPLE) result: ");
      Serial.println(sendSuccess ? "SUCCESS" : "FAIL");
      if (!sendSuccess) {
          Serial.println("ERROR: sendMessage failed for /my_id (SIMPLE). Possible network, Telegram API, or certificate issue.");
      }
      continue; // Processed this command, move to next message
    }

    if (text == "/start") {
      String welcome = "Welcome, " + fromName + ".\n";
      welcome += "Use these commands to control the motor and get water level info:\n";
      welcome += "/motor_on - Turn motor ON\n";
      welcome += "/motor_off - Turn motor OFF\n";
      welcome += "/check - Get current water level and motor status\n";
      if (chatID == ADMIN_CHAT_ID) { // Admin-only commands
        welcome += "/add_user <chat_id> - Add a new user to the allowed list\n";
        welcome += "/remove_user <chat_id> - Remove a user from the allowed list\n";
        welcome += "/list_users - List all allowed chat IDs\n";
      }
      bot.sendMessage(chatID, welcome, "");
    } else if (text == "/check") {
    // Provide current system status
    String response = "Distance: " + String(currentDistanceCm) + " cm\n\n";
    response += "Water Liters (Adjusted): " + String(waterLitersAfterOffset) + " L\n";
    response += "Water Liters (Raw): " + String(currentWaterLiters) + " L\n\n";
    response += "Approx. Small Tanker Loads: " + String(currentSmallLoads, 1) + " loads\n";
    response += "Approx. Big Tanker Loads: " + String(currentBigLoads, 1) + " loads\n\n";
    response += "Motor Status: " + String(motorStatus ? "ON" : "OFF") + "\n";
    response += "Manual Override Active: " + String(manualMotorOverrideOff ? "YES" : "NO") + "\n\n";

    // Determine motor off reason
    String motorOffReasonStr;
    switch (motorOffReason) {
        case NONE:
            motorOffReasonStr = "Motor is ON or can be turned ON (no specific off reason).";
            break;
        case SWITCH_OFF:
            motorOffReasonStr = "Motor is OFF because the physical switch is in the OFF position.";
            break;
        case BOT_OFF:
            motorOffReasonStr = "Motor is OFF due to Telegram /motor_off command.";
            break;
        case LOW_WATER_OFF:
            motorOffReasonStr = "Motor is OFF due to critically low water level (below 1000L).";
            break;
        // case AUTO_OFF:
        //    motorOffReasonStr = "Motor is OFF because the water level is not sufficient for auto-ON (between 1000L and 3000L).";
        //    break;
        default:
            motorOffReasonStr = "Unknown reason.";
            break;
    }
    response += "Motor Off Reason: " + motorOffReasonStr + "\n\n";
    
    // Uptime
    response += "Uptime: " + String(millis() / 1000 / 60) + " minutes."; // Uptime in minutes

    bool sendSuccess = bot.sendMessage(chatID, response, "");
    Serial.print("DEBUG: bot.sendMessage for /check result: ");
    Serial.println(sendSuccess ? "SUCCESS" : "FAIL");
} else if (text == "/motor_on") {
    // Check if motor can be turned ON
    if (motorOffReason == SWITCH_OFF || motorOffReason == LOW_WATER_OFF || currentWaterLiters < 3000) {
        String reasonStr;
        if (motorOffReason == SWITCH_OFF) reasonStr = "Physical Switch is OFF";
        if (motorOffReason == LOW_WATER_OFF) reasonStr = "Water Critically Low";
        if (currentWaterLiters < 3000) reasonStr = "Water Below 3000 ltrs";
        bool sendSuccess = bot.sendMessage(chatID, "Motor cannot be turned ON. " + reasonStr + ".", "");
        Serial.print("DEBUG: bot.sendMessage for /motor_on (rejected by reason) result: ");
        Serial.println(sendSuccess ? "SUCCESS" : "FAIL");
    } else {
        motor_control(true); // Turn the motor ON
        motorOffReason = NONE; // Clear any bot-off reason
        preferences.putInt("motorOffReason", NONE); // Save to NVS
        manualMotorOverrideOff = false; // Clear manual override
        preferences.putBool("manualOverride", false); // Save to NVS
        bool sendSuccess = bot.sendMessage(chatID, "Motor turned ON.", "");
        Serial.print("DEBUG: bot.sendMessage for /motor_on (success) result: ");
        Serial.println(sendSuccess ? "SUCCESS" : "FAIL");
    }
} else if (text == "/motor_off") {
    // Turn motor OFF via Telegram command
    manualMotorOverrideOff = true; // Activate manual override: motor will stay OFF
    preferences.putBool("manualOverride", true); // Save to NVS
    motor_control(false); // Turn the motor OFF
    motorOffReason = BOT_OFF; // Set reason for motor being off
    preferences.putInt("motorOffReason", BOT_OFF); // Save to NVS
    bool sendSuccess = bot.sendMessage(chatID, "Motor turned OFF.", "");
    Serial.print("DEBUG: bot.sendMessage for /motor_off (success) result: ");
    Serial.println(sendSuccess ? "SUCCESS" : "FAIL");
}

    // Admin commands
    else if (chatID == ADMIN_CHAT_ID) {
      if (text.startsWith("/add_user ")) {
        String newUserChatID = text.substring(10); // Extract chat ID
        newUserChatID.trim();
        if (newUserChatID.length() > 0 && !isChatIDAllowed(newUserChatID)) {
          allowedChatIDs.push_back(newUserChatID);
          saveAllowedChatIDs(); // Save changes to NVS
          bot.sendMessage(chatID, "User " + newUserChatID + " added to allowed list.", "");
          Serial.println("DEBUG: Added user: " + newUserChatID);
        } else if (isChatIDAllowed(newUserChatID)) {
          bot.sendMessage(chatID, "User " + newUserChatID + " is already in the allowed list.", "");
        } else {
          bot.sendMessage(chatID, "Invalid chat ID format for /add_user.", "");
        }
      } else if (text.startsWith("/remove_user ")) {
        String userToRemoveChatID = text.substring(13); // Extract chat ID
        userToRemoveChatID.trim();
        if (userToRemoveChatID.length() > 0 && userToRemoveChatID != ADMIN_CHAT_ID) { // Admin cannot remove self
          bool removed = false;
          for (size_t j = 0; j < allowedChatIDs.size(); ++j) {
            if (allowedChatIDs[j] == userToRemoveChatID) {
              allowedChatIDs.erase(allowedChatIDs.begin() + j);
              removed = true;
              break;
            }
          }
          if (removed) {
            saveAllowedChatIDs(); // Save changes to NVS
            bot.sendMessage(chatID, "User " + userToRemoveChatID + " removed from allowed list.", "");
            Serial.println("DEBUG: Removed user: " + userToRemoveChatID);
          } else {
            bot.sendMessage(chatID, "User " + userToRemoveChatID + " not found in allowed list.", "");
          }
        } else if (userToRemoveChatID == ADMIN_CHAT_ID) {
          bot.sendMessage(chatID, "You cannot remove the ADMIN_CHAT_ID.", "");
        } else {
          bot.sendMessage(chatID, "Invalid chat ID format for /remove_user.", "");
        }
      } else if (text == "/list_users") {
        String userList = "Allowed Chat IDs:\n";
        if (allowedChatIDs.empty()) {
          userList += "No users currently allowed (except admin).";
        } else {
          for (const String& id : allowedChatIDs) {
            userList += "- " + id + "\n";
          }
        }
        bot.sendMessage(chatID, userList, "");
      } else {
        bot.sendMessage(chatID, "Unknown command.", "");
      }
    } else {
      bot.sendMessage(chatID, "Unknown command or unauthorized.", "");
    }
  }
}

// Function to read ultrasonic sensor and update global water level variables
void readUltrasonicSensor() {
  // Take 15 readings and store them in the readings array for better accuracy
  for (int i = 0; i < 15; i++) {
    // Trigger the ultrasonic sensor to start measuring
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGPIN, LOW);

    // Read pulse duration from the echo pin
    long duration = pulseIn(ECHOPIN, HIGH, 50000); // Increased timeout for ~8m range (approx 50000 microseconds)
    int distance = duration / 58; // Convert pulse duration to distance in cm

    // distance = 296;
    // readings[i] = distance;

    // Apply a simple filter: if current reading is vastly different from previous mode,
    // use previous mode value to filter out spurious readings.
    if (previousMode == -1 || abs(distance - previousMode) <= 10) { // Within 10cm of previous mode
      readings[i] = distance;
    } else {
      readings[i] = previousMode; // Use the last stable mode to filter noise
    }
    delay(50); // Small delay between readings
  }

  // Apply mode calculation (most frequent value) for the actual distance
  int mode = calculateMode(readings, 15); // Using mode filter for better accuracy
  
  // Update the previousMode for the next loop iteration to be used in filtering
  previousMode = mode;

  // Validate the mode reading against tank parameters
  if (mode > water_tank_depth || mode < (min_distance - 5)) { // If reading is out of expected range
    // Do not update water level, retain previous values or handle as error.
    // For now, we'll just update the raw distance for debugging, but not process further.
    currentDistanceCm = mode;
    return; // Skip calculations if reading is invalid
  }

  // Update global variable for current distance (filtered)
  currentDistanceCm = mode;

  // Calculate water level in liters based on mode (most frequent reading)
  // map(value, fromLow, fromHigh, toLow, toHigh)
  // Distance range (min_distance to water_tank_depth) maps to Liters range (full_water to 0)
  currentWaterLiters = map(mode, min_distance, water_tank_depth, full_water, 0);

  // Apply minimum 1000 liters offset
  waterLitersAfterOffset = currentWaterLiters - 1000;
  // Ensure water_liters_after_offset stays within valid bounds (0 to full_water)
  if (waterLitersAfterOffset < 0) {
    waterLitersAfterOffset = 0;
  }
  if (waterLitersAfterOffset > full_water) {
    waterLitersAfterOffset = full_water;
  }

  // Calculate number of full Small loads (as float)
  currentSmallLoads = (double)waterLitersAfterOffset / small_load_size;
  // Calculate number of full Big loads (as float)
  currentBigLoads = (double)waterLitersAfterOffset / big_load_size;
}

// Function to control motor relay and light
void motor_control(bool relay) {
  // Only change motor state if the requested state is different from the current state
  if (relay != motorStatus) {
    motorStatus = relay; // Update the global motor status
    if (motorStatus) {
      digitalWrite(motor_relay, HIGH); // Turn motor ON (relay energizes)
      digitalWrite(motor_light, LOW); // Turn off motor light (indicates motor is running)
      Serial.println("MOTOR STATE CHANGE: Motor ON");
    } else {
      digitalWrite(motor_relay, LOW); // Turn motor OFF (relay de-energizes)
      digitalWrite(motor_light, HIGH); // Turn on motor light (indicates motor is off)
      Serial.println("MOTOR STATE CHANGE: Motor OFF");
    }
  }
}

// Function to calculate the mode (most frequent value) from an array
// Requires #include <unordered_map>
int calculateMode(int arr[], int size) {
  std::unordered_map<int, int> frequencyMap; // To store the frequency of each element
  if (size == 0) return 0; // Handle empty array case by returning 0

  // Populate the frequency map
  for (int i = 0; i < size; i++) {
    frequencyMap[arr[i]]++; // Increment the count for each element
  }

  int mode = arr[0]; // Initialize mode to the first element (fallback)
  int maxCount = 0; // Initialize the highest frequency count

  // Find the element with the highest frequency (the mode)
  for (auto& entry : frequencyMap) {
    if (entry.second > maxCount) {
      maxCount = entry.second;
      mode = entry.first;
    }
  }
  return mode;
}

// Function to control the LEDs to indicate water level (solid bar, no blinking)
void blinkLED(int ledPin) {
  // Find the index of the given ledPin in the led_pins array
  int targetIndex = -1;
  for (int i = 0; i < 6; i++) {
    if (led_pins[i] == ledPin) {
      targetIndex = i;
      break;
    }
  }
  if (targetIndex == -1) {
    Serial.print("Error: Invalid ledPin provided to blinkLED: ");
    Serial.println(ledPin);
    return; // Exit if the ledPin is not found
  }
  // Turn on LEDs up to the target index
  for (int i = 0; i <= targetIndex; i++) {
    digitalWrite(led_pins[i], HIGH);
  }
  // Turn off LEDs beyond the target index (if any)
  for (int i = targetIndex + 1; i < 6; i++) {
    digitalWrite(led_pins[i], LOW);
  }
}

void setup() {
  Serial.begin(9600); // Initialize serial communication at 115200 for faster output

  // --- Wi-Fi Connection ---
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(50);
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // --- LCD Initialization ---
  Wire.begin(SDA_PIN, SCL_PIN); // Initialize I2C communication with custom SDA and SCL pins
  lcd.begin(16, 2); // Initialize the LCD with 16 columns and 2 rows
  lcd.backlight(); // Turn on the LCD backlight
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("Connected to");
  lcd.setCursor(1,1);
  lcd.print(WiFi.localIP());
  delay(500); // Show IP for a few seconds

  // --- Ultrasonic Sensor Pins Initialization ---
  pinMode(ECHOPIN, INPUT);
  pinMode(TRIGPIN, OUTPUT);

  // --- LED Pins Initialization ---
  for (int i = 0; i < 6; i++) {
    pinMode(led_pins[i], OUTPUT);
  }

  // --- Motor Control Pins Initialization ---
  pinMode(motor_relay, OUTPUT);
  pinMode(motor_light, OUTPUT);
  pinMode(motor_switch, INPUT_PULLUP); // Use INPUT_PULLUP for the switch

  // --- Initialize NVS Preferences ---
  preferences.begin("motor_state", false); // "motor_state" is the namespace, false means not read-only

  // --- Restore Motor State from NVS ---
  int savedReasonInt = preferences.getInt("motorOffReason", NONE); // Default to NONE if not found
  bool savedManualOverride = preferences.getBool("manualOverride", false); // Default to false
  motorOffReason = static_cast<MotorOffReason>(savedReasonInt);
  manualMotorOverrideOff = savedManualOverride;
  Serial.print("Restored motor state from NVS: motorOffReason=");
  Serial.print(savedReasonInt);
  Serial.print(", manualManualOverrideOff=");
  Serial.println(savedManualOverride ? "true" : "false");

  // --- Load Allowed Chat IDs from NVS ---
  loadAllowedChatIDs();

  // Ensure ADMIN_CHAT_ID is always in the allowed list
  if (!isChatIDAllowed(ADMIN_CHAT_ID)) {
    allowedChatIDs.push_back(ADMIN_CHAT_ID);
    saveAllowedChatIDs();
    Serial.println("Admin chat ID added to allowed list.");
  }

  // --- Perform an initial sensor read to determine current water level ---
  readUltrasonicSensor(); // This updates currentWaterLiters and other related variables

  // Determine initial motorStatus based on restored NVS state AND current water level/switch
  if (motorOffReason == BOT_OFF || motorOffReason == SWITCH_OFF) {
    // If a manual override (bot or switch) was active, motor must be OFF.
    motor_control(false);
    Serial.println("Motor initialized to OFF due to restored manual override.");
  } else {
    // No manual override was active before reboot. Determine initial state based on current conditions.
    // Read the current state of the physical switch
    int currentSwitchStateAtBoot = digitalRead(motor_switch);

    if (currentWaterLiters < 1000) { // Critically low water
      motor_control(false);
      motorOffReason = LOW_WATER_OFF;
      preferences.putInt("motorOffReason", LOW_WATER_OFF); // Update NVS in case of reboot
      manualMotorOverrideOff = false; // Ensure override is false as it's not a *manual* override from NVS
      preferences.putBool("manualOverride", false);
      Serial.println("Motor initialized to OFF due to current critically low water level.");
    } else if (currentSwitchStateAtBoot == LOW) { // Physical switch is OFF (LOW with INPUT_PULLUP)
      motor_control(false);
      motorOffReason = SWITCH_OFF;
      preferences.putInt("motorOffReason", SWITCH_OFF); // Update NVS
      manualMotorOverrideOff = true; // Ensure override is true as physical switch is master
      preferences.putBool("manualOverride", true);
      Serial.println("Motor initialized to OFF because physical switch is currently OFF.");
    } else if (currentWaterLiters > 3000 /* && currentSwitchStateAtBoot == HIGH */ ) { // Water sufficient AND switch ON
      motor_control(true);
      motorOffReason = NONE; // No off reason, motor is ON
      preferences.putInt("motorOffReason", NONE);
      manualMotorOverrideOff = false;
      preferences.putBool("manualOverride", false);
      Serial.println("Motor initialized to ON due to current natural conditions (water sufficient and switch ON).");
    }
  }
  
  // Reset the reboot timer at the start
  lastRebootMillis = millis();
}

void loop() {
  unsigned long currentMillis = millis(); // Get the current time

  // --- ESP32 Reboot Logic ---
  if (currentMillis - lastRebootMillis >= rebootInterval) {
    lastRebootMillis = currentMillis; // Reset the timer
    Serial.println("Reboot interval passed. Rebooting ESP32...");
    delay(100); // Small delay to allow serial message to send
    ESP.restart(); // Perform the reboot
  }

  // --- Read Ultrasonic Sensor and Calculate Water Levels ---
  readUltrasonicSensor(); // This function now updates currentDistanceCm, currentWaterLiters, etc.

  // --- Motor Light Status Update (based on `motorStatus` global variable) ---
  if (motorStatus == false) { // If motor is off
    digitalWrite(motor_light, HIGH); // Turn on motor light
  } else { // If motor on
    digitalWrite(motor_light, LOW); // Turn OFF motor light
  }

  // --- Manual Motor Switch Control (On/Off Switch behavior) ---
  // This block directly reflects the state of the physical ON/OFF switch.
  int currentMotorSwitchState = digitalRead(motor_switch);
  // Check if the physical switch state has changed to prevent continuous updates and LCD flicker
  if (currentMotorSwitchState != lastKnownSwitchState) {
    lastKnownSwitchState = currentMotorSwitchState; // Update last known state

    // --- Inverted Logic: LOW means OFF, HIGH means ON ---
    if (currentMotorSwitchState == LOW) { // Physical switch is in the OFF position
      manualMotorOverrideOff = true; // Set manual override
      motorOffReason = SWITCH_OFF; // Set reason for motor being off
      preferences.putInt("motorOffReason", SWITCH_OFF); // Save to NVS
      preferences.putBool("manualOverride", true);        // Save to NVS
      Serial.println("Physical switch moved to OFF. Motor forced OFF by switch.");
      motor_control(false); // Force off immediately by the physical switch
    } else { // Physical switch is in the ON position
      // If the motor was previously off due to the switch, then clearing override
      if (motorOffReason == SWITCH_OFF) {
        manualMotorOverrideOff = false; // Clear manual override from switch
        motorOffReason = NONE; // No longer off due to switch, allowing auto-on or bot control
        preferences.putInt("motorOffReason", NONE); // Save to NVS
        preferences.putBool("manualOverride", false);      // Save to NVS
        Serial.println("Physical switch moved to ON. Manual override from switch cleared.");
      } else {
        // If it was off for other reasons (bot_off, auto_off, low_water_off),
        // just ensure manual override is cleared, but don't change motorOffReason unless it was SWITCH_OFF
        manualMotorOverrideOff = false;
        preferences.putBool("manualOverride", false); // Save to NVS even if reason unchanged
        Serial.println("Physical switch is ON. Manual override cleared (not by switch).");
      }
    }
  }

  // --- Prioritized Motor Control Logic (Highest to Lowest) ---
  // Rule 1: Highest priority - Water critically low (always turns motor off, resets all overrides)
  if (currentWaterLiters < 1000) {
    if (motorStatus == true) {
      motor_control(false);
    }
    motorOffReason = LOW_WATER_OFF;
    preferences.putInt("motorOffReason", LOW_WATER_OFF); // Save to NVS
    manualMotorOverrideOff = false; // Critically low water level overrides any other manual override
    preferences.putBool("manualOverride", false); // Save to NVS
    Serial.println("Motor forced OFF: Water critically low (<1000L).");
  }
  // Rule 2: Physical switch is OFF (master override over bot/auto)
  else if (digitalRead(motor_switch) == LOW) { // Changed for inverted logic: LOW means OFF
    if (motorStatus == true) {
      motor_control(false);
    }
    motorOffReason = SWITCH_OFF; // Ensure reason is set for LCD
    preferences.putInt("motorOffReason", SWITCH_OFF); // Save to NVS
    manualMotorOverrideOff = true; // Ensure bot can't turn it on
    preferences.putBool("manualOverride", true); // Save to NVS
    Serial.println("Motor kept OFF: Physical switch is in OFF position.");
  }
  // Rule 3: Bot manually turned it OFF (override over auto, if switch is ON)
  else if (motorOffReason == BOT_OFF) { // Check if bot specifically turned it off
    if (motorStatus == true) {
      motor_control(false);
    }
    manualMotorOverrideOff = true; // Keep override active
    preferences.putBool("manualOverride", true); // Save to NVS (reason already BOT_OFF)
    Serial.println("Motor kept OFF: Manual override from Telegram is active (BOT_OFF).");
  }
  // NEW Rule for Hysteresis: If motor is currently ON and water drops between 1000L and 3000L, keep it ON.
  // else if (motorStatus == true && currentWaterLiters >= 1000 && currentWaterLiters < 3000) {
  //   // Motor is already ON, water is in the hysteresis range, keep it ON.
  //   motorOffReason = NONE; // No off reason as it's intentionally ON
  //   preferences.putInt("motorOffReason", NONE); // Save to NVS
  //   manualMotorOverrideOff = false; // Not a manual override OFF
  //   preferences.putBool("manualOverride", false); // Save to NVS
  //   Serial.println("Motor kept ON: Water level in hysteresis range (1000L-3000L) and already ON.");
  // }
  // Rule 4: Conditions to turn motor ON (water sufficient, and no specific OFF reason/override)
  else if (currentWaterLiters > 3000) {
    // If we reach here, it means:
    // - Water is NOT critically low.
    // - Physical switch is NOT OFF.
    // - Bot did NOT turn it off (or bot turned it on or cleared its override).
    motor_control(true);
    motorOffReason = NONE; // Motor is on, so no off reason
    preferences.putInt("motorOffReason", NONE); // Save to NVS
    manualMotorOverrideOff = false; // No override
    preferences.putBool("manualOverride", false); // Save to NVS
    //Serial.println("Motor ON: Water sufficient (>3000L) and no specific manual override.");
  }

  // --- LCD Display Logic ---
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis; // Save the last time the display was updated
    lcd.clear(); // Clear the LCD before updating it

    // If motor is ON, or if it's OFF due to water level (LOW_WATER_OFF, AUTO_OFF), toggle display.
    if (motorStatus || motorOffReason == LOW_WATER_OFF || motorOffReason == NONE /*|| motorOffReason == AUTO_OFF*/) {
      displayFlag = !displayFlag; // Toggle the display flag
      if (displayFlag) {
        // Screen 1: Water available (in cm and liters)
        lcd.setCursor(1, 0);
        lcd.print("Water: ");
        lcd.print(currentDistanceCm);
        lcd.print(" cm");
        lcd.setCursor(1, 1);
        lcd.print(waterLitersAfterOffset); // Display water with offset
        lcd.print(" Liters");
      } else {
        // Screen 2: Small and Big load information
        lcd.setCursor(1, 0);
        lcd.print(" 6K:");
        lcd.print(currentSmallLoads, 1); // Display with 1 decimal place
        lcd.print(" Loads");
        lcd.setCursor(1, 1);
        lcd.print("12K:");
        lcd.print(currentBigLoads, 1); // Display with 1 decimal place
        lcd.print(" Loads");
      }
    } else { // Motor is OFF due to explicit switch or bot command (SWITCH_OFF or BOT_OFF)
      lcd.setCursor(1, 0); // Start from column 0 for "Motor OFF"
      lcd.print("Motor: OFF");
      lcd.setCursor(1, 1); // Start from column 0 for reason
      switch (motorOffReason) {
        case SWITCH_OFF:
          lcd.print("(By Switch)");
          break;
        case BOT_OFF:
          lcd.print("(By Bot)");
          break;
        case NONE: // Fallback, should not happen if motorStatus is false here and not AUTO_OFF/LOW_WATER_OFF
        default:
          lcd.print("(Unknown Off)"); // Fallback for unexpected states
          break;
      }
      // If motor is off due to switch or bot, force displayFlag to false to ensure the next toggle (if motor turns on)
      // starts with the water level screen.
      displayFlag = false;
    }
  }

  // --- Serial Monitor Debugging Output ---
  Serial.print("Dist: ");
  Serial.print(currentDistanceCm);
  Serial.print(" cm, Raw Water: ");
  Serial.print(currentWaterLiters);
  Serial.print(" Liters, Adj Water: ");
  Serial.print(waterLitersAfterOffset);
  Serial.print(" Liters, Big Loads: ");
  Serial.print(currentBigLoads, 1);
  Serial.print(", Small Loads: ");
  Serial.print(currentSmallLoads, 1);
  Serial.print(", Motor: ");
  Serial.print(motorStatus ? "ON" : "OFF");
  Serial.print(", Override (manual): ");
  Serial.print(manualMotorOverrideOff ? "ACTIVE" : "INACTIVE");
  Serial.print(", Reason: ");
  switch (motorOffReason) {
    case NONE: Serial.println("NONE (Motor ON or ready)"); break;
    case SWITCH_OFF: Serial.println("SWITCH_OFF"); break;
    case BOT_OFF: Serial.println("BOT_OFF"); break;
    case LOW_WATER_OFF: Serial.println("LOW_WATER_OFF"); break;
    // case AUTO_OFF: Serial.println("AUTO_OFF"); break;
    default: Serial.println("UNKNOWN"); break;
  }
  // Print the stored readings array to the serial monitor
  Serial.print("Stored Readings (Mode Filter): ");
  for (int i = 0; i < 15; i++) {
    Serial.print(readings[i]);
    Serial.print(" ");
  }
  Serial.println(); // Print a new line after the readings

  // --- LED Blinking Logic ---
  // Turn off all LEDs initially before determining which to blink
  for (int i = 0; i < 6; i++) {
    digitalWrite(led_pins[i], LOW);
  }
  // Blink LEDs based on water level (using water_liters_after_offset for display consistency)
  if (waterLitersAfterOffset < 6000) {
    blinkLED(39); // Red LED (Very Low)
  } else if (waterLitersAfterOffset >= 6000 && waterLitersAfterOffset < 12000) {
    blinkLED(37); // Yellow LED (Low)
  } else if (waterLitersAfterOffset >= 12000 && waterLitersAfterOffset < 48000) {
    blinkLED(35); // Blue1 LED (Medium)
  } else if (waterLitersAfterOffset >= 48000 && waterLitersAfterOffset < 72000) {
    blinkLED(21); // Blue2 LED (Good)
  } else if (waterLitersAfterOffset >= 72000 && waterLitersAfterOffset < 90000) {
    blinkLED(20); // Green1 LED (High)
  } else if (waterLitersAfterOffset >= 90000) {
    blinkLED(19); // Green2 LED (Full)
  }

  // --- Telegram Bot Loop ---
  unsigned long lastReconnectAttempt = 0;
  const unsigned long reconnectTimeout = 30000; // Timeout for 30 seconds

  if (WiFi.status() == WL_CONNECTED) { // Only check for bot messages if connected to WiFi
      if (currentMillis > lastTimeBotRan + botRequestDelay) {
        Serial.println("INFO: Attempting to get new messages from Telegram..."); // Added diagnostic
        int numNewMessages = bot.getUpdates(lastUpdateID + 1);
        Serial.print("INFO: Received "); // Added diagnostic
        Serial.print(numNewMessages); // Added diagnostic
        Serial.println(" new messages from getUpdates."); // Added diagnostic
        while (numNewMessages) {
          Serial.println("INFO: Processing new messages from Telegram."); // Added diagnostic
          handleNewMessages(numNewMessages);
          lastUpdateID = bot.messages[numNewMessages - 1].update_id;
          numNewMessages = bot.getUpdates(lastUpdateID + 1);
        }
        lastTimeBotRan = currentMillis;
      }
  } else {
      if (currentMillis - lastReconnectAttempt < reconnectTimeout) {
          // Wi-Fi is not connected, try reconnecting
          Serial.println("WARNING: WiFi not connected. Attempting to reconnect...");
          WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Attempt to reconnect
          delay(1000); // Wait for 1 second before the next attempt
          lastReconnectAttempt = currentMillis;
      } else {
          // If Wi-Fi is still not connected after 30 seconds, restart the board
          Serial.println("ERROR: WiFi failed to connect after 30 seconds. Restarting...");
          delay(100); // Small delay to allow serial message to send
          ESP.restart();
      }
  }
}