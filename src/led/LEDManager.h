#pragma once
/**
 * LEDManager.h — Ambience & Notification System for Nebula Player
 * Controls the built-in WS2812B LED on Pin 48.
 * Supports smooth transitions, Kelvin temperature, and USB reactivity.
 */
#include <Arduino.h>
#include <FastLED.h>
#include "../db/SettingsManager.h"

class LEDManager {
public:
    static LEDManager& getInstance() {
        static LEDManager instance;
        return instance;
    }

    void begin(const LEDSettings& settings);
    void update(const LEDSettings& settings);

    // Activity hooks
    void setTransferActive(bool active);
    void resetIdleTimer();

private:
    LEDManager() {}
    
    static void _ledTask(void* pvParameters);
    TaskHandle_t _taskHandle = NULL;
    
    CRGB _currentRGB = CRGB::Black;
    CRGB _targetRGB  = CRGB::Black;
    
    uint32_t _lastInputTime = 0;
    bool     _isTransferring = false;
    uint32_t _lastTransferTime = 0;
    
    CRGB _kelvinToRGB(uint16_t kelvin);
    void _applySmoothness(const LEDSettings& settings);

    static const uint8_t LED_PIN = 48;
    static const uint8_t NUM_LEDS = 1;
    CRGB _leds[NUM_LEDS];
};
