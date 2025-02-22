#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <Adafruit_SSD1306.h>
#include <NoiascaHt16k33.h>
#include <ezBuzzer.h>
#include <ezButton.h>
#include <Preferences.h>
#include <Wiegand.h>

const int RELAY_PIN = D2;
const int BUZZER_PIN = D6;
const int BUTTON_PIN = D1;
const int PIN_D0 = D7;
const int PIN_D1 = D8;
const int PIN_OPEN = D10;

Preferences prefs;

ezBuzzer buzzer(BUZZER_PIN);
ezButton button(BUTTON_PIN,EXTERNAL_PULLDOWN);

Wiegand wiegand;
bool triggered = false;
char card_data[10];
uint8_t card_data_bits = 0;
bool card_read = false;

bool locked = false;

bool open_door = false;
bool close_door = false;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32;

#define DISPLAY_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Noiasca_ht16k33_hw_14 ht16k33 = Noiasca_ht16k33_hw_14();
const uint8_t i2cAddress = 0x70;                                     // the I2C address of the first module
const uint8_t numOfDevices = 1;                                      // how many modules have you installed on the I2C Bus
const uint16_t wait 400;   // wait milliseconds between demos

const char* ssid = "giordano"; // Your desired Access Point name
const char* password = "12345678"; // Password for the Access Point
const int port = 80; // Web server port

WebServer server(port);
const char* ap_ssid = "ESP32-Setup";  // AP mode SSID
const char* ap_password = "12345678";  // AP mode password

// HTML form
const char* html_form = R"(
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Setup</title>
    <style>
        body { font-family: Arial; margin: 20px; }
        .form-group { margin-bottom: 15px; }
        input { padding: 5px; width: 200px; }
        button { padding: 5px 15px; }
    </style>
</head>
<body>
    <h2>WiFi Configuration</h2>
    <form action="/save" method="POST">
        <div class="form-group">
            <label>SSID:</label><br>
            <input type="text" name="ssid" required>
        </div>
        <div class="form-group">
            <label>Password:</label><br>
            <input type="password" name="password" required>
        </div>
        <button type="submit">Connect</button>
    </form>
</body>
</html>
)";

void handleSave();
void handleRoot();

void doorOpen() {
  ht16k33.clear();
  ht16k33.print(F("OPEN"));
  Serial.println("Door Open");
  if (prefs.getBool("locked")) 
  {
    Serial.println("Unlocking Door");
    digitalWrite(RELAY_PIN, LOW);
    delay(100); 
    digitalWrite(RELAY_PIN, HIGH);
  }
  prefs.putBool("locked", false);
}
void doorClose() {
  ht16k33.clear();
  ht16k33.print(F("LOCK"));
  Serial.println("Door Locked");
  if (!prefs.getBool("locked")) 
  {
    Serial.println("Locking Door");
    digitalWrite(RELAY_PIN, LOW);
    delay(100);
    digitalWrite(RELAY_PIN, HIGH);
  }
  prefs.putBool("locked", true);
}

// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
  wiegand.setPin0State(digitalRead(PIN_D0));
  wiegand.setPin1State(digitalRead(PIN_D1));
}

void keypad_triggered() {
  //Serial.println("keypad interrupt triggered");
  triggered = true;
}

void doorToggle() {
  Serial.println("doorToggle");
  int c = digitalRead(BUTTON_PIN);
  if (prefs.getBool("locked")) {
    triggered = true;
    close_door = false;
    open_door = true;
  } else {
    triggered = true;
    close_door = true;
    open_door = false;
  }
}
// Notifies when a reader has been connected or disconnected.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onStateChange()`
void stateChanged(bool plugged, const char* message) {
    Serial.print(message);
    Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
}

// Notifies when a card was read.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onReceive()`
void receivedData(uint8_t* data, uint8_t bits, const char* message) {
    Serial.print(message);
    Serial.print(bits);
    Serial.print("bits / ");
    card_data_bits = bits;
    //Print value in HEX
    uint8_t bytes = (bits+7)/8;
    for (int i=0; i<bytes; i++) {
        Serial.print(data[i] >> 4, 16);
        Serial.print(data[i] & 0xF, 16);
        card_data[i] = data[i];
    }
    Serial.println();
    card_read = true;
}

// Notifies when an invalid transmission is detected
void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message) {
    Serial.print(message);
    Serial.print(Wiegand::DataErrorStr(error));
    Serial.print(" - Raw data: ");
    Serial.print(rawBits);
    Serial.print("bits / ");
    card_data_bits = rawBits;

    //Print value in HEX
    uint8_t bytes = (rawBits+7)/8;
    for (int i=0; i<bytes; i++) {
        Serial.print(rawData[i] >> 4, 16);
        Serial.print(rawData[i] & 0xF, 16);
        card_data[i] = rawData[i];
    }
    Serial.println();
}

void handleRoot() {
  server.send(200, "text/html", html_form);
}

void startAPMode() {
  // Start AP mode
  WiFi.softAP(ap_ssid, ap_password);
  
  Serial.println("AP Mode Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  
  server.begin();
  Serial.println("HTTP server started");
}

void handleSave() {
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");

  // Save to preferences
  prefs.putString("ssid", new_ssid);
  prefs.putString("password", new_password);
  // Send response
  server.send(200, "text/html", "Saved. ESP32 will now connect to the new network...<br>Please connect your device to the same network.");

  // Wait a bit before trying to connect
  delay(1000);

  // Connect to the new network
  WiFi.softAPdisconnect(true);
  WiFi.begin(new_ssid.c_str(), new_password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to new WiFi");
    Serial.println("IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect. Restarting AP mode...");
    startAPMode();
  }
}

void getWifiConnection() {
  // Initialize preferences
  prefs.begin("wifi-config", false);

  // Check if we have saved credentials
  String saved_ssid = prefs.getString("ssid", "");
  String saved_password = prefs.getString("password", "");

  if (saved_ssid.length() > 0) {
    // Try to connect with saved credentials
    WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi");
      Serial.println("IP: " + WiFi.localIP().toString());
      return;
    }
  }

  // If no saved credentials or connection failed, start AP mode
  startAPMode();
}

void setup(void) 
{
  Serial.begin(9600);
  delay(5000);
  Serial.println("Hello!");
  prefs.begin("backdoor", false);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(SDA, OUTPUT);
  pinMode(SCL, OUTPUT);
  Wire.begin(SDA, SCL);
  Serial.println("ht16k33 begin");
  ht16k33.begin(i2cAddress, numOfDevices);
  ht16k33.clear();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    while(1); // Don't proceed, loop forever
  }
  Serial.println("Display begin");
  display.clearDisplay();
  display.setCursor(0, 0); //oled display
  display.setTextSize(1);
  display.setTextColor(WHITE);

  getWifiConnection();
  server.handleClient();
  Serial.println(WiFi.localIP());

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  display.setCursor(0, 0); //oled display
  display.print(WiFi.localIP());

   //Install listeners and initialize Wiegand reader
  wiegand.onReceive(receivedData, "Card read: ");
  wiegand.onReceiveError(receivedDataError, "Card read error: ");
  wiegand.onStateChange(stateChanged, "State changed: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);

  //initialize pins as INPUT and attaches interruptions
  pinMode(PIN_D0, INPUT);
  pinMode(PIN_D1, INPUT);
  pinMode(PIN_OPEN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_OPEN), keypad_triggered, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_D0), pinStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_D1), pinStateChanged, CHANGE);
  //Sends the initial pin state to the Wiegand library
  pinStateChanged();

  display.setCursor(0, 12);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.print("Waiting for input");
  display.setCursor(0, 55);
  locked = prefs.getBool("locked", false); 
  if (locked)
  {
    Serial.println("Door is locked");
    doorClose(); 
  }
  else
  {
    Serial.println("Door is unlocked");
    doorOpen(); 
  }

  display.display();
}

void loop(void) 
{
  noInterrupts();
  wiegand.flush();
  interrupts();

  if (open_door)
  {
    doorOpen();
    open_door = false;
  }
  if (close_door)
  {
    doorClose();
    close_door = false;
  }
    
  ArduinoOTA.handle();
 
  if (card_read) 
  {
      Serial.println("Card read");
      display.clearDisplay();
      display.setCursor(0, 12); //oled display
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.print("UID Length:");
      display.print(card_data_bits, DEC);
      display.println(" bits");
      display.setCursor(0, 24); //oled display
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.println("UID Value: ");
      display.setCursor(5, 36); //oled display
 
      uint8_t bytes = (card_data_bits+7)/8;
      for (uint8_t i=0; i < bytes; i++) 
      {
        display.print(" 0x");
        display.print(card_data[i] >> 4, 16);
        display.print(card_data[i] & 0xF, 16);
      }
      if (triggered)
      {
        Serial.println(" Valid Card");
        display.setCursor(0, 48); //oled display
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.print("Valid Card");
        display.display();
        doorOpen();
        triggered = false;
      }
      else{
        Serial.println(" Invalid Card");
        display.setCursor(0, 48); //oled display
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.print("Invalid Card");
        display.display();
        doorClose();
        triggered = false;
      }
      card_read = false;
  }
  else if (false)
  {
    display.clearDisplay();
    display.setCursor(0, 48); //oled display
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.print("keycode triggered");
    display.display();
    doorOpen();
    triggered = false;
  }

  button.loop();
  if (button.isPressed())
  {
    Serial.println("Button Pressed");
    doorToggle();
  }

  // Wait 100 msecs before continuing
  delay(100);
}