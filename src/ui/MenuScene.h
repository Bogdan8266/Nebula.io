#pragma once
/**
 * MenuScene.h — iPod-style library browser for Nebula OS
 *
 * Navigation: ARTISTS -> ALBUMS -> TRACKS -> NowPlaying
 *
 * Controls:
 *   1 = UP      2 = SELECT      3 = DOWN      4 = BACK
 */
#include <Arduino.h>
#include <FS.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "../db/MediaDB.h"
#include "StatusWidget.h"

// Layout constants
static constexpr uint16_t MN_W        = 200;
static constexpr uint16_t MN_H        = 200;
static constexpr uint16_t MN_HDR_H    = 42;    
static constexpr uint16_t MN_HINTS_H  = 22;    
static constexpr uint16_t MN_LIST_Y   = MN_HDR_H;
static constexpr uint16_t MN_LIST_H   = MN_H - MN_HDR_H - MN_HINTS_H;
static constexpr uint8_t  MN_ROW_H    = 26;    
static constexpr uint8_t  MN_VISIBLE  = MN_LIST_H / MN_ROW_H; 

enum class MenuState { ARTISTS, ALBUMS, TRACKS };

template<typename Display, typename FS_T = fs::FS>
class MenuScene {
public:
    StatusWidget* status = nullptr;

    void init(MediaDB& db, Display& disp, FS_T& sd, bool draw = true) {
        _db = &db; _disp = &disp; _sd = &sd;
        _state = MenuState::ARTISTS;
        if (draw) {
            _loadArtists();
            drawFull();
        }
    }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do { _renderAll(); } while (_disp->nextPage());
        _dirty = false;
    }

    void loadAlbumOnly(const char* artist, const char* album) {
        strncpy(_selArtist, artist, 63);
        strncpy(_selAlbum, album, 63);
        _state = MenuState::TRACKS;
        _selected = 0; _scroll = 0;
        _loadTracks();
        drawFull();
    }

    void updateListIfDirty() {
        if (!_dirty) return;
        _disp->setPartialWindow(0, MN_LIST_Y, MN_W, MN_LIST_H);
        _disp->firstPage();
        do {
            _disp->fillScreen(GxEPD_BLACK);
            _renderList();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void onUp() {
        if (_selected > 0) { _selected--; _clampScroll(); _dirty = true; }
    }
    void onDown() {
        if (_selected + 1 < _count) { _selected++; _clampScroll(); _dirty = true; }
    }

    bool onSelect() {
        if (_count == 0) return false;
        switch (_state) {
            case MenuState::ARTISTS:
                strncpy(_selArtist, _items[_selected], 63);
                _state = MenuState::ALBUMS;
                _selected = 0; _scroll = 0;
                _loadAlbums();
                drawFull();
                return false;

            case MenuState::ALBUMS:
                strncpy(_selAlbum, _items[_selected], 63);
                _state = MenuState::TRACKS;
                _selected = 0; _scroll = 0;
                _loadTracks();
                drawFull();
                return false;

            case MenuState::TRACKS:
                _selectedTrack = _tracks[_selected];
                return true;
        }
        return false;
    }

    void onBack() {
        if (_state == MenuState::ALBUMS) {
            _state = MenuState::ARTISTS; _selected = 0; _scroll = 0;
            _loadArtists(); drawFull();
        } else if (_state == MenuState::TRACKS) {
            _state = MenuState::ALBUMS; _selected = 0; _scroll = 0;
            _loadAlbums(); drawFull();
        }
    }

    void setColors(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; }

    TrackRecord selectedTrack() const { return _selectedTrack; }

private:
    Display*   _disp    = nullptr;
    MediaDB*   _db      = nullptr;
    FS_T*      _sd      = nullptr;
    MenuState  _state   = MenuState::ARTISTS;
    bool       _dirty   = false;
    uint16_t   _fg      = GxEPD_WHITE;
    uint16_t   _bg      = GxEPD_BLACK;

    static const uint8_t MAX_ITEMS = 16;
    char     _items[MAX_ITEMS][64];
    uint8_t  _count    = 0;
    uint8_t  _selected = 0;
    uint8_t  _scroll   = 0;  

    TrackRecord _tracks[MAX_ITEMS];
    char _selArtist[64] = {};
    char _selAlbum[64]  = {};
    TrackRecord _selectedTrack = {};

    void _loadArtists() {
        _count = (uint8_t)min((int)_db->getArtists(_items, MAX_ITEMS), (int)MAX_ITEMS);
    }

    void _loadAlbums() {
        _count = (uint8_t)min((int)_db->getAlbums(_selArtist, _items, MAX_ITEMS), (int)MAX_ITEMS);
    }

    void _loadTracks() {
        _count = (uint8_t)min((int)_db->getAlbumTracks(_selArtist, _selAlbum, _tracks, MAX_ITEMS), (int)MAX_ITEMS);
        for (uint8_t i = 0; i < _count; i++) {
            char buf[64];
            if (_tracks[i].trackNum > 0)
                snprintf(buf, sizeof(buf), "%02u. %s", _tracks[i].trackNum, _tracks[i].title);
            else
                strncpy(buf, _tracks[i].title, 63);
            strncpy(_items[i], buf, 63); _items[i][63]=0;
        }
    }

    void _clampScroll() {
        if (_selected < _scroll) _scroll = _selected;
        if (_selected >= _scroll + MN_VISIBLE) _scroll = _selected - MN_VISIBLE + 1;
    }

    void _renderAll() {
        _disp->fillScreen(_bg);
        _renderHeader();
        _renderList();
        _renderHints();
    }

    void _renderHeader() {
        if (status) status->drawGlobal(*_disp, 0, 14, MN_W, _fg, _bg);

        _disp->fillRect(0, 18, MN_W, 20, _fg);
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setTextColor(_bg);
        _disp->setCursor(3, 34);

        const char* title = "ARTISTS";
        if (_state == MenuState::ALBUMS) title = _selArtist;
        if (_state == MenuState::TRACKS) title = _selAlbum;

        char buf[24];
        strncpy(buf, title, 20); buf[20]=0;
        _disp->print(buf);

        char cnt[12];
        snprintf(cnt, sizeof(cnt), "| %u", _count);
        _disp->setCursor(MN_W - 55, 34);
        _disp->print(cnt);

        _disp->drawFastHLine(0, MN_HDR_H - 1, MN_W, _fg);
    }

    void _renderList() {
        _disp->setFont(&FreeMono9pt7b);
        for (uint8_t vi = 0; vi < MN_VISIBLE; vi++) {
            uint8_t idx = _scroll + vi;
            if (idx >= _count) break;
            uint16_t rowY = MN_LIST_Y + vi * MN_ROW_H;
            bool selected = (idx == _selected);

            if (selected) {
                _disp->fillRect(0, rowY, MN_W, MN_ROW_H - 1, _fg);
                _disp->setTextColor(_bg);
            } else {
                _disp->setTextColor(_fg);
            }
            _disp->setCursor(5, rowY + MN_ROW_H - 7);
            char buf[26];
            strncpy(buf, _items[idx], 25); buf[25]=0;
            _disp->print(buf);
        }

        if (_count > MN_VISIBLE) {
            uint16_t barH = (uint32_t)MN_LIST_H * MN_VISIBLE / _count;
            uint16_t barY = MN_LIST_Y + (uint32_t)MN_LIST_H * _scroll / _count;
            _disp->fillRect(MN_W - 3, barY, 2, barH, _fg);
        }
    }

    void _renderHints() {
        _disp->drawFastHLine(0, MN_H - MN_HINTS_H, MN_W, _fg);
        _disp->setFont(&FreeMono9pt7b);
        _disp->setTextColor(_fg);
        _disp->setCursor(5, MN_H - 6);
        _disp->print("UP  SEL  DN  BACK");
    }
};
