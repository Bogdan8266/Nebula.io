#pragma once
/**
 * UsbMenuScene.h — USB Menu and Secret Vault logic for Nebula OS
 */
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

class UsbMenuScene {
public:
    void init(GxEPD2_BW<GxEPD2_154_D67, 200>& disp) {
        _disp = &disp;
        _selectedIdx = 0;
        _vaultUnlocked = false;
        _leftPressCount = 0;
        _lastLeftPress = 0;
    }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            _disp->fillScreen(GxEPD_BLACK); 
            _renderMenu();
        } while (_disp->nextPage());
    }

    void onPrev() {
        uint32_t now = millis();
        if (now - _lastLeftPress < 2000) {
            _leftPressCount++;
            if (_leftPressCount >= 3 && !_vaultUnlocked) {
                _vaultUnlocked = true;
                _leftPressCount = 0;
                drawFull();
                return;
            }
        } else {
            _leftPressCount = 1;
        }
        _lastLeftPress = now;

        if (_selectedIdx > 0) {
            _selectedIdx--;
            drawFull();
        }
    }

    void onNext() {
        _leftPressCount = 0; // reset combo
        int max = _vaultUnlocked ? 5 : 4;
        if (_selectedIdx < max - 1) {
            _selectedIdx++;
            drawFull();
        }
    }

    int getSelectedIndex() { return _selectedIdx; }
    bool isVaultUnlocked() const { return _vaultUnlocked; }

private:
    GxEPD2_BW<GxEPD2_154_D67, 200>* _disp;
    int _selectedIdx = 0;
    bool _vaultUnlocked = false;
    int _leftPressCount = 0;
    uint32_t _lastLeftPress = 0;

    void _renderMenu() {
        uint16_t bg = GxEPD_BLACK;
        uint16_t fg = GxEPD_WHITE;

        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->fillRect(0, 0, 200, 25, fg);
        _disp->setTextColor(bg);
        _disp->setCursor(5, 18);
        _disp->print("USB MODE");
        
        _disp->setFont(&FreeMono9pt7b);
        int y = 50;
        const char* modes[] = {
            ">_ SERIAL", 
            "[_] STORAGE", 
            "(R) MUSIC", 
            "!!! SLOWBOOT", 
            "*** THE VAULT"
        };
        int max = _vaultUnlocked ? 5 : 4;

        for (int i = 0; i < max; i++) {
            if (i == _selectedIdx) {
                _disp->fillRect(0, y - 16, 200, 22, fg);
                _disp->setTextColor(bg);
            } else {
                _disp->setTextColor(fg);
            }
            _disp->setCursor(10, y + 2);
            _disp->print(modes[i]);
            y += 28;
        }
    }
};
