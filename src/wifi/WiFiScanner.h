#pragma once
/**
 * WiFiScanner.h — WiFi Scanner with QWERTY Keyboard
 */
#include <Arduino.h>
#include <WiFi.h>

#define MAX_NETWORKS 20

struct WiFiNetwork {
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    uint8_t channel;
};

class WiFiKeyboard {
public:
    static constexpr int ROWS = 4;
    static constexpr int COLS = 10;
    
    static const char* getKey(int row, int col) {
        if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return NULL;
        return keyboard[row][col];
    }
    
private:
    static const char* keyboard[ROWS][COLS];
};

class WiFiScanner {
public:
    enum class ConnectStatus { IDLE, CONNECTING, CONNECTED, FAILED };
    
    WiFiScanner() {
        _networks = new WiFiNetwork[MAX_NETWORKS];
        _networkCount = 0;
        _connectStatus = ConnectStatus::IDLE;
    }
    
    ~WiFiScanner() {
        delete[] _networks;
    }
    
    void scanNetworks() {
        // Reset to STA mode before scanning
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, true);
        
        delay(100);
        
        int n = WiFi.scanNetworks(false, true, false, 300);
        
        _networkCount = min(n, MAX_NETWORKS);
        
        for (int i = 0; i < _networkCount; i++) {
            _networks[i].ssid = WiFi.SSID(i);
            _networks[i].rssi = WiFi.RSSI(i);
            _networks[i].encryptionType = WiFi.encryptionType(i);
            _networks[i].channel = WiFi.channel(i);
        }
        
        // Sort by RSSI (strongest first)
        for (int i = 0; i < _networkCount - 1; i++) {
            for (int j = i + 1; j < _networkCount; j++) {
                if (_networks[j].rssi > _networks[i].rssi) {
                    WiFiNetwork temp = _networks[i];
                    _networks[i] = _networks[j];
                    _networks[j] = temp;
                }
            }
        }
    }
    
    int getNetworkCount() { return _networkCount; }
    WiFiNetwork& getNetwork(int idx) { return _networks[idx]; }
    
    bool connect(const char* ssid, const char* password = "") {
        _connectStatus = ConnectStatus::CONNECTING;
        _lastError = "";
        
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);
        delay(100);
        
        if (password && strlen(password) > 0) {
            WiFi.begin(ssid, password);
        } else {
            WiFi.begin(ssid);
        }
        
        // Wait for connection
        int attempts = 0;
        while (attempts < 30) {
            if (WiFi.status() == WL_CONNECTED) {
                _connectStatus = ConnectStatus::CONNECTED;
                _ipAddress = WiFi.localIP().toString();
                return true;
            }
            delay(500);
            attempts++;
        }
        
        _connectStatus = ConnectStatus::FAILED;
        _lastError = WiFi.status() == WL_NO_SSID_AVAIL ? "No SSID" : "Timeout";
        return false;
    }
    
    ConnectStatus getStatus() { return _connectStatus; }
    String getIP() { return _ipAddress; }
    String getLastError() { return _lastError; }
    
    void disconnect() {
        WiFi.disconnect(true);
        _connectStatus = ConnectStatus::IDLE;
    }
    
private:
    WiFiNetwork* _networks;
    int _networkCount;
    ConnectStatus _connectStatus;
    String _ipAddress;
    String _lastError;
};

// QWERTY Keyboard Layout
const char* WiFiKeyboard::keyboard[WiFiKeyboard::ROWS][WiFiKeyboard::COLS] = {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
    {"A", "S", "D", "F", "G", "H", "J", "K", "L", "DEL"},
    {"Z", "X", "C", "V", "B", "N", "M", "-", "_", "."}
};
