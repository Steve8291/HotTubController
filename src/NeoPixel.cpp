#include "NeoPixel.h"

NeoPixel::NeoPixel(uint16_t pixel_num, int16_t pin, neoPixelType type)
    : Adafruit_NeoPixel(pixel_num, pin, type),
      _numPixels(pixel_num),
      _currentPixel(0),
      _scene(0),
      _currentMood(Mood::OFF),
      pixelTimer(500),
      sceneTimer(5 * 60000) // 5min
    {}


void NeoPixel::runMood() {
    if (pixelTimer.expired()) {
        pixelTimer.reset();
        switch (_currentMood) {
            case Mood::RED:
                colorWipe(color.RED);
                break;
            case Mood::GREEN:
                colorWipe(color.GREEN);
                break;
            case Mood::BLUE:
                colorWipe(color.BLUE);
                break;
            case Mood::YELLOW:
                colorWipe(color.YELLOW);
                break;
                case Mood::CYAN:
                colorWipe(color.CYAN);
                break;
            case Mood::PURPLE:
                colorWipe(color.PURPLE);
                break;
            case Mood::PINK:
                colorWipe(color.PINK);
                break;
            case Mood::ORANGE:
                colorWipe(color.ORANGE);
                break;
            case Mood::WHITE:
                colorWipe(color.WHITE);
                break;
            case Mood::COLORCYCLE: {
                const uint32_t colorArray[] = {color.RED, color.GREEN, color.BLUE, color.YELLOW, color.CYAN, color.PURPLE, color.PINK, color.ORANGE, color.WHITE};
                const uint32_t cArraySize = sizeof(colorArray) / sizeof(colorArray[0]);
                colorWipe(colorArray[_scene]);
                
                if (sceneTimer.expired()) {
                    _currentPixel = 0;
                    sceneTimer.reset();
                    _scene++;
                    if (_scene >= cArraySize) {
                        _scene = 0;
                    }
                }
            } break;
            case Mood::RAINBOW:
                rainbow();
                break;
            case Mood::PARTY:
                party();
                break;
        }
    }
}

void NeoPixel::setMood(uint16_t newMood) {
    _currentMood = static_cast<Mood>(newMood);
    _currentPixel = 0;
    _scene = 0;
    // Change timer settings
    switch (_currentMood) {
        case Mood::OFF:
            Adafruit_NeoPixel::clear();
            Adafruit_NeoPixel::show();
            break;
        case Mood::RAINBOW:
            pixelTimer.modify(100);
            break;
        case Mood::PARTY:
            pixelTimer.modify(50);
            break;
        default:  // Used for colorWipe with solid colors and Cycle Colors
            pixelTimer.modify(500);
            break;
    }
    pixelTimer.forceExpire();
    sceneTimer.reset();
    NeoPixel::runMood();
}

uint32_t NeoPixel::getMood() const {
    return static_cast<uint32_t>(_currentMood);
}

void NeoPixel::moods2Json(JsonArray json) const {
    const char* const mood_strings[] = {
        "Light OFF", 
        "Red", 
        "Green", 
        "Blue", 
        "Yelow", 
        "Cyan", 
        "Purple", 
        "Pink", 
        "Orange", 
        "White", 
        "Color Cycle", 
        "Rainbow", 
        "Party"
    };
    for (const char* element : mood_strings) {
        json.add(element);
    }
}

void NeoPixel::colorWipe(uint32_t color) {
    Adafruit_NeoPixel::setPixelColor(_currentPixel++, color);
    Adafruit_NeoPixel::show();
    if(_currentPixel >= _numPixels) {
        _currentPixel = 0;
    }
}


void NeoPixel::rainbow() {
    for (uint16_t i = 0; i < _numPixels; i++) {
        Adafruit_NeoPixel::setPixelColor(i, Wheel((i * 256 / _numPixels + _scene) & 255));
    }
    Adafruit_NeoPixel::show();
    _scene++;
    if(_scene >= 256) {
        _scene = 0;
    }
}


void NeoPixel::party() {
    static uint32_t pixelQueue = 0;
    for(int i=0; i < _numPixels; i+=3) {
        Adafruit_NeoPixel::setPixelColor(i + pixelQueue, Wheel((i + _scene) % 255));
    }
    Adafruit_NeoPixel::show();
    for(int i=0; i < _numPixels; i+=3) {
        Adafruit_NeoPixel::setPixelColor(i + pixelQueue, Adafruit_NeoPixel::Color(0, 0, 0));
    }      
    pixelQueue++;
    _scene++;
    if(pixelQueue >= 3) {
        pixelQueue = 0;
    }
    if(_scene >= 256) {
        _scene = 0;
    }
}


// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t NeoPixel::Wheel(byte WheelPos) const {
    WheelPos = 255 - WheelPos;
    if(WheelPos < 85) {
      return Adafruit_NeoPixel::Color(255 - WheelPos * 3, 0, WheelPos * 3);
    }
    if(WheelPos < 170) {
      WheelPos -= 85;
      return Adafruit_NeoPixel::Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
    WheelPos -= 170;
    return Adafruit_NeoPixel::Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }

