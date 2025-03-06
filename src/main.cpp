#include <Arduino.h>
#include <Preferences.h>          // https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences
#include <AiEsp32RotaryEncoder.h> // https://github.com/igorantolic/ai-esp32-rotary-encoder
#include <LiquidCrystal_I2C.h>    // https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library
#include <WiFi.h>
#include <LittleFS.h>
#include <AsyncTCP.h>             // https://github.com/ESP32Async/AsyncTCP
#include <ESPAsyncWebServer.h>    // https://github.com/ESP32Async/ESPAsyncWebServer
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <MillisChronoTimer.h>
#include <VectorStats.h>
#include "settings.h"

int16_t temp_setting;
float current_temp;
bool heatState = LOW;
bool pumpState = HIGH;

Preferences userSettings;
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ENCODER_B_PIN, ENCODER_A_PIN, ENCODER_BUTTON_PIN, ENCODER_VCC_PIN, ENCODER_STEPS);
MillisChronoTimer lcdTimer(LCD_TIMEOUT);
MillisChronoTimer dataIntervalTimer(DATA_INTERVAL);  // Interval for collecting thermistor readings
MillisChronoTimer tempIntervalTimer(TEMP_INTERVAL);  // Interval for calculating temperature
MillisChronoTimer hotTimer(DRIFT_TIME * 60000);
MillisChronoTimer coldTimer(DRIFT_TIME * 60000);
MillisChronoTimer pumpTimer(CIRCULATION_TIME * 3600000);
MillisChronoTimer elementCooldownTimer(10 * 60000);  // 10 min heat element cooldown
LiquidCrystal_I2C lcd(0x27, 16, 2);
VectorStats<int16_t> dataBuffer(DATA_BUFFER_SIZE);
VectorStats<int16_t> medianBuffer(MEDIAN_BUFFER_SIZE);
AsyncWebServer server(80);  // port 80
AsyncWebSocket ws("/ws");
JsonDocument tubStateDoc;  // Json object to hold tub_state variables.
JsonDocument receivedJson; // holds received data from clients
JsonDocument tubDefaults;  // holds defaluts for max and min

void IRAM_ATTR readEncoderISR() {
    rotaryEncoder.readEncoder_ISR();
}

// " Temp: 101.3 °F "
// "  Set: 103 °F   "
void updateLCD() {
    lcd.setCursor(1, 0);
    lcd.print("Temp: ");
    lcd.print(current_temp, 1); // Rounds to 1 decimal.
    lcd.print(" ");
    lcd.print((char)223);
    lcd.print("F  ");
    
    lcd.setCursor(2, 1);
    lcd.print("Set: ");
    lcd.print(temp_setting);
    lcd.print(" ");
    lcd.print((char)223);
    lcd.print("F  ");
}

// Pushing Button activates/deactivates rotary encoder.
// LCD turns off via a timer.
void handleRotaryEncoder() {
    static bool rotary_engaged = false;
    static bool push_exit = false;
    if (rotaryEncoder.isEncoderButtonClicked()) {
        updateLCD();
        if (rotary_engaged) {
            userSettings.putShort("temp", temp_setting);  // minimize writes
            coldTimer.forceExpire();
            hotTimer.forceExpire();
            push_exit = true;
            rotary_engaged = false;
        } else {
            lcdTimer.reset();
            lcd.display();
            lcd.backlight();
            rotary_engaged = true;
        }
    }

    if (rotary_engaged) {
        if (rotaryEncoder.encoderChanged()) {
            lcdTimer.reset();
            temp_setting = rotaryEncoder.readEncoder();
            updateLCD();
        } else if (lcdTimer.expired()) {
            lcd.noDisplay();
            lcd.noBacklight();
            userSettings.putShort("temp", temp_setting);  // minimize writes
            coldTimer.forceExpire();
            hotTimer.forceExpire();
            rotary_engaged = false;
        }
    }

    if (push_exit && lcdTimer.expired()) {
        lcd.noDisplay();
        lcd.noBacklight();
        push_exit = false;
    }
}

void readThermistor() {
    if (dataBuffer.bufferFull()) {
        medianBuffer.add(dataBuffer.getMedian());  // getMedian() resets bufferFull() false.
        dataIntervalTimer.reset();
        
    } else if (dataIntervalTimer.expired()){
        dataBuffer.add(analogRead(THERMISTOR_INPUT_PIN));
    }
}

// Calculate temp using a 3rd order polynomial.
// Higher values use a different set of coefficients.
// TempF = Ax^3+Bx^2+Cx+D
float calculateTemp(float avgMedian) {
    float x = avgMedian;
    float tempF;
    if (x <= upper_cutoff) {
        tempF = A*pow(x, 3)+B*pow(x, 2)+C*x+D;
    } else {
        tempF = uA*pow(x, 3)+uB*pow(x, 2)+uC*x+uD;
    }
    return tempF;
}

void runThermostat() {
    static bool activeHot = false;
    static bool activeCold = false;
    static bool cooldown = false;
    static bool circPump = false;

    // Tub Too Cold
    if (current_temp > temp_setting - DRIFT_DEGREES) {
        coldTimer.reset();
    } else if (!heatState && coldTimer.expired()){
        pumpState = HIGH;
        heatState = HIGH;
        digitalWrite(PUMP_PIN, pumpState);
        digitalWrite(HEAT_PIN, heatState);
    }

    // Tub Too Hot
    if (current_temp < temp_setting + DRIFT_DEGREES && heatState) {
        hotTimer.reset();
    } else if (heatState && hotTimer.expired()) {
        elementCooldownTimer.reset();
        cooldown = true;
        heatState = LOW;
        digitalWrite(HEAT_PIN, heatState);
    } else if (cooldown && elementCooldownTimer.expired()) {
        cooldown = false;
        circPump = false;
        pumpTimer.reset();
        pumpState = LOW;
        digitalWrite(PUMP_PIN, pumpState);
    }

    // Run Circulation Pump
    if (pumpState && !circPump) {
        pumpTimer.reset();  // reset if pump on
    } else if (pumpTimer.expired() && !circPump) {
        circPump = true;
        pumpState = HIGH;
        digitalWrite(PUMP_PIN, pumpState);
    } else if (pumpTimer.elapsed() >= CIRCULATION_TIME * 2 && !heatState && !cooldown) {
        circPump = false;
        pumpState = LOW;
        digitalWrite(PUMP_PIN, pumpState);
        pumpTimer.reset();
    }
}

// This is faster than using: AsyncWebSocketMessageBuffer and makeBuffer()
// https://github.com/ESP32Async/ESPAsyncWebServer/wiki#asyncwebsocketmessagebuffer-and-makebuffer
void send(JsonDocument& doc) {
    char jsonbuffer[72]; // use multiples of 8
    size_t len = serializeJson(doc, jsonbuffer);
    ws.textAll(jsonbuffer, len);
    // jsonbuffer must be at least the size of length + 1
    if (DEBUG) {Serial.printf("Sending: %s    length: %d\n", jsonbuffer, len);}
}

void sendData() {
    tubStateDoc["type"] = "data";
    tubStateDoc["temp"] = (int)(current_temp * 10 + 0.5) / 10.0;  // round to 1 decimal place
    tubStateDoc["setTemp"] = temp_setting;
    if (pumpState) {
        tubStateDoc["pump"] = "ON";
    } else tubStateDoc["pump"] = "OFF";
    if (heatState) {
        tubStateDoc["heat"] = "ON";
    } else tubStateDoc["heat"] = "OFF";
    send(tubStateDoc);
}

void sendDefaults() {
    tubDefaults["type"] = "defaults";
    tubDefaults["max"] = MAX_TEMP;
    tubDefaults["min"] = MIN_TEMP;
    tubDefaults["setTemp"] = temp_setting;
    send(tubDefaults);
}

void handleWebSocketMessage(uint8_t *data) {
    deserializeJson(receivedJson, (char*)data);
    // If the key doesn’t exist c++ will set an int = 0
    int set_new = receivedJson["set_temp"];
    int refresh = receivedJson["refresh"];

    if (set_new) {
        temp_setting = set_new;
        userSettings.putShort("temp", temp_setting);
        coldTimer.forceExpire();  // Turn on heat immediately
        hotTimer.forceExpire();  // Turn off heat immediately
        runThermostat();
        sendData();
    }
    
    if (refresh) {
        sendDefaults();
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            if (DEBUG) {Serial.printf("Client: %s (CONNECTED)\n", client->remoteIP().toString());}
            sendDefaults();
            sendData();
        break;

        case WS_EVT_DISCONNECT:
           if (DEBUG) {Serial.println("Client: (DISCONNECT)");}
        break;

        case WS_EVT_DATA:
            AwsFrameInfo *info = (AwsFrameInfo *)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                data[len] = 0;
                if (DEBUG) {Serial.printf("Receive: %s    length: %d\n", (char*)data), len;}
                handleWebSocketMessage(data);
            }
        break;
    }
}

void setup() {
    if (DEBUG) {Serial.begin(115200); delay(1000);}

    // Set Heat and Pump States
    pinMode(HEAT_PIN, OUTPUT);
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(HEAT_PIN, LOW);
    digitalWrite(PUMP_PIN, HIGH);

    // Fill Buffers
    pinMode(THERMISTOR_INPUT_PIN, INPUT);
    dataBuffer.fillBuffer(analogRead(THERMISTOR_INPUT_PIN));
    medianBuffer.fillBuffer(dataBuffer.getMedian());
    current_temp = calculateTemp(medianBuffer.getAverage());

    // Initalize Non-volitile Storage
    userSettings.begin("hot-tub", false);
    temp_setting = userSettings.getShort("temp", DEFAULT_TEMP); // DEFAULT_TEMP if missing.

    // Initialize Rotary Encoder
    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);
    rotaryEncoder.setBoundaries(MIN_TEMP, MAX_TEMP, false); // minValue, maxValue, circleValues
    rotaryEncoder.setEncoderValue(temp_setting);

    // Initalize LCD
    lcd.init();
    lcd.clear();
    lcd.noCursor();
    lcd.noDisplay();
    lcd.noBacklight();

    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);
    if (DEBUG) {Serial.print("\nConnecting to WiFi ..");}
    while (WiFi.status() != WL_CONNECTED) {
        if (DEBUG) {Serial.print('.');}
        delay(1000);
    }
    if (DEBUG) {Serial.printf("\nServer IP: %s\n", WiFi.localIP().toString());}

    // Initalize LittleFS
    // https://forums.adafruit.com/viewtopic.php?t=203910
    // bool fs::LittleFSFS::begin(bool formatOnFail = false, const char *basePath = "/littlefs", uint8_t maxOpenFiles = (uint8_t)10U, const char *partitionLabel = "spiffs")
    // On Adafruit Metro ESP32-S3 the default partition label is "ffat" not "spiffs"
    if (!LittleFS.begin(false, "/littlefs", 10, "ffat")){
        if (DEBUG) {Serial.println("An Error occurred while mounting LittleFS");}
    } else {
        if (DEBUG) {Serial.println("LittleFS mounted successfully\n");}
    }

    // Initialize WebSocket:
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
    server.serveStatic("/", LittleFS, "/");  // .serveStatic(requestURL, FileSystem, DirectoryOnESP32)
    server.begin();
}

void loop() {
    handleRotaryEncoder();
    // Timer for collecting thermistor readings
    if (dataIntervalTimer.expired()) {
        readThermistor();
    }

    // Timer for calculating a temp for thermostat and server.send()
    if (tempIntervalTimer.expired()) {
        current_temp = calculateTemp(medianBuffer.getAverage());
        tempIntervalTimer.reset();
        runThermostat();
        if (DEBUG) {Serial.printf("Temp: %.2f    PumpState: %d    HeatState: %d\n", current_temp, pumpState, heatState);}
        // Only send to WebSocket when client count != 0
        if (ws.count()) {
            sendData();
            ws.cleanupClients(); // Cleanup disconnected clients or too many clients
        }
    }
}
