#pragma once
/**
 * FileExplorerScene.h — Basic File Browser for Nebula OS
 */
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <FS.h>
#include <vector>
#include <algorithm>

struct ExplorerFileEntry {
    String name;
    bool isDir;
    uint32_t size;
};

class FileExplorerScene {
public:
    void init(GxEPD2_BW<GxEPD2_154_D67, 200>& disp, fs::FS& sd) {
        _disp = &disp;
        _sd = &sd;
        _currentPath = "/";
        _selectedIdx = 0;
        _scrollOffset = 0;
        _refreshList();
    }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            _disp->fillScreen(GxEPD_WHITE); // Explorer usually looks better in Light mode or keep consistent
            _render();
        } while (_disp->nextPage());
    }

    void drawPartial() {
        _disp->setPartialWindow(0, 0, 200, 200);
        _disp->firstPage();
        do {
            _disp->fillScreen(GxEPD_WHITE);
            _render();
        } while (_disp->nextPage());
    }

    void onUp() {
        if (_files.empty()) return;
        _selectedIdx = (_selectedIdx + _files.size() - 1) % _files.size();
        _updateScroll();
        drawPartial();
    }

    void onDown() {
        if (_files.empty()) return;
        _selectedIdx = (_selectedIdx + 1) % _files.size();
        _updateScroll();
        drawPartial();
    }

    bool onSelect() {
        if (_files.empty()) return false;
        ExplorerFileEntry& entry = _files[_selectedIdx];
        if (entry.name == "..") {
            // Go up
            int lastSlash = _currentPath.lastIndexOf('/', _currentPath.length() - 2);
            if (lastSlash < 0) _currentPath = "/";
            else _currentPath = _currentPath.substring(0, lastSlash + 1);
            _selectedIdx = 0;
            _scrollOffset = 0;
            _refreshList();
            drawFull();
        } else if (entry.isDir) {
            _currentPath += entry.name + "/";
            _selectedIdx = 0;
            _scrollOffset = 0;
            _refreshList();
            drawFull();
        }
        return false;
    }

    bool onBack() {
        if (_currentPath == "/") return true; // Exit explorer
        
        // Go up logic
        int lastSlash = _currentPath.lastIndexOf('/', _currentPath.length() - 2);
        if (lastSlash < 0) _currentPath = "/";
        else _currentPath = _currentPath.substring(0, lastSlash + 1);
        _selectedIdx = 0;
        _scrollOffset = 0;
        _refreshList();
        drawFull();
        return false;
    }

private:
    GxEPD2_BW<GxEPD2_154_D67, 200>* _disp;
    fs::FS* _sd;
    String _currentPath;
    std::vector<ExplorerFileEntry> _files;
    int _selectedIdx = 0;
    int _scrollOffset = 0;

    void _refreshList() {
        _files.clear();
        if (_currentPath != "/") {
            _files.push_back({"..", true, 0});
        }

        fs::File root = _sd->open(_currentPath);
        if (!root || !root.isDirectory()) return;

        fs::File file = root.openNextFile();
        while (file) {
            String name = String(file.name());
            // Files returned by SD_MMC sometimes include path, sometimes just name
            if (name.lastIndexOf('/') != -1) {
                name = name.substring(name.lastIndexOf('/') + 1);
            }
            _files.push_back({name, file.isDirectory(), (uint32_t)file.size()});
            file = root.openNextFile();
        }
        
        // Sort: Dirs first, then alphabet
        std::sort(_files.begin(), _files.end(), [](const ExplorerFileEntry& a, const ExplorerFileEntry& b) {
            if (a.name == "..") return true;
            if (b.name == "..") return false;
            if (a.isDir != b.isDir) return a.isDir;
            return a.name < b.name;
        });
    }

    void _updateScroll() {
        if (_selectedIdx < _scrollOffset) {
            _scrollOffset = _selectedIdx;
        } else if (_selectedIdx >= _scrollOffset + 7) {
            _scrollOffset = _selectedIdx - 6;
        }
    }

    void _render() {
        // Title bar
        _disp->fillRect(0, 0, 200, 25, GxEPD_BLACK);
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setTextColor(GxEPD_WHITE);
        _disp->setCursor(5, 18);
        String title = _currentPath;
        if (title.length() > 18) title = "..." + title.substring(title.length() - 15);
        _disp->print(title);

        _disp->setTextColor(GxEPD_BLACK);
        int y = 45;
        int itemsToShow = 7;
        for (int i = _scrollOffset; i < (int)_files.size() && i < _scrollOffset + itemsToShow; i++) {
            ExplorerFileEntry& fe = _files[i];
            
            if (i == _selectedIdx) {
                _disp->fillRect(0, y - 14, 200, 18, GxEPD_BLACK);
                _disp->setTextColor(GxEPD_WHITE);
            } else {
                _disp->setTextColor(GxEPD_BLACK);
            }

            _disp->setCursor(5, y);
            String prefix = fe.isDir ? ">" : " ";
            String name = fe.name;
            if (name.length() > 14) name = name.substring(0, 11) + "...";
            _disp->print(prefix + name);

            if (!fe.isDir) {
                String sz = _formatSize(fe.size);
                int16_t x1, y1; uint16_t w, h;
                _disp->getTextBounds(sz.c_str(), 0, 0, &x1, &y1, &w, &h);
                _disp->setCursor(195 - w, y);
                _disp->print(sz);
            }
            y += 22;
        }

        // Scroll indicators
        if (_scrollOffset > 0) _disp->fillTriangle(190, 32, 186, 36, 194, 36, GxEPD_BLACK);
        if (_scrollOffset + itemsToShow < (int)_files.size()) _disp->fillTriangle(190, 195, 186, 191, 194, 191, GxEPD_BLACK);
    }

    String _formatSize(uint32_t bytes) {
        if (bytes < 1024) return String(bytes) + "B";
        if (bytes < 1024 * 1024) return String(bytes / 1024) + "K";
        return String(bytes / (1024.0 * 1024.0), 1) + "M";
    }
};
