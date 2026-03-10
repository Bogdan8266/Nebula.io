#pragma once
/**
 * WiFiScene.h — WiFi Network Scanner UI with QWERTY Keyboard
 */
#include <Arduino.h>
#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "../db/SettingsManager.h"
#include "../wifi/WiFiScanner.h"
#include "../wifi/WiFiQRCode.h"

enum class WiFiMode { IDLE, SCANNING, SCAN, PASSWORD, CONNECTING, CONNECTED, FAILED, INFO, QRCODE };

class WiFiScene {
public:
    void init(GxEPD2_BW<GxEPD2_154_D67, 200>& disp, fs::FS& sd, SystemSettings& settings, WiFiScanner& scanner) {
        _disp = &disp;
        _sd = &sd;
        _settings = &settings;
        _scanner = &scanner;
        _mode = WiFiMode::IDLE;
        _selectedNet = 0;
        _dirty = true;
    }

    void startScan() {
        _mode = WiFiMode::SCANNING;
        _dirty = true;
        drawFull();
        
        // Start scanning in background
        _scanner->scanNetworks();
        _mode = WiFiMode::SCAN;
        _selectedNet = 0;
        _dirty = true;
        drawFull();
    }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            _disp->fillScreen(_settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE);
            _render();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void drawPartial() {
        _disp->setPartialWindow(0, 0, 200, 200);
        _disp->firstPage();
        do {
            _disp->fillScreen(_settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE);
            _render();
        } while (_disp->nextPage());
        _dirty = false;
    }

    void onUp() {
        if (_mode == WiFiMode::SCAN) {
            int max = _scanner->getNetworkCount();
            if (max > 0) {
                _selectedNet = (_selectedNet - 1 + max) % max;
                _dirty = true;
                drawPartial();
            }
        } else if (_mode == WiFiMode::PASSWORD) {
            _kbRow = (_kbRow - 1 + WiFiKeyboard::ROWS) % WiFiKeyboard::ROWS;
            _dirty = true;
            drawPartial();
        } else if (_mode == WiFiMode::CONNECTED) {
            // Navigate menu: 0=QR Code, 1=Details
            _selectedQR = (_selectedQR - 1 + 2) % 2;
            _dirty = true;
            drawPartial();
        }
    }

    void onDown() {
        if (_mode == WiFiMode::SCAN) {
            int max = _scanner->getNetworkCount();
            if (max > 0) {
                _selectedNet = (_selectedNet + 1) % max;
                _dirty = true;
                drawPartial();
            }
        } else if (_mode == WiFiMode::PASSWORD) {
            _kbRow = (_kbRow + 1) % WiFiKeyboard::ROWS;
            _dirty = true;
            drawPartial();
        } else if (_mode == WiFiMode::CONNECTED) {
            // Navigate menu: 0=QR Code, 1=Details
            _selectedQR = (_selectedQR + 1) % 2;
            _dirty = true;
            drawPartial();
        }
    }

    void onLeft() {
        if (_mode == WiFiMode::PASSWORD) {
            _kbCol = (_kbCol - 1 + WiFiKeyboard::COLS) % WiFiKeyboard::COLS;
            _dirty = true;
            drawPartial();
        }
    }

    void onDelete() {
        if (_mode == WiFiMode::PASSWORD) {
            // Delete character
            if (_password.length() > 0) {
                _password.remove(_password.length() - 1);
                _dirty = true;
                drawPartial();
            }
        }
    }

    void onInput() {
        if (_mode == WiFiMode::PASSWORD) {
            // Add character
            const char* key = WiFiKeyboard::getKey(_kbRow, _kbCol);
            if (key != NULL && _password.length() < 63) {
                _password += key;
                _dirty = true;
                drawPartial();
            }
        } else if (_mode == WiFiMode::CONNECTED) {
            // Button 2 = QR Code, Button 3 = Details
            if (_selectedQR == 0) {
                // Go to QR Code
                _qrGenerated = false;
                _mode = WiFiMode::QRCODE;
                drawFull();
            } else {
                // Go to Details
                _mode = WiFiMode::INFO;
                drawFull();
            }
        }
    }

    bool onSelect() {
        if (_mode == WiFiMode::SCAN) {
            // Check if this is a saved network - use saved password if matches
            WiFiNetwork& net = _scanner->getNetwork(_selectedNet);
            if (net.ssid.equals(_settings->wifi.savedSSID) && strlen(_settings->wifi.savedPassword) > 0) {
                // Use saved password - go directly to connect
                _password = _settings->wifi.savedPassword;
                startConnect();
            } else {
                // Go to password input
                _mode = WiFiMode::PASSWORD;
                _password = "";
                _kbRow = 0;
                _kbCol = 0;
                drawFull();
            }
            return false;
        } else if (_mode == WiFiMode::PASSWORD) {
            // Add character
            const char* key = WiFiKeyboard::getKey(_kbRow, _kbCol);
            if (key != NULL && _password.length() < 63) {
                _password += key;
                _dirty = true;
                drawPartial();
            }
            return false;
        } else if (_mode == WiFiMode::FAILED) {
            // Return to scan
            startScan();
            return false;
        } else if (_mode == WiFiMode::INFO) {
            // Back to scan
            startScan();
            return false;
        }
        return false;
    }
    
    // Handle button 4 (right button)
    bool onRight() {
        if (_mode == WiFiMode::SCAN) {
            // Show INFO if has saved credentials
            if (_settings->wifi.savedSSID[0] != '\0') {
                _mode = WiFiMode::INFO;
                drawFull();
            }
            return false;
        }
        return false;
    }
    
    // Handle button 5 (action button)
    bool onAction() {
        if (_mode == WiFiMode::SCAN) {
            // Show QR code if WiFi is connected and has saved credentials
            if (WiFi.status() == WL_CONNECTED && _settings->wifi.savedSSID[0] != '\0') {
                _qrGenerated = false;
                _mode = WiFiMode::QRCODE;
                drawFull();
            }
            return false;
        }
        return false;
    }

    bool onBack() {
        if (_mode == WiFiMode::PASSWORD) {
            // Delete character
            if (_password.length() > 0) {
                _password.remove(_password.length() - 1);
                _dirty = true;
                drawPartial();
            }
            return false;
        } else if (_mode == WiFiMode::QRCODE) {
            // Back to connected menu
            _selectedQR = 0;
            _mode = WiFiMode::CONNECTED;
            drawFull();
            return false;
        } else if (_mode == WiFiMode::SCAN || _mode == WiFiMode::CONNECTED || _mode == WiFiMode::FAILED || _mode == WiFiMode::INFO) {
            // Go back to settings
            return true;
        }
        return true;
    }

    // Rescan networks
    void rescan() {
        startScan();
    }

    // Show WiFi info
    void showInfo() {
        // Always show info if we have saved credentials
        if (_settings->wifi.savedSSID[0] != '\0') {
            _mode = WiFiMode::INFO;
            drawFull();
        }
    }

    // Show QR Code
    void showQRCode() {
        // Show QR code if has saved credentials
        if (_settings->wifi.savedSSID[0] != '\0') {
            _qrGenerated = false;
            _mode = WiFiMode::QRCODE;
            drawFull();
        }
    }

    void startConnect() {
        if (_scanner->getNetworkCount() == 0) return;
        
        WiFiNetwork& net = _scanner->getNetwork(_selectedNet);
        
        // Check if password needed
        if (net.encryptionType != WIFI_AUTH_OPEN && _password.length() == 0) {
            _mode = WiFiMode::PASSWORD;
            drawFull();
            return;
        }
        
        _mode = WiFiMode::CONNECTING;
        drawFull();
        
        // Connect
        bool success = _scanner->connect(net.ssid.c_str(), _password.c_str());
        
        if (success) {
            _mode = WiFiMode::CONNECTED;
            // Save credentials
            strncpy(_settings->wifi.savedSSID, net.ssid.c_str(), 31);
            strncpy(_settings->wifi.savedPassword, _password.c_str(), 63);
            SettingsManager::save(*_sd, *_settings);
        } else {
            _mode = WiFiMode::FAILED;
        }
        drawFull();
    }

    bool isInScanMode() {
        return _mode == WiFiMode::SCAN;
    }

    bool tick() {
        if (_mode == WiFiMode::CONNECTING) {
            if (_scanner->getStatus() == WiFiScanner::ConnectStatus::CONNECTED) {
                _mode = WiFiMode::CONNECTED;
                drawFull();
                return true;
            } else if (_scanner->getStatus() == WiFiScanner::ConnectStatus::FAILED) {
                _mode = WiFiMode::FAILED;
                drawFull();
                return true;
            }
        }
        return false;
    }

private:
    GxEPD2_BW<GxEPD2_154_D67, 200>* _disp = nullptr;
    fs::FS* _sd = nullptr;
    SystemSettings* _settings = nullptr;
    WiFiScanner* _scanner = nullptr;
    WiFiMode _mode = WiFiMode::IDLE;
    int _selectedNet = 0;
    String _password;
    int _kbRow = 0;
    int _kbCol = 0;
    int _selectedQR = 0;  // 0 = QR Code, 1 = Details
    bool _dirty = true;
    
    // QR code buffer (70 bytes for version 3 QR code)
    uint8_t _qrBuffer[70];
    bool _qrGenerated = false;

    void _render() {
        uint16_t bg = _settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE;
        uint16_t fg = _settings->display.inverted ? GxEPD_WHITE : GxEPD_BLACK;
        
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->fillRect(0, 0, 200, 25, fg);
        _disp->setTextColor(bg);
        _disp->setCursor(5, 18);
        
        switch(_mode) {
            case WiFiMode::IDLE: _disp->print("WiFi"); break;
            case WiFiMode::SCANNING: _disp->print("SCANNING..."); break;
            case WiFiMode::SCAN: _disp->print("WiFi SCAN"); break;
            case WiFiMode::PASSWORD: _disp->print("PASSWORD"); break;
            case WiFiMode::CONNECTING: _disp->print("CONNECTING"); break;
            case WiFiMode::CONNECTED: _disp->print("WiFi INFO"); break;
            case WiFiMode::FAILED: _disp->print("FAILED"); break;
            case WiFiMode::INFO: _disp->print("WiFi INFO"); break;
            case WiFiMode::QRCODE: _disp->print("WiFi QR"); break;
        }
        _disp->setTextColor(fg);

        if (_mode == WiFiMode::SCANNING) _renderScanning(bg, fg);
        else if (_mode == WiFiMode::SCAN) _renderScanList(bg, fg);
        else if (_mode == WiFiMode::PASSWORD) _renderKeyboard(bg, fg);
        else if (_mode == WiFiMode::CONNECTING) _renderConnecting(bg, fg);
        else if (_mode == WiFiMode::CONNECTED) _renderConnected(bg, fg);
        else if (_mode == WiFiMode::FAILED) _renderFailed(bg, fg);
        else if (_mode == WiFiMode::INFO) _renderInfo(bg, fg);
        else if (_mode == WiFiMode::QRCODE) _renderQRCode(bg, fg);
    }

    void _renderScanning(uint16_t bg, uint16_t fg) {
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setCursor(10, 60);
        _disp->print("Scanning");
        
        // Animation spinner with long dash
        static uint32_t lastAnim = 0;
        static uint8_t frame = 0;
        uint32_t now = millis();
        if (now - lastAnim > 300) {
            lastAnim = now;
            frame = (frame + 1) % 4;
        }
        const char* spins = "―\\|/";
        _disp->setCursor(85, 60);
        _disp->print(spins[frame]);
    }

    void _renderScanList(uint16_t bg, uint16_t fg) {
        int count = _scanner->getNetworkCount();
        if (count == 0) {
            _disp->setCursor(10, 50);
            _disp->print("No networks");
            _disp->setCursor(10, 75);
            _disp->print("[1] Rescan");
            return;
        }
        
        int start = max(0, _selectedNet - 2);
        int end = min(count, start + 6);
        int y = 40;
        
        for (int i = start; i < end; i++) {
            WiFiNetwork& net = _scanner->getNetwork(i);
            if (i == _selectedNet) {
                _disp->fillRect(0, y - 14, 200, 18, fg);
                _disp->setTextColor(bg);
            } else {
                _disp->setTextColor(fg);
            }
            _disp->setCursor(5, y);
            _disp->print(net.ssid.substring(0, 18));
            y += 20;
        }
        
        _disp->setFont(&FreeMono9pt7b);
        _disp->setCursor(10, 170);
        _disp->print("[1]v [3]^ [2]SEL");
        
        // Show options if WiFi is connected or has saved credentials
        if (_settings->wifi.savedSSID[0] != '\0') {
            if (WiFi.status() == WL_CONNECTED) {
                _disp->setCursor(10, 190);
                _disp->print("[5] QR");
            }
            _disp->setCursor(100, 190);
            _disp->print("[4] INFO");
        }
    }

    void _renderKeyboard(uint16_t bg, uint16_t fg) {
        if (_scanner->getNetworkCount() == 0) return;
        
        WiFiNetwork& net = _scanner->getNetwork(_selectedNet);
        _disp->setCursor(5, 40);
        _disp->setFont(&FreeMono9pt7b);
        _disp->print(net.ssid);
        _disp->setCursor(5, 60);
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->print(_password);
        _disp->print("_");
        
        int keyW = 20, keyH = 18;
        for (int row = 0; row < WiFiKeyboard::ROWS; row++) {
            for (int col = 0; col < WiFiKeyboard::COLS; col++) {
                const char* key = WiFiKeyboard::getKey(row, col);
                if (key == NULL) continue;
                int x = col * keyW;
                int y = 80 + row * keyH;
                if (row == _kbRow && col == _kbCol) {
                    _disp->fillRect(x, y, keyW-1, keyH-1, fg);
                    _disp->setTextColor(bg);
                } else {
                    _disp->drawRect(x, y, keyW-1, keyH-1, fg);
                    _disp->setTextColor(fg);
                }
                _disp->setFont(&FreeMono9pt7b);
                _disp->setCursor(x+3, y+13);
                _disp->print(key);
            }
        }
        
        _disp->setFont(&FreeMono9pt7b);
        _disp->setCursor(5, 165);
        _disp->print("[2]> [1]v [3]OK");
        
        _disp->setCursor(5, 185);
        _disp->print("[4]DEL [5]OK");
    }

    void _renderConnecting(uint16_t bg, uint16_t fg) {
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setCursor(10, 60);
        _disp->print("Connecting");
        
        // Animation spinner with long dash
        static uint32_t lastAnim = 0;
        static uint8_t frame = 0;
        uint32_t now = millis();
        if (now - lastAnim > 300) {
            lastAnim = now;
            frame = (frame + 1) % 4;
        }
        const char* spins = "―\\|/";
        _disp->setCursor(85, 60);
        _disp->print(spins[frame]);
        
        if (_scanner->getNetworkCount() > 0) {
            WiFiNetwork& net = _scanner->getNetwork(_selectedNet);
            _disp->setCursor(10, 90);
            _disp->print(net.ssid);
        }
    }

    void _renderConnected(uint16_t bg, uint16_t fg) {
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setCursor(10, 40);
        _disp->print("CONNECTED!");
        
        // Show SSID
        _disp->setCursor(10, 65);
        _disp->print(_settings->wifi.savedSSID);
        
        // Show IP
        _disp->setFont(&FreeMono9pt7b);
        _disp->setCursor(10, 85);
        _disp->print(_scanner->getIP());
        
        _disp->setTextColor(fg);
        _disp->setCursor(10, 120);
        _disp->print("[2] SCAN");
        _disp->setCursor(10, 140);
        _disp->print("[4] BACK");
    }

    void _renderFailed(uint16_t bg, uint16_t fg) {
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setCursor(10, 50);
        _disp->print("FAILED!");
        _disp->setCursor(10, 80);
        _disp->print(_scanner->getLastError());
        _disp->setFont(&FreeMono9pt7b);
        _disp->setCursor(10, 120);
        _disp->print("[5] Retry");
        _disp->setCursor(10, 140);
        _disp->print("[4] Back");
    }

    void _renderInfo(uint16_t bg, uint16_t fg) {
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->setCursor(10, 40);
        _disp->print("SSID:");
        _disp->setCursor(10, 60);
        _disp->print(_settings->wifi.savedSSID);
        
        _disp->setCursor(10, 85);
        _disp->print("PASS:");
        _disp->setCursor(10, 105);
        // Show password without mask
        _disp->print(_settings->wifi.savedPassword);
        
        // Check if connected
        if (WiFi.status() == WL_CONNECTED) {
            _disp->setCursor(10, 130);
            _disp->print("IP:");
            _disp->setCursor(10, 150);
            _disp->print(_scanner->getIP());
            
            int32_t rssi = WiFi.RSSI();
            _disp->setCursor(100, 130);
            _disp->print(String(rssi) + " dBm");
        } else {
            _disp->setCursor(10, 130);
            _disp->print("Status: OFFLINE");
        }
        
        _disp->setFont(&FreeMono9pt7b);
        _disp->setCursor(10, 175);
        _disp->print("[2] SCAN [4] EXIT");
    }

    void _renderQRCode(uint16_t bg, uint16_t fg) {
        // Generate QR code if not done yet
        static QRCode qrcode;
        if (!_qrGenerated) {
            WiFiQRCode::generate(
                _settings->wifi.savedSSID,
                _settings->wifi.savedPassword,
                false,  // not open network
                _qrBuffer
            );
            // Re-initialize the QRCode struct from buffer
            qrcode_initText(&qrcode, _qrBuffer, 3, 0, 
                WiFiQRCode::generateWiFiString(_settings->wifi.savedSSID, _settings->wifi.savedPassword, false).c_str());
            _qrGenerated = true;
        }
        
        // Get QR size from struct
        const int qrSize = qrcode.size;
        
        // Calculate QR code display size - fit in available space
        // Display is 200x200, header takes 25px, bottom text takes ~20px
        const int displaySize = 150;
        const int moduleSize = displaySize / qrSize;
        const int offsetX = (200 - displaySize) / 2;
        const int offsetY = 28;
        
        // Draw QR code
        for (int y = 0; y < qrSize; y++) {
            for (int x = 0; x < qrSize; x++) {
                // Get module state from QR data
                bool module = qrcode_getModule(&qrcode, x, y);
                
                int px = offsetX + x * moduleSize;
                int py = offsetY + y * moduleSize;
                
                if (module) {
                    _disp->fillRect(px, py, moduleSize, moduleSize, fg);
                }
            }
        }
        
        // Draw border around QR
        _disp->drawRect(offsetX - 1, offsetY - 1, displaySize + 2, displaySize + 2, fg);
        
        // Instructions
        _disp->setFont(&FreeMono9pt7b);
        _disp->setCursor(5, 192);
        _disp->print("[4] BACK");
    }
};
