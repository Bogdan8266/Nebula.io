#pragma once
/**
 * CoverFlowScene.h — iPod-style album browser for Nebula OS
 */
#include <FS.h>
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "../db/MediaDB.h"
#include "../db/ArtExtractor.h"
#include "../db/SettingsManager.h"
#include "ArtRenderer.h"
#include "StatusWidget.h"

template<typename Display, typename FS_T = fs::FS>
class CoverFlowScene {
public:
    StatusWidget* status = nullptr;

    void init(MediaDB& db, Display& disp, FS_T& sd) {
        _db = &db; _disp = &disp; _sd = &sd;
        
        if (!_bgBitmap) _bgBitmap = (uint8_t*)malloc(5000);
        _bgValid = false;

        _loadAlbums();
        _selected = 0;
        _dirty = true;
    }

    void setSettings(const SystemSettings& settings) { _settings = &settings; }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            if (status) status->drawGlobal(*_disp, 0, 14, 200, _fg, _bg);
            _renderCarousel();
            _renderText();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void drawPartial() {
        _disp->setPartialWindow(0, 0, 200, 200);
        _disp->firstPage();
        do {
            if (status) status->drawGlobal(*_disp, 0, 14, 200, _fg, _bg);
            _renderCarousel();
            _renderText();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void onNext() {
        if (_count == 0) return;
        _selected = (_selected + 1) % _count;
        _bgValid = false;
        _dirty = true;
        if (_settings && _settings->display.cfFullRefresh) drawFull();
        else drawPartial();
    }

    void onPrev() {
        if (_count == 0) return;
        _selected = (_selected + _count - 1) % _count;
        _bgValid = false;
        _dirty = true;
        if (_settings && _settings->display.cfFullRefresh) drawFull();
        else drawPartial();
    }

    void setColors(uint16_t fg, uint16_t bg, bool skipArtInvert) { 
        _fg = fg; _bg = bg; _skipArtInvert = skipArtInvert;
    }

    AlbumRecord selectedAlbum() const {
        if (_count == 0) return {};
        return _albums[_selected];
    }

private:
    Display*   _disp    = nullptr;
    MediaDB*   _db      = nullptr;
    FS_T*      _sd      = nullptr;
    const SystemSettings* _settings = nullptr;
    uint8_t*   _bgBitmap = nullptr;
    bool       _bgValid  = false;
    uint16_t   _fg      = GxEPD_WHITE;
    uint16_t   _bg      = GxEPD_BLACK;
    bool       _skipArtInvert = true;

    static const uint16_t MAX_ALBUMS = 32;
    AlbumRecord _albums[MAX_ALBUMS];
    uint16_t    _count = 0;
    uint16_t    _selected = 0;
    bool        _dirty = false;

    void _loadAlbums() {
        _count = _db->getAlbumsRecords(_albums, MAX_ALBUMS);
    }

    void _renderCarousel() {
        if (_count == 0) return;

        if (!_bgValid) {
            char artPath[256];
            ArtExtractor::getArtPath(_albums[_selected].artist, _albums[_selected].album, artPath, sizeof(artPath));
            if (!ArtExtractor::artExists(*_sd, artPath)) {
                ArtExtractor::extractFromFlac(*_sd, _albums[_selected].firstTrackPath, artPath);
            }
            ArtRenderer::renderTo(*_sd, artPath, _bgBitmap, 200, 200, ArtRenderMode::BACKGROUND);
            _bgValid = true;
        }

        // Color logic for art: Natural is 1=WHITE, 0=BLACK
        uint16_t artFG = GxEPD_WHITE;
        uint16_t artBG = GxEPD_BLACK;
        if (!_skipArtInvert) {
             artFG = GxEPD_BLACK; artBG = GxEPD_WHITE;
        }

        _disp->drawBitmap(0, 0, _bgBitmap, 200, 200, artFG, artBG);

        int prevIdx = (_selected + _count - 1) % _count;
        int nextIdx = (_selected + 1) % _count;

        _drawCover(_albums[prevIdx], 0, 20, 40, 120, ArtRenderMode::SIDE_LEFT);
        _drawCover(_albums[nextIdx], 160, 20, 40, 120, ArtRenderMode::SIDE_RIGHT);
        _drawCover(_albums[_selected], 40, 20, 120, 120, ArtRenderMode::NORMAL);
    }

    void _drawCover(const AlbumRecord& album, int16_t x, int16_t y, uint16_t w, uint16_t h, ArtRenderMode mode) {
        char artPath[256];
        ArtExtractor::getArtPath(album.artist, album.album, artPath, sizeof(artPath));

        if (!ArtExtractor::artExists(*_sd, artPath)) {
            ArtExtractor::extractFromFlac(*_sd, album.firstTrackPath, artPath);
        }

        size_t bufSize = ((w + 7) / 8) * h;
        uint8_t* buf = (uint8_t*)malloc(bufSize);
        if (!buf) return;

        uint16_t rowBytes = (w + 7) / 8;
        for (uint16_t i = 0; i < h; i++) {
            uint8_t* src = _bgBitmap + (y + i) * 25 + (x / 8);
            memcpy(buf + i * rowBytes, src, rowBytes);
        }

        uint16_t artFG = GxEPD_WHITE;
        uint16_t artBG = GxEPD_BLACK;
        if (!_skipArtInvert) {
             artFG = GxEPD_BLACK; artBG = GxEPD_WHITE;
        }

        if (ArtRenderer::renderTo(*_sd, artPath, buf, w, h, mode, false)) {
            _disp->drawBitmap(x, y, buf, w, h, artFG, artBG);
        } else {
            _disp->drawRect(x, y, w, h, _fg);
        }
        free(buf);
    }

    void _renderText() {
        if (_count == 0) return;
        const AlbumRecord& album = _albums[_selected];
        _disp->setFont(&FreeMonoBold9pt7b);
        _drawOutlineCenteredText(album.album, 165);
        _disp->setFont(&FreeMono9pt7b);
        _drawOutlineCenteredText(album.artist, 185);
    }

    void _drawOutlineCenteredText(const char* str, int16_t y) {
        int16_t x1, y1; uint16_t w, h;
        _disp->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
        int16_t x = (200 - w) / 2;

        _disp->setTextColor(_bg);
        _disp->setCursor(x - 1, y);     _disp->print(str);
        _disp->setCursor(x + 1, y);     _disp->print(str);
        _disp->setCursor(x, y - 1);     _disp->print(str);
        _disp->setCursor(x, y + 1);     _disp->print(str);
        _disp->setCursor(x - 1, y - 1); _disp->print(str);
        _disp->setCursor(x + 1, y + 1); _disp->print(str);

        _disp->setTextColor(_fg);
        _disp->setCursor(x, y);
        _disp->print(str);
    }
};
