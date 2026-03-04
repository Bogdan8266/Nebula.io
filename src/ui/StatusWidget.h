#pragma once
/**
 * StatusWidget.h — Top status bar (State B of Now Playing header)
 */
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

struct StatusConfig {
    bool showTime    = true;
    bool showBattery = true;
    bool showSD      = true;
};

namespace Icons {
    static const char* battery(float v) {
        if (v >= 4.05f) return "|||"; 
        if (v >= 3.85f) return "||."; 
        if (v >= 3.70f) return "|.."; 
        return "!!!";                
    }
    static const char* sd(bool ok)  { return ok ? "SD" : "SD!"; }
}

class StatusWidget {
public:
    StatusConfig cfg;

    void setBattery(float v) { _batV = v; }
    void setTime(uint8_t h, uint8_t m) { _h = h; _m = m; }
    void setSD(bool ok)   { _sdOk = ok; }
    void setTrack(const char* title) { strncpy(_track, title, sizeof(_track)-1); _track[sizeof(_track)-1]=0; }
    void setPlaying(bool p) { _playing = p; }

    /**
     * Draws status bar with Time on Left and Battery on Right.
     * Used in NowPlaying scene.
     */
    template<typename T>
    void draw(T& display, uint16_t x, uint16_t y, uint16_t width, uint16_t fg = GxEPD_WHITE, uint16_t bg = GxEPD_BLACK) {
        display.setFont(&FreeMonoBold9pt7b);

        auto drawOutline = [&](const char* str, int16_t cx, int16_t cy) {
            display.setTextColor(bg);
            display.setCursor(cx - 1, cy); display.print(str);
            display.setCursor(cx + 1, cy); display.print(str);
            display.setCursor(cx, cy - 1); display.print(str);
            display.setCursor(cx, cy + 1); display.print(str);
            display.setTextColor(fg);
            display.setCursor(cx, cy); display.print(str);
        };

        if (cfg.showTime) {
            char tBuf[8];
            snprintf(tBuf, sizeof(tBuf), "%02u:%02u", _h, _m);
            drawOutline(tBuf, x, y);
        }

        if (cfg.showBattery) {
            char bBuf[32];
            snprintf(bBuf, sizeof(bBuf), "%.2fV %s", _batV, Icons::battery(_batV));
            // rough alignment
            int16_t bx = x + width - (strlen(bBuf) * 11) - 8;
            if (bx < 80) bx = 80; 
            drawOutline(bBuf, bx, y);
        }
    }

    /**
     * Draws a single-line status bar at the very top:
     * [Time]  [Track Name if playing]  [Bat]
     */
    template<typename T>
    void drawGlobal(T& display, uint16_t x, uint16_t y, uint16_t width, uint16_t fg = GxEPD_WHITE, uint16_t bg = GxEPD_BLACK) {
        display.setFont(&FreeMonoBold9pt7b);
        
        auto drawOutline = [&](const char* str, int16_t cx, int16_t cy) {
            display.setTextColor(bg);
            display.setCursor(cx - 1, cy); display.print(str);
            display.setCursor(cx + 1, cy); display.print(str);
            display.setCursor(cx, cy - 1); display.print(str);
            display.setCursor(cx, cy + 1); display.print(str);
            display.setTextColor(fg);
            display.setCursor(cx, cy); display.print(str);
        };

        // 1. Time (Left)
        char tBuf[8];
        snprintf(tBuf, sizeof(tBuf), "%02u:%02u", _h, _m);
        drawOutline(tBuf, x, y);

        // 2. Track Name (Center, truncated)
        if (_playing && _track[0]) {
            display.setFont(&FreeMono9pt7b);
            char b[32];
            snprintf(b, sizeof(b), "> %s", _track);
            b[18] = '\0'; // Truncate for space
            
            int16_t tx1, ty1; uint16_t tw, th;
            display.getTextBounds(b, 0, 0, &tx1, &ty1, &tw, &th);
            drawOutline(b, x + (width - tw) / 2, y);
            display.setFont(&FreeMonoBold9pt7b);
        }

        // 3. Bat (Right)
        char bBuf[16];
        snprintf(bBuf, sizeof(bBuf), "%.1fV %s", _batV, Icons::battery(_batV));
        int16_t bx1, by1; uint16_t bw, bh;
        display.getTextBounds(bBuf, 0, 0, &bx1, &by1, &bw, &bh);
        drawOutline(bBuf, x + width - bw - 2, y);
    }

private:
    float    _batV    = 3.7f;
    uint8_t  _h = 0, _m = 0;
    bool     _sdOk    = true;
    bool     _playing = false;
    char     _track[64] = {};
};
