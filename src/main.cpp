#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
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

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32;

#define DISPLAY_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Noiasca_ht16k33_hw_14 ht16k33 = Noiasca_ht16k33_hw_14();
const uint8_t i2cAddress = 0x70;                                     // the I2C address of the first module
const uint8_t numOfDevices = 1;                                      // how many modules have you installed on the I2C Bus
const uint16_t wait = 400;   // wait milliseconds between demos

const int port = 80; // Web server port
const char *apSSID = "ESP32-AP"; // AP SSID
const char *apPassword = "password"; // AP Password
WebServer server(port); // Web server instance
DNSServer dnsServer; // DNS server instance
String newSSID = "";
String newPassword = "";
void handleConfigSubmit();
void startAP();
void connectToWiFi();
void handleConfig();
void handleConfigSubmit();

void doorOpen() {
  ht16k33.clear();
  ht16k33.print(F("OPEN"));
  if (prefs.getBool("locked")) 
  {
    Serial.println("Unlocking Door");
    digitalWrite(RELAY_PIN, LOW);
    delay(100); 
    digitalWrite(RELAY_PIN, HIGH);
    prefs.putBool("locked", false);
    Serial.println("Door Open");
  }
}

void doorClose() {
  ht16k33.clear();
  ht16k33.print(F("LOCK"));
  if (!prefs.getBool("locked")){
    Serial.println("Locking Door");
    digitalWrite(RELAY_PIN, LOW);
    delay(100); 
    digitalWrite(RELAY_PIN, HIGH);
    prefs.putBool("locked", true);
    Serial.println("Door Locked");
  }
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
    doorOpen();
  } else {
    doorClose();
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

// Function to handle the configuration page
void handleConfig() {
  String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
      <title>WiFi Configuration</title>
    </head>
    <body>
      <h1>WiFi Configuration</h1>
      <form action="/config" method="POST">
        <label for="ssid">SSID:</label><br>
        <input type="text" id="ssid" name="ssid"><br><br>
        <label for="password">Password:</label><br>
        <input type="password" id="password" name="password"><br><br>
        <input type="submit" value="Submit">
      </form>
    </body>
    </html>
  )";
  server.send(200, "text/html", html);
}

// Function to start the Access Point
void startAP() {
  WiFi.mode(WIFI_MODE_NULL);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPassword);
  Serial.println("Access Point started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Setup DNS Server for Captive Portal
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Setup Web Server routes
  server.on("/", handleConfig);
  server.on("/config", HTTP_POST, handleConfigSubmit);
  server.begin();
}

// Function to connect to the WiFi network
void connectToWiFi() {
  WiFi.begin(newSSID.c_str(), newPassword.c_str());
  Serial.println("newssid=" + newSSID);
  Serial.println("newpassword=" + newPassword);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    prefs.putString("ssid", newSSID);
    prefs.putString("password", newPassword);
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed. Restarting AP mode.");
    startAP();
  }
}

void handleConfigSubmit() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    newSSID = server.arg("ssid");
    newPassword = server.arg("password");
    server.send(200, "text/plain", "WiFi credentials received. ESP will attempt to connect...");
 // Function to handle the form submission
    delay(1000);
    WiFi.softAPdisconnect(true); // Disable the AP
    WiFi.mode(WIFI_MODE_NULL);
    WiFi.mode(WIFI_STA); // Switch to STA mode
    connectToWiFi();
  } else {
    server.send(400, "text/plain", "Missing SSID or password");
  }
}

void setup(void) 
{
  Serial.begin(9600);
  delay(5000);
  Serial.println("Hello!");

  prefs.getString("ssid", newSSID);
  prefs.getString("password", newPassword);
  connectToWiFi();
  while (newSSID == "")
  {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(SDA, OUTPUT);
  pinMode(SCL, OUTPUT);
  Wire.begin(SDA, SCL);

  Serial.println("Display begin");
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    while(1); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setCursor(0, 0); //oled display
  display.setTextSize(1);
  display.setTextColor(WHITE);

  Serial.println("ht16k33 begin");
  ht16k33.begin(i2cAddress, numOfDevices);
  ht16k33.clear();
  prefs.putBool("locked", true);
  doorOpen();

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

  display.display();
}

void loop(void) 
{
  noInterrupts();
  wiegand.flush();
  interrupts();

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