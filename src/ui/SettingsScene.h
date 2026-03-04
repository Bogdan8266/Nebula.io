#pragma once
/**
 * SettingsScene.h — Hierarchical Settings for Nebula OS
 */
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "../db/SettingsManager.h"

enum class SettingsMenu { CATEGORIES, AUDIO, DISPLAY_PAGE, GPIO, AOD, STORAGE, USB_CFG, SENSORS };

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
        if (_selectedIdx > 0) {
            _selectedIdx--;
            _dirty = true;
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
    }

    void onDown() {
        int max = _getMaxItems();
        if (_selectedIdx < max - 1) {
            _selectedIdx++;
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

    bool onSelect() {
        if (_menu == SettingsMenu::CATEGORIES) {
            if (_selectedIdx == 6) { // SENSORS is index 6 (7th item)
                _wantsSensors = true;
                return false;
            }
            _menu = (SettingsMenu)(_selectedIdx + 1);
            _selectedIdx = 0;
            drawFull();
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
            return true; // Go back to main menu
        } else {
            _menu = SettingsMenu::CATEGORIES;
            _selectedIdx = 0;
            _dirty = true;
            drawFull();
            return false;
        }
    }

    // wantsUsbMenu removed

private:
    GxEPD2_BW<GxEPD2_154_D67, 200>* _disp;
    fs::FS* _sd;
    SystemSettings* _settings;
    SettingsMenu _menu;
    int _selectedIdx = 0;
    bool _dirty = true;
    bool _isLongPress = false;
    bool _wantsSensors = false;

    int _getMaxItems() {
        switch(_menu) {
            case SettingsMenu::CATEGORIES:   return 7;
            case SettingsMenu::AUDIO:        return 9;
            case SettingsMenu::DISPLAY_PAGE: return 4;
            case SettingsMenu::USB_CFG:      return 3;
            default: return 0;
        }
    }

    void _renderMenu() {
        uint16_t bg = _settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE;
        uint16_t fg = _settings->display.inverted ? GxEPD_WHITE : GxEPD_BLACK;

        _disp->setFont(&FreeMonoBold9pt7b);
        
        // Header
        _disp->fillRect(0, 0, 200, 25, fg);
        _disp->setTextColor(bg);
        _disp->setCursor(5, 18);
        _disp->print(_getMenuTitle());
        _disp->setTextColor(fg);

        // List
        int y = 50;
        int items = _getMaxItems();
        for (int i = 0; i < items; i++) {
            if (i == _selectedIdx) {
                _disp->fillRect(0, y - 16, 200, 20, fg);
                _disp->setTextColor(bg);
            } else {
                _disp->setTextColor(fg);
            }
            _disp->setCursor(10, y);
            _disp->print(_getItemLabel(i));
            
            // Value display for submenus
            if (_menu != SettingsMenu::CATEGORIES) {
                String val = _getItemValue(i);
                int16_t x1, y1; uint16_t w, h;
                _disp->getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
                _disp->setCursor(190 - w, y);
                _disp->print(val);
            }
            
            y += 24;
        }
    }

    const char* _getMenuTitle() {
        switch(_menu) {
            case SettingsMenu::CATEGORIES:   return "SETTINGS";
            case SettingsMenu::AUDIO:        return "AUDIO CFG";
            case SettingsMenu::DISPLAY_PAGE: return "DISPLAY CFG";
            case SettingsMenu::GPIO:         return "GPIO CFG";
            case SettingsMenu::AOD:          return "AOD CFG";
            case SettingsMenu::STORAGE:      return "STORAGE CFG";
            case SettingsMenu::USB_CFG:      return "USB CFG";
            default: return "";
        }
    }

    const char* _getItemLabel(int i) {
        if (_menu == SettingsMenu::CATEGORIES) {
            const char* cats[] = {"AUDIO", "DISPLAY", "GPIO", "AOD", "STORAGE", "USB", "SENSORS"};
            return cats[i];
        } else if (_menu == SettingsMenu::AUDIO) {
            const char* audio[] = {"Volume", "SampleRate", "Channels", "Bits", "BufSize", "BufCount", "Priority", "Core", "Balance"};
            return audio[i];
        } else if (_menu == SettingsMenu::DISPLAY_PAGE) {
            const char* disp[] = {"Inverted", "Skip Art", "Partial Ref", "CF Full Ref"};
            return disp[i];
        } else if (_menu == SettingsMenu::USB_CFG) {
            const char* usb[] = {"Mode", "ChunkSecs", "FreqKHz"};
            return usb[i];
        }
        return "";
    }

    String _getItemValue(int i) {
        if (_menu == SettingsMenu::AUDIO) {
            switch(i) {
                case 0: return String((int)(_settings->audio.volume * 100)) + "%";
                case 1: return String(_settings->audio.sampleRate);
                case 2: return String(_settings->audio.channels);
                case 3: return String(_settings->audio.bitsPerSample);
                case 4: return String(_settings->audio.bufferSize);
                case 5: return String(_settings->audio.bufferCount);
                case 6: return String(_settings->audio.taskPriority);
                case 7: return String(_settings->audio.coreID);
                case 8: return String(_settings->audio.balance, 1);
            }
        } else if (_menu == SettingsMenu::DISPLAY_PAGE) {
            switch(i) {
                case 0: return _settings->display.inverted ? "ON" : "OFF";
                case 1: return _settings->display.skipArtInvert ? "YES" : "NO";
                case 2: return _settings->display.partialRefresh ? "ON" : "OFF";
                case 3: return _settings->display.cfFullRefresh ? "ON" : "OFF";
            }
        } else if (_menu == SettingsMenu::USB_CFG) {
            switch(i) {
                case 0: 
                    if (_settings->usb.mode == 0) return "SERIAL";
                    if (_settings->usb.mode == 1) return "STORAGE";
                    return "FLASH";
                case 1:
                    return String(_settings->usb.chunkSectors);
                case 2:
                    return String(_settings->usb.freqKhz / 1000) + "k";
            }
        }
        return "";
    }

    void _handleValueAdjustment() {
        // Simple toggle/increment for now
        if (_menu == SettingsMenu::AUDIO) {
            switch(_selectedIdx) {
                case 0: _settings->audio.volume += 0.05f; if(_settings->audio.volume > 1.01f) _settings->audio.volume = 0; break;
                case 1: 
                    if (_settings->audio.sampleRate == 44100) _settings->audio.sampleRate = 48000;
                    else if (_settings->audio.sampleRate == 48000) _settings->audio.sampleRate = 88200;
                    else if (_settings->audio.sampleRate == 88200) _settings->audio.sampleRate = 96000;
                    else _settings->audio.sampleRate = 44100;
                    break;
                case 2: _settings->audio.channels = (_settings->audio.channels == 1) ? 2 : 1; break;
                case 3: _settings->audio.bitsPerSample = (_settings->audio.bitsPerSample == 16) ? 24 : 16; break;
                case 4: _settings->audio.bufferSize += 512; if(_settings->audio.bufferSize > 4096) _settings->audio.bufferSize = 512; break;
                case 5: _settings->audio.bufferCount++; if(_settings->audio.bufferCount > 32) _settings->audio.bufferCount = 4; break;
                case 6: _settings->audio.taskPriority++; if(_settings->audio.taskPriority > 20) _settings->audio.taskPriority = 1; break;
                case 7: _settings->audio.coreID = 1 - _settings->audio.coreID; break;
                case 8: _settings->audio.balance += 0.2f; if(_settings->audio.balance > 1.1f) _settings->audio.balance = -1.0f; break;
            }
        } else if (_menu == SettingsMenu::DISPLAY_PAGE) {
            switch(_selectedIdx) {
                case 0: _settings->display.inverted = !_settings->display.inverted; break;
                case 1: _settings->display.skipArtInvert = !_settings->display.skipArtInvert; break;
                case 2: _settings->display.partialRefresh = !_settings->display.partialRefresh; break;
                case 3: _settings->display.cfFullRefresh = !_settings->display.cfFullRefresh; break;
            }
        } else if (_menu == SettingsMenu::USB_CFG) {
            switch(_selectedIdx) {
                case 0: 
                    _settings->usb.mode = (_settings->usb.mode + 1) % 3;
                    break;
                case 1:
                    if (_settings->usb.chunkSectors == 64) _settings->usb.chunkSectors = 128;
                    else if (_settings->usb.chunkSectors == 128) _settings->usb.chunkSectors = 256;
                    else _settings->usb.chunkSectors = 64;
                    break;
                case 2:
                    if (_settings->usb.freqKhz == 20000) _settings->usb.freqKhz = 40000;
                    else if (_settings->usb.freqKhz == 40000) _settings->usb.freqKhz = 80000;
                    else _settings->usb.freqKhz = 20000;
                    break;
            }
        }
    }
};
