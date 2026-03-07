#pragma once
/**
 * SettingsScene.h — Hierarchical Settings for Nebula OS
 */
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "../db/SettingsManager.h"
#include "../led/LEDManager.h"

enum class SettingsMenu { CATEGORIES, AUDIO, DISPLAY_PAGE, GPIO, AOD, STORAGE, USB_CFG, SENSORS, LIGHTING, RTOS };

class SettingsScene {
public:
    void init(GxEPD2_BW<GxEPD2_154_D67, 200>& disp, fs::FS& sd, SystemSettings& settings) {
        _disp = &disp;
        _sd = &sd;
        _settings = &settings;
        _menu = SettingsMenu::CATEGORIES;
        _selectedIdx = 0;
        _dirty = true;
    }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            _disp->fillScreen(_settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE);
            _renderMenu();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void drawPartial() {
        _disp->setPartialWindow(0, 0, 200, 200);
        _disp->firstPage();
        do {
            _disp->fillScreen(_settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE);
            _renderMenu();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void onUp() {
        int max = _getMaxItems();
        if (max > 0) {
            _selectedIdx = (max + _selectedIdx - 1) % max;
            _dirty = true;
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
    }

    void onDown() {
        int max = _getMaxItems();
        if (max > 0) {
            _selectedIdx = (_selectedIdx + 1) % max;
            _dirty = true;
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
    }

    bool wantsSensors() {
        bool v = _wantsSensors;
        _wantsSensors = false;
        return v;
    }

    bool wantsExplorer() {
        bool v = _wantsExplorer;
        _wantsExplorer = false;
        return v;
    }

    bool onSelect() {
        if (_menu == SettingsMenu::CATEGORIES) {
            if (_selectedIdx == 4) { // STORAGE
                _menu = SettingsMenu::STORAGE;
                _selectedIdx = 0;
                drawFull();
                return false;
            }
            if (_selectedIdx == 6) { // SENSORS
                _wantsSensors = true;
                return false;
            }
            if (_selectedIdx == 7) { // LIGHTING
                _menu = SettingsMenu::LIGHTING;
                _selectedIdx = 0;
                drawFull();
                return false;
            }
            if (_selectedIdx == 8) { // RTOS
                _menu = SettingsMenu::RTOS;
                _selectedIdx = 0;
                drawFull();
                return false;
            }
            _menu = (SettingsMenu)(_selectedIdx + 1);
            _selectedIdx = 0;
            drawFull();
        } else if (_menu == SettingsMenu::STORAGE) {
            if (_selectedIdx == 4) { // BROWSE FILES
                _wantsExplorer = true;
                return false;
            }
        } else {
            _handleValueAdjustment();
            _dirty = true;
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
        return false;
    }

    bool onBack() {
        if (_menu == SettingsMenu::CATEGORIES) {
            SettingsManager::save(*_sd, *_settings);
            return true;
        } else {
            _menu = SettingsMenu::CATEGORIES;
            _selectedIdx = 4; // Return to STORAGE category
            _dirty = true;
            drawFull();
            return false;
        }
    }

private:
    GxEPD2_BW<GxEPD2_154_D67, 200>* _disp;
    fs::FS* _sd;
    SystemSettings* _settings;
    SettingsMenu _menu;
    int _selectedIdx = 0;
    bool _dirty = true;
    bool _wantsSensors = false;
    bool _wantsExplorer = false;

    // Helper to identify which LED setting is at which index
    enum LEDItem { L_ENABLED, L_BRIGHTNESS, L_MODE, L_KELVIN, L_SMOOTHNESS, L_FADE_SPEED, L_AUTO_OFF, L_COLOR, L_COUNT };
    
    LEDItem _getLEDItem(int guiIdx) {
        int current = 0;
        for (int i = 0; i < L_COUNT; i++) {
            bool visible = true;
            if (i == L_KELVIN && _settings->led.mode != MODE_KELVIN) visible = false;
            if (i == L_COLOR && _settings->led.mode != MODE_SOLID) visible = false;
            if (i == L_FADE_SPEED && !_settings->led.smoothness) visible = false;
            
            if (visible) {
                if (current == guiIdx) return (LEDItem)i;
                current++;
            }
        }
        return L_COUNT;
    }

        int _getMaxItems() {
        switch(_menu) {
            case SettingsMenu::CATEGORIES:   return 9;
            case SettingsMenu::AUDIO:        return 11; // +Phase Inversion
            case SettingsMenu::DISPLAY_PAGE: return 8;  // +3x VSH1 voltage
            case SettingsMenu::USB_CFG:      return 3;
            case SettingsMenu::LIGHTING: {
                int count = 0;
                for (int i = 0; i < L_COUNT; i++) {
                    bool visible = true;
                    if (i == L_KELVIN && _settings->led.mode != MODE_KELVIN) visible = false;
                    if (i == L_COLOR && _settings->led.mode != MODE_SOLID) visible = false;
                    if (i == L_FADE_SPEED && !_settings->led.smoothness) visible = false;
                    if (visible) count++;
                }
                return count;
            }
            case SettingsMenu::RTOS:         return 4;
            case SettingsMenu::STORAGE:      return 5;
            default: return 0;
        }
    }

    void _renderMenu() {
        uint16_t bg = _settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE;
        uint16_t fg = _settings->display.inverted ? GxEPD_WHITE : GxEPD_BLACK;

        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->fillRect(0, 0, 200, 25, fg);
        _disp->setTextColor(bg);
        _disp->setCursor(5, 18);
        _disp->print(_getMenuTitle());
        _disp->setTextColor(fg);

        int items = _getMaxItems();
        
        // Scrolling logic: show window of 6 items
        int start = 0;
        if (_selectedIdx >= 6) {
            start = _selectedIdx - 5;
        }
        int end = min(start + 6, items);
        if (end - start < 6 && items > 6) {
            start = max(0, end - 6);
        }

        int y = 50;
        for (int i = start; i < end; i++) {
            if (i == _selectedIdx) {
                _disp->fillRect(0, y - 16, 200, 20, fg);
                _disp->setTextColor(bg);
            } else {
                _disp->setTextColor(fg);
            }
            _disp->setCursor(10, y);
            _disp->print(_getItemLabel(i));
            
            if (_menu != SettingsMenu::CATEGORIES) {
                String val = _getItemValue(i);
                int16_t x1, y1; uint16_t w, h;
                _disp->getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
                _disp->setCursor(190 - w, y);
                _disp->print(val);
            }
            y += 24;
        }
        
        // Scroll indicators
        if (start > 0) {
            _disp->fillTriangle(190, 35, 185, 40, 195, 40, fg);
        }
        if (end < items) {
            _disp->fillTriangle(190, 195, 185, 190, 195, 190, fg);
        }
    }

    const char* _getMenuTitle() {
        switch(_menu) {
            case SettingsMenu::CATEGORIES:   return "SETTINGS";
            case SettingsMenu::AUDIO:        return "AUDIO CFG";
            case SettingsMenu::DISPLAY_PAGE: return "DISPLAY CFG";
            case SettingsMenu::USB_CFG:      return "USB CFG";
            case SettingsMenu::LIGHTING:     return "LIGHTING CFG";
            case SettingsMenu::RTOS:         return "POWER CFG";
            case SettingsMenu::STORAGE:      return "STORAGE INFO";
            default: return "";
        }
    }

    const char* _getItemLabel(int i) {
        if (_menu == SettingsMenu::CATEGORIES) {
            const char* cats[] = {"AUDIO", "DISPLAY", "GPIO", "AOD", "STORAGE", "USB", "SENSORS", "LIGHTING", "RTOS"};
            return cats[i];
        } else if (_menu == SettingsMenu::AUDIO) {
            const char* audio[] = {"Volume", "Sample", "Chan", "Bits", "BufS", "BufC", "Prio", "Core", "Bal", "Phase", "BG Mode"};
            return audio[i];
        } else if (_menu == SettingsMenu::DISPLAY_PAGE) {
            const char* disp[] = {"Inverted", "Skip Art", "Partial", "CF Full", "SPI MHz", "VSH1 Menu", "VSH1 Media", "VSH1 AOD"};
            return disp[i];
        } else if (_menu == SettingsMenu::USB_CFG) {
            return (i==0)?"Mode":(i==1)?"Chunk":"Freq";
        } else if (_menu == SettingsMenu::LIGHTING) {
            LEDItem item = _getLEDItem(i);
            switch(item) {
                case L_ENABLED:    return "Enabled";
                case L_BRIGHTNESS: return "Brightness";
                case L_MODE:       return "Mode";
                case L_KELVIN:     return "Kelvin";
                case L_SMOOTHNESS: return "Smoothness";
                case L_FADE_SPEED: return "Fade Speed";
                case L_AUTO_OFF:   return "AutoOff";
                case L_COLOR:      return "Color";
                default:           return "";
            }
        } else if (_menu == SettingsMenu::RTOS) {
            const char* pwr[] = {"Menu CPU", "Music CPU", "USB CPU", "DEEP SLEEP"};
            return pwr[i];
        } else if (_menu == SettingsMenu::STORAGE) {
            const char* str[] = {"Total", "Used", "Free", "Music", "BROWSE FILES"};
            return str[i];
        }
        return "";
    }

    String _getItemValue(int i) {
        if (_menu == SettingsMenu::AUDIO) {
            switch(i) {
                case 0: return String((int)(_settings->audio.volume * 100)) + "%";
                case 1: return String(_settings->audio.sampleRate / 1000) + "k";
                case 2: return String(_settings->audio.channels);
                case 3: return String(_settings->audio.bitsPerSample);
                case 4: return String(_settings->audio.bufferSize);
                case 5: return String(_settings->audio.bufferCount);
                case 6: return String(_settings->audio.taskPriority);
                case 7: return String(_settings->audio.coreID);
                case 8: return String(_settings->audio.balance, 1);
                case 9: {
                    const char* phases[] = {"OFF", "LEFT", "RIGHT", "ALL"};
                    return phases[_settings->audio.phaseInversion];
                }
                case 10: return _settings->audio.backgroundMode ? "ON" : "OFF";
            }
        } else if (_menu == SettingsMenu::DISPLAY_PAGE) {
            switch(i) {
                case 0: return _settings->display.inverted ? "ON" : "OFF";
                case 1: return _settings->display.skipArtInvert ? "YES" : "NO";
                case 2: return _settings->display.partialRefresh ? "ON" : "OFF";
                case 3: return _settings->display.cfFullRefresh ? "ON" : "OFF";
                case 4: return String(_settings->display.spiFreqMhz) + "M";
                case 5: return String(_settings->display.vsh1Menu / 10.0, 1) + "V";
                case 6: return String(_settings->display.vsh1Media / 10.0, 1) + "V";
                case 7: return String(_settings->display.vsh1Aod / 10.0, 1) + "V";
            }
        } else if (_menu == SettingsMenu::USB_CFG) {
            if (i==0) return (_settings->usb.mode==0)?"SERIAL":(_settings->usb.mode==1)?"STORAGE":"FLASH";
            if (i==1) return String(_settings->usb.chunkSectors);
            return String(_settings->usb.freqKhz / 1000) + "k";
        } else if (_menu == SettingsMenu::LIGHTING) {
            LEDItem item = _getLEDItem(i);
            switch(item) {
                case L_ENABLED:    return _settings->led.enabled ? "ON" : "OFF";
                case L_BRIGHTNESS: return String(_settings->led.brightness) + "%";
                case L_MODE: {
                    const char* modes[] = {"SOLID", "KELVIN", "RAINBOW"};
                    return modes[_settings->led.mode];
                }
                case L_KELVIN:     return String(_settings->led.kelvin) + "K";
                case L_SMOOTHNESS: return _settings->led.smoothness ? "ON" : "OFF";
                case L_FADE_SPEED: return String(_settings->led.fadeSpeed);
                case L_AUTO_OFF:   return String(_settings->led.autoOffSec) + "s";
                case L_COLOR: {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "#%02X%02X%02X", _settings->led.solidColor.r, _settings->led.solidColor.g, _settings->led.solidColor.b);
                    return String(buf);
                }
                default: return "";
            }
        } else if (_menu == SettingsMenu::RTOS) {
            switch(i) {
                case 0: return String(_settings->power.menuFreq) + "M";
                case 1: return String(_settings->power.musicFreq) + "M";
                case 2: return String(_settings->power.usbFreq) + "M";
                case 3: return "ENTER";
            }
        } else if (_menu == SettingsMenu::STORAGE) {
            #include <SD_MMC.h>
            extern class MediaDB mediaDB;
            switch(i) {
                case 0: return _formatSize64(SD_MMC.totalBytes());
                case 1: return _formatSize64(SD_MMC.usedBytes());
                case 2: return _formatSize64(SD_MMC.totalBytes() - SD_MMC.usedBytes());
                case 3: return String(mediaDB.trackCount());
                case 4: return "";
            }
        }
        return "";
    }

    String _formatSize64(uint64_t bytes) {
        double sz = bytes;
        const char* units[] = {"B", "K", "M", "G"};
        int i = 0;
        while (sz > 1024 && i < 3) {
            sz /= 1024;
            i++;
        }
        return String(sz, 1) + units[i];
    }

    void _handleValueAdjustment() {
        if (_menu == SettingsMenu::AUDIO) {
            switch(_selectedIdx) {
                case 0: _settings->audio.volume += 0.05f; if(_settings->audio.volume > 1.01f) _settings->audio.volume = 0; break;
                case 1: {
                    uint32_t rates[] = {44100, 48000, 88200, 96000};
                    int rIdx = 0; for(;rIdx<4;rIdx++) if(rates[rIdx]==_settings->audio.sampleRate) break;
                    _settings->audio.sampleRate = rates[(rIdx+1)%4];
                    break;
                }
                case 2: _settings->audio.channels = (_settings->audio.channels == 1) ? 2 : 1; break;
                case 3: _settings->audio.bitsPerSample = (_settings->audio.bitsPerSample == 16) ? 24 : 16; break;
                case 4: _settings->audio.bufferSize += 512; if(_settings->audio.bufferSize > 4096) _settings->audio.bufferSize = 512; break;
                case 5: _settings->audio.bufferCount++; if(_settings->audio.bufferCount > 32) _settings->audio.bufferCount = 4; break;
                case 6: _settings->audio.taskPriority++; if(_settings->audio.taskPriority > 20) _settings->audio.taskPriority = 1; break;
                case 7: _settings->audio.coreID = 1 - _settings->audio.coreID; break;
                case 8: _settings->audio.balance += 0.2f; if(_settings->audio.balance > 1.1f) _settings->audio.balance = -1.0f; break;
                case 9: _settings->audio.phaseInversion = (PhaseInversionMode)((_settings->audio.phaseInversion + 1) % 4); break;
                case 10: _settings->audio.backgroundMode = !_settings->audio.backgroundMode; break;
            }
        } else if (_menu == SettingsMenu::DISPLAY_PAGE) {
            switch(_selectedIdx) {
                case 0: _settings->display.inverted = !_settings->display.inverted; break;
                case 1: _settings->display.skipArtInvert = !_settings->display.skipArtInvert; break;
                case 2: _settings->display.partialRefresh = !_settings->display.partialRefresh; break;
                case 3: _settings->display.cfFullRefresh = !_settings->display.cfFullRefresh; break;
                case 4: {
                    uint8_t spis[] = {2, 4, 8, 10, 20};
                    int idx = 0; for(;idx<5;idx++) if(spis[idx]==_settings->display.spiFreqMhz) break;
                    _settings->display.spiFreqMhz = spis[(idx+1)%5];
                    break;
                }
                case 5: { // VSH1 Menu - range 24-170 (2.4V-17V)
                    uint8_t& v = _settings->display.vsh1Menu;
                    v += 5; // 0.5V steps
                    if (v > 170) v = 24;
                    extern void applyChargePumpVoltage();
                    applyChargePumpVoltage();
                    break;
                }
                case 6: { // VSH1 Media
                    uint8_t& v = _settings->display.vsh1Media;
                    v += 5;
                    if (v > 170) v = 24;
                    extern void applyChargePumpVoltage();
                    applyChargePumpVoltage();
                    break;
                }
                case 7: { // VSH1 AOD
                    uint8_t& v = _settings->display.vsh1Aod;
                    v += 5;
                    if (v > 170) v = 24;
                    extern void applyChargePumpVoltage();
                    applyChargePumpVoltage();
                    break;
                }
            }
        } else if (_menu == SettingsMenu::USB_CFG) {
            if (_selectedIdx==0) _settings->usb.mode = (_settings->usb.mode + 1) % 3;
            else if (_selectedIdx==1) _settings->usb.chunkSectors = (_settings->usb.chunkSectors==64)?128:(_settings->usb.chunkSectors==128)?256:64;
            else _settings->usb.freqKhz = (_settings->usb.freqKhz==20000)?40000:(_settings->usb.freqKhz==40000)?80000:20000;
        } else if (_menu == SettingsMenu::LIGHTING) {
            LEDItem item = _getLEDItem(_selectedIdx);
            switch(item) {
                case L_ENABLED: _settings->led.enabled = !_settings->led.enabled; break;
                case L_BRIGHTNESS: {
                    uint8_t b = _settings->led.brightness;
                    if (b < 20) b += 2;
                    else if (b < 100) b += 10;
                    else b = 0;
                    if (b > 100) b = 100;
                    _settings->led.brightness = b;
                    break;
                }
                case L_MODE: {
                    _settings->led.mode = (_settings->led.mode + 1) % 3;
                    _selectedIdx = 0;
                    break;
                }
                case L_KELVIN: {
                    uint16_t k = _settings->led.kelvin;
                    if (k < 3000) k += 200;
                    else if (k < 6400) k += 500;
                    else k = 1000;
                    if (k > 6400) k = 6400;
                    _settings->led.kelvin = k;
                    break;
                }
                case L_SMOOTHNESS: {
                    _settings->led.smoothness = !_settings->led.smoothness;
                    _selectedIdx = 0;
                    break;
                }
                case L_FADE_SPEED: {
                    _settings->led.fadeSpeed += 25;
                    if (_settings->led.fadeSpeed == 0) _settings->led.fadeSpeed = 25;
                    break;
                }
                case L_AUTO_OFF: {
                    uint16_t timeouts[] = {0, 5, 10, 30, 60, 120, 300, 600};
                    int len = sizeof(timeouts)/sizeof(timeouts[0]);
                    int tIdx = 0; for(;tIdx < len; tIdx++) if(timeouts[tIdx] == _settings->led.autoOffSec) break;
                    _settings->led.autoOffSec = timeouts[(tIdx + 1) % len];
                    break;
                }
                case L_COLOR: {
                    if (_settings->led.solidColor.r == 255) { _settings->led.solidColor.r=0; _settings->led.solidColor.g=255; _settings->led.solidColor.b=0; }
                    else if (_settings->led.solidColor.g == 255) { _settings->led.solidColor.r=0; _settings->led.solidColor.g=0; _settings->led.solidColor.b=255; }
                    else { _settings->led.solidColor.r=255; _settings->led.solidColor.g=0; _settings->led.solidColor.b=0; }
                    break;
                }
                default: break;
            }
            LEDManager::getInstance().update(_settings->led);
            LEDManager::getInstance().resetIdleTimer();
        } else if (_menu == SettingsMenu::RTOS) {
            auto adjFreq = [](uint16_t& freq) {
                uint16_t rates[] = {20, 40, 80, 160, 240};
                int idx = 0; for(;idx<5;idx++) if(rates[idx]==freq) break;
                freq = rates[(idx+1)%5];
            };
            switch(_selectedIdx) {
                case 0: adjFreq(_settings->power.menuFreq); break;
                case 1: adjFreq(_settings->power.musicFreq); break;
                case 2: adjFreq(_settings->power.usbFreq); break;
                case 3: {
                    extern void enterDeepSleep();
                    enterDeepSleep();
                    break;
                }
            }
            extern void applyPowerSettings();
            applyPowerSettings();
        }
    }
};
