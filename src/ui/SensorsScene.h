#pragma once
/**
 * SensorsScene.h — Multi-level Sensor Dashboard for Nebula OS
 * Hierarchy: SENSORS → MPU6050/BMP388/ESP32/LIGHT/BATTERY
 *            MPU6050 → LIVE DATA / 3D CUBE / CONFIG
 */
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "../db/SettingsManager.h"
#include "../sensors/MPU6050Manager.h"

enum class SensorMenu {
    SENSOR_LIST,     // Top: MPU6050, BMP388, ESP32, Light, Battery
    MPU_MAIN,        // MPU sub: Live Data, 3D Cube, Config
    MPU_LIVE,        // Live sensor values
    MPU_CUBE,        // 3D wireframe cube
    MPU_CONFIG,      // Accel/Gyro range, inversion, DMP
    ESP_INFO,        // ESP32 system info
    LIGHT_INFO,      // Light sensor ADC
    BATTERY_INFO     // Battery voltage
};

class SensorsScene {
public:
    void init(GxEPD2_BW<GxEPD2_154_D67, 200>& disp, SystemSettings& settings, MPU6050Manager& mpu) {
        _disp = &disp;
        _settings = &settings;
        _mpu = &mpu;
        _menu = SensorMenu::SENSOR_LIST;
        _selectedIdx = 0;
        _cubeActive = false;
    }

    void drawFull() {
        _disp->setFullWindow();
        _disp->firstPage();
        do {
            _disp->fillScreen(_bg());
            _renderCurrentView();
        } while (_disp->nextPage());
    }

    void drawPartial() {
        _disp->setPartialWindow(0, 0, 200, 200);
        _disp->firstPage();
        do {
            _disp->fillScreen(_bg());
            _renderCurrentView();
        } while (_disp->nextPage());
    }

    void onUp() {
        if (_cubeActive) return; // no nav in cube mode
        if (_selectedIdx > 0) {
            _selectedIdx--;
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
    }

    void onDown() {
        if (_cubeActive) return;
        int max = _getMaxItems();
        if (_selectedIdx < max - 1) {
            _selectedIdx++;
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
    }

    // Returns true if we should exit to settings
    bool onBack() {
        if (_cubeActive) {
            _cubeActive = false;
            _menu = SensorMenu::MPU_MAIN;
            _selectedIdx = 1;
            drawFull();
            return false;
        }
        switch (_menu) {
            case SensorMenu::SENSOR_LIST:
                return true; // exit to settings
            case SensorMenu::MPU_MAIN:
            case SensorMenu::ESP_INFO:
            case SensorMenu::LIGHT_INFO:
            case SensorMenu::BATTERY_INFO:
                _menu = SensorMenu::SENSOR_LIST;
                _selectedIdx = 0;
                drawFull();
                return false;
            case SensorMenu::MPU_LIVE:
            case SensorMenu::MPU_CUBE:
            case SensorMenu::MPU_CONFIG:
                _menu = SensorMenu::MPU_MAIN;
                _selectedIdx = 0;
                drawFull();
                return false;
            default:
                return true;
        }
    }

    void onSelect() {
        if (_cubeActive) return;

        if (_menu == SensorMenu::SENSOR_LIST) {
            switch (_selectedIdx) {
                case 0: _menu = SensorMenu::MPU_MAIN;    break;
                case 1: /* BMP388 placeholder */ return;
                case 2: _menu = SensorMenu::ESP_INFO;    break;
                case 3: _menu = SensorMenu::LIGHT_INFO;  break;
                case 4: _menu = SensorMenu::BATTERY_INFO; break;
            }
            _selectedIdx = 0;
            drawFull();
        }
        else if (_menu == SensorMenu::MPU_MAIN) {
            switch (_selectedIdx) {
                case 0: _menu = SensorMenu::MPU_LIVE;   break;
                case 1:
                    _menu = SensorMenu::MPU_CUBE;
                    _cubeActive = true;
                    _lastCubeUpdate = 0;
                    drawFull();
                    return;
                case 2: _menu = SensorMenu::MPU_CONFIG;  break;
            }
            _selectedIdx = 0;
            drawFull();
        }
        else if (_menu == SensorMenu::MPU_CONFIG) {
            _handleConfigAdjust();
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
        // Live/ESP/Light/Battery are read-only, select refreshes data
        else if (_menu == SensorMenu::MPU_LIVE || _menu == SensorMenu::ESP_INFO ||
                 _menu == SensorMenu::LIGHT_INFO || _menu == SensorMenu::BATTERY_INFO) {
            if (_menu == SensorMenu::MPU_LIVE) _mpu->update();
            if (_settings->display.partialRefresh) drawPartial();
            else drawFull();
        }
    }

    // Called from loop() for live updates
    void tick(uint32_t now) {
        if (_cubeActive && _mpu->isConnected()) {
            if (now - _lastCubeUpdate > 250) { // ~4fps
                _lastCubeUpdate = now;
                _mpu->update();
                _drawCube();
            }
        }
    }

    bool isCubeActive() const { return _cubeActive; }

private:
    GxEPD2_BW<GxEPD2_154_D67, 200>* _disp;
    SystemSettings* _settings;
    MPU6050Manager* _mpu;
    SensorMenu _menu;
    int _selectedIdx = 0;
    bool _cubeActive = false;
    uint32_t _lastCubeUpdate = 0;

    uint16_t _bg() { return _settings->display.inverted ? GxEPD_BLACK : GxEPD_WHITE; }
    uint16_t _fg() { return _settings->display.inverted ? GxEPD_WHITE : GxEPD_BLACK; }

    // ── Max items per menu level ──────────────────────────────
    int _getMaxItems() {
        switch (_menu) {
            case SensorMenu::SENSOR_LIST: return 5;  // MPU6050, BMP388, ESP32, Light, Battery
            case SensorMenu::MPU_MAIN:    return 3;  // Live Data, 3D Cube, Config
            case SensorMenu::MPU_LIVE:    return 7;  // Ax,Ay,Az, Gx,Gy,Gz, Temp
            case SensorMenu::MPU_CONFIG:  return 6;  // AccelRange, GyroRange, InvX, InvY, InvZ, DMP
            case SensorMenu::ESP_INFO:    return 5;  // Freq, Heap, PSRAM, Uptime, RSSI
            case SensorMenu::LIGHT_INFO:  return 1;
            case SensorMenu::BATTERY_INFO: return 2; // Voltage, Percent
            default: return 0;
        }
    }

    // ── Render dispatcher ─────────────────────────────────────
    void _renderCurrentView() {
        if (_cubeActive) {
            _renderCubeView();
            return;
        }
        _renderMenuList();
    }

    // ── Standard list rendering (same pattern as SettingsScene) ──
    void _renderMenuList() {
        _disp->setFont(&FreeMonoBold9pt7b);

        // Header bar
        _disp->fillRect(0, 0, 200, 25, _fg());
        _disp->setTextColor(_bg());
        _disp->setCursor(5, 18);
        _disp->print(_getMenuTitle());
        _disp->setTextColor(_fg());

        // List items
        int y = 50;
        int items = _getMaxItems();
        for (int i = 0; i < items; i++) {
            if (i == _selectedIdx) {
                _disp->fillRect(0, y - 16, 200, 20, _fg());
                _disp->setTextColor(_bg());
            } else {
                _disp->setTextColor(_fg());
            }
            _disp->setCursor(10, y);
            _disp->print(_getItemLabel(i));

            // Value on the right
            String val = _getItemValue(i);
            if (val.length() > 0) {
                int16_t x1, y1; uint16_t w, h;
                _disp->getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
                _disp->setCursor(190 - w, y);
                _disp->print(val);
            }
            y += 24;
        }
    }

    // ── Menu titles ───────────────────────────────────────────
    const char* _getMenuTitle() {
        switch (_menu) {
            case SensorMenu::SENSOR_LIST:  return "SENSORS";
            case SensorMenu::MPU_MAIN:     return "MPU6050";
            case SensorMenu::MPU_LIVE:     return "MPU LIVE";
            case SensorMenu::MPU_CUBE:     return "3D CUBE";
            case SensorMenu::MPU_CONFIG:   return "MPU CONFIG";
            case SensorMenu::ESP_INFO:     return "ESP32 INFO";
            case SensorMenu::LIGHT_INFO:   return "LIGHT SENS";
            case SensorMenu::BATTERY_INFO: return "BATTERY";
            default: return "";
        }
    }

    // ── Item labels ───────────────────────────────────────────
    const char* _getItemLabel(int i) {
        switch (_menu) {
            case SensorMenu::SENSOR_LIST: {
                const char* items[] = {"MPU6050", "BMP388", "ESP32", "LIGHT", "BATTERY"};
                return items[i];
            }
            case SensorMenu::MPU_MAIN: {
                const char* items[] = {"LIVE DATA", "3D CUBE", "CONFIG"};
                return items[i];
            }
            case SensorMenu::MPU_LIVE: {
                const char* items[] = {"Accel X", "Accel Y", "Accel Z", "Gyro X", "Gyro Y", "Gyro Z", "Temp"};
                return items[i];
            }
            case SensorMenu::MPU_CONFIG: {
                const char* items[] = {"AccelRng", "GyroRng", "Invert X", "Invert Y", "Invert Z", "DMP"};
                return items[i];
            }
            case SensorMenu::ESP_INFO: {
                const char* items[] = {"CPU MHz", "Heap", "PSRAM", "Uptime", "WiFi dBm"};
                return items[i];
            }
            case SensorMenu::LIGHT_INFO: {
                return "ADC Val";
            }
            case SensorMenu::BATTERY_INFO: {
                const char* items[] = {"Voltage", "Percent"};
                return items[i];
            }
            default: return "";
        }
    }

    // ── Item values ───────────────────────────────────────────
    String _getItemValue(int i) {
        switch (_menu) {
            case SensorMenu::SENSOR_LIST:
                if (i == 0) return _mpu->isConnected() ? "OK" : "N/A";
                if (i == 1) return "N/C"; // BMP388 not connected
                if (i == 2) return "OK";
                if (i == 3) return "OK";
                if (i == 4) return "OK";
                return "";
            case SensorMenu::MPU_MAIN:
                return ">"; // sub-menu indicator
            case SensorMenu::MPU_LIVE: {
                _mpu->update();
                Vec3f a = _mpu->accel();
                Vec3f g = _mpu->gyro();
                switch (i) {
                    case 0: return String(a.x, 2);
                    case 1: return String(a.y, 2);
                    case 2: return String(a.z, 2);
                    case 3: return String(g.x, 1);
                    case 4: return String(g.y, 1);
                    case 5: return String(g.z, 1);
                    case 6: return String(_mpu->temp(), 1) + "C";
                }
                return "";
            }
            case SensorMenu::MPU_CONFIG:
                switch (i) {
                    case 0: return MPU6050Manager::accelRangeStr(_settings->mpu.accelRange);
                    case 1: return MPU6050Manager::gyroRangeStr(_settings->mpu.gyroRange);
                    case 2: return _settings->mpu.invertX ? "ON" : "OFF";
                    case 3: return _settings->mpu.invertY ? "ON" : "OFF";
                    case 4: return _settings->mpu.invertZ ? "ON" : "OFF";
                    case 5: return _settings->mpu.dmpEnabled ? "ON" : "OFF";
                }
                return "";
            case SensorMenu::ESP_INFO:
                switch (i) {
                    case 0: return String(ESP.getCpuFreqMHz());
                    case 1: return String(ESP.getFreeHeap() / 1024) + "KB";
                    case 2: return String(ESP.getFreePsram() / 1024) + "KB";
                    case 3: {
                        uint32_t s = millis() / 1000;
                        uint32_t m = s / 60; s %= 60;
                        uint32_t h = m / 60; m %= 60;
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", h, m, s);
                        return String(buf);
                    }
                    case 4: {
                        int rssi = WiFi.RSSI();
                        return (rssi == 0) ? "N/C" : String(rssi);
                    }
                }
                return "";
            case SensorMenu::LIGHT_INFO:
                return String(analogRead(13));
            case SensorMenu::BATTERY_INFO:
                // Placeholder — requires actual ADC pin for battery divider
                switch (i) {
                    case 0: return "3.85V";
                    case 1: return "78%";
                }
                return "";
            default: return "";
        }
    }

    // ── Config adjustment (cycle values) ──────────────────────
    void _handleConfigAdjust() {
        switch (_selectedIdx) {
            case 0: // Accel Range
                _settings->mpu.accelRange = (_settings->mpu.accelRange + 1) % 4;
                break;
            case 1: // Gyro Range
                _settings->mpu.gyroRange = (_settings->mpu.gyroRange + 1) % 4;
                break;
            case 2: _settings->mpu.invertX = !_settings->mpu.invertX; break;
            case 3: _settings->mpu.invertY = !_settings->mpu.invertY; break;
            case 4: _settings->mpu.invertZ = !_settings->mpu.invertZ; break;
            case 5: _settings->mpu.dmpEnabled = !_settings->mpu.dmpEnabled; break;
        }
        // Apply immediately
        _mpu->applySettings(_settings->mpu);
    }

    // ═══════════════════════════════════════════════════════════
    //  3D CUBE RENDERER
    // ═══════════════════════════════════════════════════════════

    // Cube vertices (unit cube centered at origin)
    static constexpr float _cubeVerts[8][3] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };

    // 12 edges as vertex index pairs
    static constexpr uint8_t _cubeEdges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},  // back face
        {4,5},{5,6},{6,7},{7,4},  // front face
        {0,4},{1,5},{2,6},{3,7}   // connecting edges
    };

    void _renderCubeView() {
        // Header
        _disp->setFont(&FreeMonoBold9pt7b);
        _disp->fillRect(0, 0, 200, 25, _fg());
        _disp->setTextColor(_bg());
        _disp->setCursor(5, 18);
        _disp->print("3D CUBE");

        // Info text at bottom
        _disp->setTextColor(_fg());
        _disp->setCursor(5, 195);
        _disp->setFont(nullptr); // small default font
        _disp->print("[4] BACK");

        _drawCubeWireframe();
    }

    void _drawCubeWireframe() {
        float p = _mpu->pitch();
        float r = _mpu->roll();

        float sp = sinf(p), cp = cosf(p);
        float sr = sinf(r), cr = cosf(r);

        // Project each vertex
        int16_t sx[8], sy[8];
        float scale = 50.0f;
        float cx = 100.0f, cy = 112.0f; // center of cube area (below header)

        for (int i = 0; i < 8; i++) {
            float x = _cubeVerts[i][0];
            float y = _cubeVerts[i][1];
            float z = _cubeVerts[i][2];

            // Rotate around X (pitch)
            float y1 = y * cp - z * sp;
            float z1 = y * sp + z * cp;

            // Rotate around Y (roll)
            float x2 = x * cr + z1 * sr;
            float z2 = -x * sr + z1 * cr;

            // Simple perspective
            float d = 4.0f + z2 * 0.3f;
            sx[i] = (int16_t)(cx + x2 * scale / d);
            sy[i] = (int16_t)(cy + y1 * scale / d);
        }

        // Draw edges
        for (int i = 0; i < 12; i++) {
            _disp->drawLine(
                sx[_cubeEdges[i][0]], sy[_cubeEdges[i][0]],
                sx[_cubeEdges[i][1]], sy[_cubeEdges[i][1]],
                _fg()
            );
        }
    }

    void _drawCube() {
        _mpu->update();
        // Partial refresh only the cube area (below header)
        _disp->setPartialWindow(0, 25, 200, 175);
        _disp->firstPage();
        do {
            _disp->fillRect(0, 25, 200, 175, _bg());

            _drawCubeWireframe();

            // Angle info
            _disp->setFont(nullptr);
            _disp->setTextColor(_fg());
            _disp->setCursor(5, 30);
            _disp->print("P:");
            _disp->print(String(_mpu->pitch() * 57.2958f, 1));
            _disp->setCursor(100, 30);
            _disp->print("R:");
            _disp->print(String(_mpu->roll() * 57.2958f, 1));

            _disp->setCursor(5, 195);
            _disp->print("[4] BACK");
        } while (_disp->nextPage());
    }
};

// Static member definitions
constexpr float SensorsScene::_cubeVerts[8][3];
constexpr uint8_t SensorsScene::_cubeEdges[12][2];
