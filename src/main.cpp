#include <Arduino.h>
#include <esp_task_wdt.h>
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
#include <NeoPixel.h>
#include "settings.h"


int16_t temp_setting;
int16_t light_setting = 0;
float current_temp;
bool heatState = LOW;


Preferences userSettings;
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ENCODER_B_PIN, ENCODER_A_PIN, ENCODER_BUTTON_PIN, ENCODER_VCC_PIN, ENCODER_STEPS);
MillisChronoTimer lcdTimer(LCD_TIMEOUT);
MillisChronoTimer dataIntervalTimer(DATA_INTERVAL);  // Interval for collecting thermistor readings
MillisChronoTimer tempIntervalTimer(TEMP_INTERVAL);  // Interval for calculating temperature
MillisChronoTimer lightTimer(LIGHT_TIMER * 60000);   // Time to run light
LiquidCrystal_I2C lcd(0x27, 16, 2);
VectorStats<int16_t> dataBuffer(DATA_BUFFER_SIZE);
VectorStats<int16_t> medianBuffer(MEDIAN_BUFFER_SIZE);
AsyncWebServer server(80);  // port 80
AsyncWebSocket ws("/ws");
JsonDocument tubStateDoc;  // Json object to hold tub_state variables.
JsonDocument receivedJson; // holds received data from clients
JsonDocument tubDefaults;  // holds defaluts for max and min
NeoPixel neoPixel(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);


void IRAM_ATTR readEncoderISR() {
    rotaryEncoder.readEncoder_ISR();
}


void updateLCD(int16_t set_temp = temp_setting) {
    lcd.setCursor(1, 0);
    lcd.print("Temp: ");
    lcd.print(current_temp, 1); // Rounds to 1 decimal.
    lcd.print(" ");
    lcd.print((char)223);
    lcd.print("F  ");
    
    lcd.setCursor(2, 1);
    lcd.print("Set: ");
    lcd.print(set_temp);
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
        if (rotary_engaged) {
            push_exit = true;
            rotary_engaged = false;
            temp_setting = rotaryEncoder.readEncoder();
            userSettings.putShort("temp", temp_setting);  // minimize writes
            tempIntervalTimer.forceExpire();
        } else {
            rotaryEncoder.setEncoderValue(temp_setting);
            updateLCD();
            lcdTimer.reset();
            lcd.display();
            lcd.backlight();
            rotary_engaged = true;
        }
    }

    if (rotary_engaged) {
        if (rotaryEncoder.encoderChanged()) {
            lcdTimer.reset();
            updateLCD(rotaryEncoder.readEncoder());
        } else if (lcdTimer.expired()) {
            temp_setting = rotaryEncoder.readEncoder();
            userSettings.putShort("temp", temp_setting);  // minimize writes
            tempIntervalTimer.forceExpire();
            lcd.noDisplay();
            lcd.noBacklight();
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
    if (heatState == LOW && current_temp <= temp_setting - DRIFT_DEGREES) {
        heatState = HIGH;
        digitalWrite(HEAT_PIN, heatState);
    } else if (heatState == HIGH && current_temp >= temp_setting + DRIFT_DEGREES) {
        heatState = LOW;
        digitalWrite(HEAT_PIN, heatState);
    }
}


// This is faster than using: AsyncWebSocketMessageBuffer and makeBuffer()
// https://github.com/ESP32Async/ESPAsyncWebServer/wiki#asyncwebsocketmessagebuffer-and-makebuffer
void send(JsonDocument& doc) {
    char jsonbuffer[256]; // use multiples of 8
    size_t len = serializeJson(doc, jsonbuffer);
    ws.textAll(jsonbuffer, len);
    // jsonbuffer must be at least the size of length + 1
    if (DEBUG) {Serial.printf("Sending: %s    length: %d\n", jsonbuffer, len);}
}


void sendStatus() {
    tubStateDoc["temp"] = (int)(current_temp * 10 + 0.5) / 10.0;  // round to 1 decimal place
    tubStateDoc["setTemp"] = temp_setting;
    tubStateDoc["light"] = light_setting;
    tubStateDoc["heat"] = static_cast<int>(heatState);
    send(tubStateDoc);
}


void buildDefaults() {
    tubDefaults["max"] = MAX_TEMP;
    tubDefaults["min"] = MIN_TEMP;
    JsonArray colors = tubDefaults["colors"].to<JsonArray>();
    neoPixel.moods2Json(colors);
}


void handleWebSocketMessage(uint8_t *data) {
    deserializeJson(receivedJson, (char*)data);

    if (receivedJson["setTemp"].is<int>()) {
        temp_setting = receivedJson["setTemp"];
        userSettings.putShort("temp", temp_setting);
        runThermostat();
    }
    if (receivedJson["light"].is<int>()) {
        light_setting = receivedJson["light"];
        neoPixel.setMood(light_setting);
        lightTimer.reset();
    }
    sendStatus();
}


void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            if (DEBUG) {Serial.printf("Client: %s id:%u (CONNECTED)\n", client->remoteIP().toString(), client->id());}
            send(tubDefaults);
            sendStatus();
        break;

        case WS_EVT_DISCONNECT:
            if (DEBUG) {Serial.printf("Client: %s id:%u (DISCONNECT)\n", client->remoteIP().toString(), client->id());}
        break;

        case WS_EVT_ERROR:
            if (DEBUG) {Serial.printf("Client: %s id:%u (ERROR)\n", client->remoteIP().toString(), client->id());}
        break;

        case WS_EVT_DATA:
            AwsFrameInfo *info = (AwsFrameInfo *)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                if (DEBUG) {Serial.printf("\nReceive: %s    length: %d\n", (char*)data, len);}
                data[len] = 0;
                handleWebSocketMessage(data);
            }
        break;
    }
}


void setup() {
    if (DEBUG) {Serial.begin(115200); delay(1000);}
    buildDefaults();

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
        if (DEBUG) {Serial.println("LittleFS mounted successfully");}
    }

    // Initialize WebSocket:
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
    server.serveStatic("/", LittleFS, "/");  // .serveStatic(requestURL, FileSystem, DirectoryOnESP32)
    server.begin();

    // Initialize NeoPixel
    neoPixel.begin();
    neoPixel.show();
    neoPixel.setBrightness(LED_BRIGHTNESS);

    // Initialize Watchdog Timer
    if (esp_task_wdt_init(WDT_TIMEOUT, true) == ESP_OK) {
        if (DEBUG) {Serial.println("Task Watchdog Timer Initialized");}
    } else {
        if (DEBUG) {Serial.println("ERROR: Task Watchdog Timer Failed to Initialize");}
    }
    if (esp_task_wdt_add(NULL) == ESP_OK) {
        if (DEBUG) {Serial.println("TWDT successfully added task");}
    } else {
        if (DEBUG) {Serial.println("ERROR: Unable to add task to TWDT");}
    }

    Serial.println();
}

void loop() {
    esp_task_wdt_reset();
    handleRotaryEncoder();

    // Timer for collecting thermistor readings
    if (dataIntervalTimer.expired()) {
        readThermistor();
    }

    // Timer for calculating a temp for thermostat and sendStatus()
    if (tempIntervalTimer.expired()) {
        current_temp = calculateTemp(medianBuffer.getAverage());
        tempIntervalTimer.reset();
        runThermostat();
        // if (DEBUG) {Serial.printf("Temp: %.2f    PumpState: %d    HeatState: %d\n", current_temp, pumpState, heatState);}
        if (DEBUG) {Serial.printf("------- Temp: %.2f    TempSet: %d    HeatState: %d    Light: %d -------\n", current_temp, temp_setting , heatState, light_setting);}
        // Only send to WebSocket when client count != 0
        if (ws.count()) {
            sendStatus();
            ws.cleanupClients(); // Cleanup disconnected clients or too many clients
        }
    }

    // Timer for running light
    neoPixel.runMood();
    if (light_setting && lightTimer.expired()) {
        neoPixel.setMood(0);  // Set Mood to OFF
        light_setting = 0;
    }

}
