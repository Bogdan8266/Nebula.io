#pragma once
/**
 * NowPlaying.h — "Now Playing" scene for Nebula OS
 */

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "StatusWidget.h"

static constexpr uint16_t NP_W           = 200;
static constexpr uint16_t NP_H           = 200;
static constexpr uint16_t HEADER_Y       = 14; 
static constexpr uint16_t HEADER_H       = 30; 
static constexpr uint16_t HEADER_ARTIST_Y= 27; 

static constexpr uint16_t ART_SIZE       = 140; 
static constexpr uint16_t ART_X          = (NP_W - ART_SIZE) / 2; // 30
static constexpr uint16_t ART_Y          = HEADER_H + 4;           // 34

static constexpr uint16_t BTN_Y          = NP_H - 6;  
static constexpr uint16_t BTN_ZONE_Y     = NP_H - 22; 

static constexpr uint32_t HEADER_TOGGLE_MS = 10000;   

enum class HeaderState { TRACK_INFO, STATUS_BAR };

template<typename DisplayType>
class NowPlaying {
public:
    StatusWidget status;

    NowPlaying(DisplayType& disp) : _disp(disp) {
        strncpy(_title,  "No Track",  sizeof(_title)  - 1);
        strncpy(_artist, "Unknown",   sizeof(_artist) - 1);
    }

    void setTrack(const char* title, const char* artist) {
        strncpy(_title,  title,  sizeof(_title)  - 1); _title [sizeof(_title) -1]=0;
        strncpy(_artist, artist, sizeof(_artist) - 1); _artist[sizeof(_artist)-1]=0;
    }

    void setPlaying(bool p) { _playing = p; }
    bool isPlaying() const  { return _playing; }
    void setHasArt(bool has) { _hasArt = has; }
    void setBgBitmap(const uint8_t* bg) { _bgBitmap = bg; }

    void tick(uint32_t nowMs) {
        if (nowMs - _lastToggle >= HEADER_TOGGLE_MS) {
            toggleHeader();
            _lastToggle = nowMs;
        }
    }

    void toggleHeader() {
        _headerState = (_headerState == HeaderState::TRACK_INFO)
                       ? HeaderState::STATUS_BAR
                       : HeaderState::TRACK_INFO;
        _headerDirty = true;
    }

    void setColors(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; }
    void setBgColors(uint16_t fg, uint16_t bg) { _bgFG = fg; _bgBG = bg; }

    void drawFull() {
        // We assume the window is already set and filled by caller (main.cpp)
        _drawHeader();
        if (!_hasArt) _drawArtPlaceholder();
        else          _drawArtFrame();
        _drawButtons();
        _headerDirty = false;
    }

    void updateHeaderIfDirty() {
        if (!_headerDirty) return;
        _disp.setPartialWindow(0, 0, NP_W, HEADER_H);
        _disp.firstPage();
        do {
            if (_bgBitmap) _disp.drawBitmap(0, 0, _bgBitmap, 200, 200, _bgFG, _bgBG);
            else _disp.fillScreen(_bg);
            _drawHeader();
        } while (_disp.nextPage());
        _headerDirty = false;
    }

    void updateButtons() {
        _disp.setPartialWindow(0, BTN_ZONE_Y, NP_W, NP_H - BTN_ZONE_Y);
        _disp.firstPage();
        do {
            if (_bgBitmap) _disp.drawBitmap(0, 0, _bgBitmap, 200, 200, _bgFG, _bgBG);
            else _disp.fillScreen(_bg);
            _drawButtons();
        } while (_disp.nextPage());
    }

    bool headerDirty() const { return _headerDirty; }

private:
    DisplayType& _disp;
    HeaderState  _headerState = HeaderState::TRACK_INFO;
    bool         _playing     = false;
    bool         _headerDirty = false;
    bool         _hasArt      = false;
    uint32_t     _lastToggle  = 0;
    const uint8_t* _bgBitmap  = nullptr;
    char         _title [64]  = {};
    char         _artist[64]  = {};
    
    uint16_t     _fg = GxEPD_WHITE;
    uint16_t     _bg = GxEPD_BLACK;
    uint16_t     _bgFG = GxEPD_WHITE;
    uint16_t     _bgBG = GxEPD_BLACK;

    void drawTextOutline(const char* str, int16_t cx, int16_t cy) {
        _disp.setTextColor(_bg);
        // Radius 2 outline for maximum contrast
        for (int16_t dx = -2; dx <= 2; dx++) {
            for (int16_t dy = -2; dy <= 2; dy++) {
                if (dx == 0 && dy == 0) continue;
                if (abs(dx) == 2 && abs(dy) == 2) continue; // slightly rounded corners
                _disp.setCursor(cx + dx, cy + dy);
                _disp.print(str);
            }
        }
        _disp.setTextColor(_fg);
        _disp.setCursor(cx, cy); _disp.print(str);
    }

    void _drawHeader() {
        if (_headerState == HeaderState::TRACK_INFO) {
            _drawTrackInfo();
        } else {
            _drawStatusBar();
        }
    }

    void _drawTrackInfo() {
        _disp.setFont(&FreeMonoBold9pt7b);
        char buf[27];
        strncpy(buf, _title, 26); buf[26] = '\0';
        drawTextOutline(buf, 2, HEADER_Y);

        _disp.setFont(&FreeMono9pt7b);
        strncpy(buf, _artist, 26); buf[26] = '\0';
        drawTextOutline(buf, 2, HEADER_ARTIST_Y);
    }

    void _drawStatusBar() {
        // Line 1: Time Left, Battery Right (Bold)
        status.draw(_disp, 2, HEADER_Y, NP_W, _fg, _bg);
        // Line 2: Mode (Regular)
        _disp.setFont(&FreeMono9pt7b);
        drawTextOutline(_playing ? ">> PLAY" : "|| PAUSE", 2, HEADER_ARTIST_Y);
    }

    void _drawArtFrame() {
        _disp.drawRect(ART_X - 1, ART_Y - 1, ART_SIZE + 2, ART_SIZE + 2, _fg);
    }

    void _drawArtPlaceholder() {
        _drawArtFrame();
        _disp.setFont(&FreeMono9pt7b);
        drawTextOutline("NO ART", ART_X + 35, ART_Y + 75);
    }

    void _drawButtons() {
        const char* pS = _playing ? "||" : "|>";
        char buf[32];
        snprintf(buf, sizeof(buf), "<<  %s  >>  MENU", pS);

        _disp.drawFastHLine(0, BTN_ZONE_Y, NP_W, _bg);
        _disp.drawFastHLine(0, BTN_ZONE_Y + 1, NP_W, _fg);
        _disp.drawFastHLine(0, BTN_ZONE_Y + 2, NP_W, _bg);

        _disp.setFont(&FreeMono9pt7b);
        
        _disp.setTextColor(_bg);
        for (int16_t dx = -3; dx <= 3; dx++) {
            for (int16_t dy = -3; dy <= 3; dy++) {
                if (abs(dx) == 3 && abs(dy) == 3) continue;
                _disp.setCursor(8 + dx, BTN_Y + dy);
                _disp.print(buf);
            }
        }
        _disp.setTextColor(_fg);
        _disp.setCursor(8, BTN_Y);
        _disp.print(buf);
    }
};
