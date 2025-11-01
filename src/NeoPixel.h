#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <MillisChronoTimer.h>

class NeoPixel : public Adafruit_NeoPixel {
public:
    NeoPixel(uint16_t pixel_num, int16_t pin = 6, neoPixelType type = NEO_GRB + NEO_KHZ800);

    enum class Mood {
        OFF, // Must be element 0
        RED,
        GREEN,
        BLUE,
        YELLOW,
        CYAN,
        PURPLE,
        PINK,
        ORANGE,
        WHITE,
        COLORCYCLE,
        RAINBOW,
        PARTY
    };

    void moods2Json(JsonArray json) const;
    void runMood();
    void setMood(uint16_t newMood);
    uint32_t getMood() const;

private:
    const uint16_t _numPixels;
    uint16_t _currentPixel;
    uint16_t _scene;
    Mood _currentMood;
    MillisChronoTimer pixelTimer;
    MillisChronoTimer sceneTimer;
    void colorWipe(uint32_t color);
    void rainbow();
    void party();
    uint32_t Wheel(byte WheelPos) const;

    struct ColorValues {
        const uint32_t RED = Adafruit_NeoPixel::Color(255, 0, 0);
        const uint32_t GREEN = Adafruit_NeoPixel::Color(0, 255, 0);
        const uint32_t BLUE = Adafruit_NeoPixel::Color(0, 0, 255);
        const uint32_t YELLOW = Adafruit_NeoPixel::Color(255, 255, 0);
        const uint32_t CYAN = Adafruit_NeoPixel::Color(0, 255, 255);
        const uint32_t PURPLE = Adafruit_NeoPixel::Color(255, 0, 255);
        const uint32_t PINK = Adafruit_NeoPixel::Color(255, 0, 195);
        const uint32_t ORANGE = Adafruit_NeoPixel::Color(255, 51, 0);
        const uint32_t WHITE = Adafruit_NeoPixel::Color(0, 0, 0, 255);
    };
    const ColorValues color;
};

#endif