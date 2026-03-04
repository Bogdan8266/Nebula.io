#pragma once
/**
 * MainMenuScene.h — 2x2 Grid Menu for Nebula OS
 */
#include <FS.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "../db/SettingsManager.h"

#include "StatusWidget.h"

// 48x48 Icons (1-bit)
template<typename Display, typename FS_T = fs::FS>
class MainMenuScene {
public:
    StatusWidget* status = nullptr;

    void init(Display& disp, FS_T& sd) {
        _disp = &disp;
        _sd   = &sd;
        _selected = 0;
        _dirty = true;
    }

    void setSettings(const SystemSettings& settings) { _settings = &settings; }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            _disp->fillScreen(_bg);
            if (status) status->drawGlobal(*_disp, 0, 14, 200, _fg, _bg); 
            _renderGrid();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void drawPartial() {
        _disp->setPartialWindow(0, 0, 200, 200);
        _disp->firstPage();
        do {
            _disp->fillScreen(_bg);
            if (status) status->drawGlobal(*_disp, 0, 14, 200, _fg, _bg); 
            _renderGrid();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void onNext() {
        _selected = (_selected + 1) % 4;
        _dirty = true;
        if (_settings && _settings->display.partialRefresh) drawPartial();
        else drawFull();
    }

    void onPrev() {
        _selected = (_selected + 3) % 4;
        _dirty = true;
        if (_settings && _settings->display.partialRefresh) drawPartial();
        else drawFull();
    }

    uint8_t selectedIndex() const { return _selected; }
    bool isDirty() const { return _dirty; }

    void setColors(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; }

private:
    Display*  _disp = nullptr;
    FS_T*     _sd   = nullptr;
    const SystemSettings* _settings = nullptr;
    uint8_t   _selected = 0;
    bool      _dirty = false;
    uint16_t  _fg    = GxEPD_WHITE;
    uint16_t  _bg    = GxEPD_BLACK;

    void _renderGrid() {
        // Shifted down to accommodate status bar
        _renderCell(0, 0, 40, "MEDIA", "/Bitmaps/Music.bin"); 
        _renderCell(1, 100, 40, "TELEMETRY", "/Bitmaps/Telemetry.bin"); 
        _renderCell(2, 0, 130, "EXTRAS", "/Bitmaps/Extra.bin"); // Shifted down by 20
        _renderCell(3, 100, 130, "SETTINGS", "/Bitmaps/Settings.bin"); // Shifted down by 20
    }

    void _renderCell(uint8_t idx, int16_t x, int16_t y, const char* label, const char* iconPath) {
        bool selected = (idx == _selected);
        uint16_t cellH = 90; // Reduced height to fit 2 rows + status bar
        
        if (selected) {
            _disp->fillRect(x, y, 100, cellH, _fg);
            _drawIconFromSD(x + 26, y + 10, iconPath, true);
            _disp->setTextColor(_bg);
        } else {
            _drawIconFromSD(x + 26, y + 10, iconPath, false);
            _disp->setTextColor(_fg);
        }

        _disp->setFont(&FreeMonoBold9pt7b);
        int16_t x1, y1; uint16_t w, h;
        _disp->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
        _disp->setCursor(x + (100 - w) / 2, y + 75);
        _disp->print(label);
        
        _disp->drawRect(x, y, 100, cellH, selected ? _bg : _fg);
    }

    void _drawIconFromSD(int16_t x, int16_t y, const char* path, bool currentCellSelected) {
        if (!_sd) return;
        
        auto file = _sd->open(path, "r");
        if (!file) {
            _disp->drawRect(x, y, 48, 48, currentCellSelected ? _bg : _fg);
            return;
        }

        uint8_t src[72];
        if (file.read(src, 72) != 72) { file.close(); return; }
        file.close();

        uint8_t dst[288];
        memset(dst, 0, 288);

        for (int row = 0; row < 24; row++) {
            for (int col = 0; col < 24; col++) {
                int srcByteIdx = (row * 3) + (col / 8);
                int srcBitIdx  = 7 - (col % 8);
                if ((src[srcByteIdx] >> srcBitIdx) & 1) {
                    _setScaledPixel(dst, col * 2,     row * 2);
                    _setScaledPixel(dst, col * 2 + 1, row * 2);
                    _setScaledPixel(dst, col * 2,     row * 2 + 1);
                    _setScaledPixel(dst, col * 2 + 1, row * 2 + 1);
                }
            }
        }

        uint16_t iconColor = currentCellSelected ? _bg : _fg;
        uint16_t iconBG    = currentCellSelected ? _fg : _bg;
        _disp->drawBitmap(x, y, dst, 48, 48, iconColor, iconBG);
    }

    void _setScaledPixel(uint8_t* buf, int x, int y) {
        if (x < 0 || x >= 48 || y < 0 || y >= 48) return;
        int byteIdx = (y * 6) + (x / 8);
        int bitIdx  = 7 - (x % 8);
        buf[byteIdx] |= (1 << bitIdx);
    }
};
