#include "LEDManager.h"

extern SystemSettings sysSettings;

void LEDManager::begin(const LEDSettings& settings) {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(_leds, NUM_LEDS);
    _lastInputTime = millis();
    _currentRGB = CRGB::Black;
    
    if (_taskHandle == NULL) {
        xTaskCreatePinnedToCore(_ledTask, "LED_Task", 2048, this, 1, &_taskHandle, 0); // Core 0 to avoid display blocking
    }
}

void LEDManager::update(const LEDSettings& settings) {
    // Local target will be updated in task
}

void LEDManager::_ledTask(void* pvParameters) {
    LEDManager* self = (LEDManager*)pvParameters;
    
    for (;;) {
        const LEDSettings& settings = sysSettings.led;
        uint32_t now = millis();

        // 1. Auto-Off
        bool timedOut = false;
        if (settings.autoOffSec > 0 && (now - self->_lastInputTime > (uint32_t)settings.autoOffSec * 1000)) {
            timedOut = true;
        }

        // 2. State Logic
        if (!settings.enabled || timedOut) {
            self->_targetRGB = CRGB::Black;
        } else {
            // Check for activity timeout to switch back to "Idle" Green
            if (now - self->_lastTransferTime > 1000) {
                self->_isTransferring = false;
            }

            // Global USB Activity Override
            // Keep USB pulse active for 5 seconds after last data to keep user in "USB mode" visual
            if (now - self->_lastTransferTime < 5000) {
                // Interval 2 seconds (0.5Hz)
                float breathing = (exp(sin(now / 2000.0 * 2.0 * PI)) - 0.36787944) * 0.42545906;
                
                CRGB mscColor;
                if (self->_isTransferring) {
                    mscColor = self->_kelvinToRGB(settings.kelvin); // Warm White from settings
                } else {
                    mscColor = CRGB::Green; // Idle Green
                }
                
                self->_targetRGB = mscColor;
                // Target with brightness applied + breathing
                uint8_t masterBrightness = map(settings.brightness, 0, 100, 0, 255);
                self->_targetRGB.nscale8(masterBrightness * breathing);
            } else {
                // Normal Modes
                if (settings.mode == MODE_RAINBOW) {
                    self->_targetRGB = CHSV(now / 20, 255, map(settings.brightness, 0, 100, 0, 255));
                } else if (settings.mode == MODE_KELVIN) {
                    self->_targetRGB = self->_kelvinToRGB(settings.kelvin);
                    self->_targetRGB.nscale8(map(settings.brightness, 0, 100, 0, 255));
                } else { // SOLID
                    self->_targetRGB = CRGB(settings.solidColor.r, settings.solidColor.g, settings.solidColor.b);
                    self->_targetRGB.nscale8(map(settings.brightness, 0, 100, 0, 255));
                }
            }
        }

        // 3. Smoothness
        if (settings.smoothness) {
            self->_applySmoothness(settings);
        } else {
            self->_currentRGB = self->_targetRGB;
        }

        self->_leds[0] = self->_currentRGB;
        FastLED.show();
        
        vTaskDelay(pdMS_TO_TICKS(20)); // ~50fps
    }
}

void LEDManager::setTransferActive(bool active) {
    _isTransferring = active;
    _lastTransferTime = millis();
}

void LEDManager::resetIdleTimer() {
    _lastInputTime = millis();
}

CRGB LEDManager::_kelvinToRGB(uint16_t kelvin) {
    uint16_t k = constrain(kelvin, 1000, 6400);
    float temp = k / 100.0f;
    float r, g, b;

    if (temp <= 66) {
        r = 255;
        g = 99.4708025861 * log(temp) - 161.1195681661;
        if (temp <= 19) b = 0;
        else b = 138.5177312231 * log(temp - 10) - 305.0447927307;
    } else {
        r = 329.698727446 * pow(temp - 60, -0.1332047592);
        g = 288.1221695283 * pow(temp - 60, -0.0755148492);
        b = 255;
    }

    return CRGB(constrain((int)r, 0, 255), constrain((int)g, 0, 255), constrain((int)b, 0, 255));
}

void LEDManager::_applySmoothness(const LEDSettings& settings) {
    // Variable step size for smoother look
    uint8_t step = (settings.fadeSpeed / 10) + 1;
    
    auto move = [step](uint8_t& cur, uint8_t tar) {
        if (cur < tar) {
            uint16_t n = cur + step;
            cur = (n > tar) ? tar : n;
        } else if (cur > tar) {
            int16_t n = cur - step;
            cur = (n < tar) ? tar : n;
        }
    };

    move(_currentRGB.r, _targetRGB.r);
    move(_currentRGB.g, _targetRGB.g);
    move(_currentRGB.b, _targetRGB.b);
}
