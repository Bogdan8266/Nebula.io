#pragma once
#include <Arduino.h>
#include <FS.h>

enum PhaseInversionMode { PHASE_OFF, PHASE_LEFT, PHASE_RIGHT, PHASE_ALL };

struct AudioSettings {
    float    volume         = 0.05f;
    uint32_t sampleRate     = 44100;
    uint8_t  channels       = 2;
    uint8_t  bitsPerSample  = 16;
    uint16_t bufferSize     = 1024;
    uint16_t bufferCount    = 16;
    uint8_t  taskPriority   = 10;
    uint8_t  coreID         = 0;
    float    balance        = 0.0f; // -1.0 (Left) to 1.0 (Right)
    bool     backgroundMode = false; // Keep playing while navigating menus
    PhaseInversionMode phaseInversion = PHASE_OFF; // Phase inversion for psychoacoustic effects
};

struct DisplaySettings {
    bool     inverted       = false;
    bool     skipArtInvert  = true;
    bool     partialRefresh = true;
    bool     cfFullRefresh  = false;
    uint8_t  spiFreqMhz    = 4; // SPI clock: 2, 4, 8, 10, 20
    
    // Charge Pump Voltage Control (VSH1 in 0.1V units, range 24-170 = 2.4V-17V)
    uint8_t  vsh1Menu = 150;  // 15.0V for Menu
    uint8_t  vsh1Media = 150; // 15.0V for Media/NowPlaying
    uint8_t  vsh1Aod = 100;    // 10.0V for AOD (lower = less power)
};

struct UsbSettings {
    int      mode         = 1;     // 0=SERIAL, 1=STORAGE, 2=FLASH
    int      chunkSectors = 128;   // 64, 128, 256
    uint32_t freqKhz      = 40000; // 20000, 40000, 80000
};

struct MPU6050Settings {
    uint8_t  accelRange   = 0;     // 0=±2g, 1=±4g, 2=±8g, 3=±16g
    uint8_t  gyroRange    = 0;     // 0=±250, 1=±500, 2=±1000, 3=±2000 °/s
    bool     invertX      = false;
    bool     invertY      = false;
    bool     invertZ      = false;
    bool     dmpEnabled   = false;
};

struct RGBColor {
    uint8_t r = 255;
    uint8_t g = 100;
    uint8_t b = 50;
};

enum LEDMode { MODE_SOLID, MODE_KELVIN, MODE_RAINBOW };

struct LEDSettings {
    bool     enabled      = true;
    uint8_t  brightness   = 50;  // 0-100%
    uint8_t  mode         = MODE_KELVIN;
    uint16_t kelvin       = 3500;
    RGBColor solidColor;
    bool     smoothness   = true;
    uint8_t  fadeSpeed    = 50;  // 1-255
    uint16_t autoOffSec   = 300; // 5 mins
};

struct PowerSettings {
    uint16_t menuFreq    = 240; // Default max
    uint16_t musicFreq   = 160;
    uint16_t usbFreq     = 80;
};

struct SystemSettings {
    AudioSettings   audio;
    DisplaySettings display;
    UsbSettings     usb;
    MPU6050Settings mpu;
    LEDSettings     led;
    PowerSettings   power;
};

// Bump this whenever SystemSettings struct layout changes
static constexpr uint32_t SETTINGS_VERSION = 5;

struct SettingsFileHeader {
    uint32_t magic;   // 0xNEBULA01
    uint32_t version; // SETTINGS_VERSION
    uint32_t size;    // sizeof(SystemSettings)
};
static constexpr uint32_t SETTINGS_MAGIC = 0xEB010001;

class SettingsManager {
public:
    static bool load(fs::FS& sd, SystemSettings& settings) {
        fs::File file = sd.open("/Config/settings.bin", "r");
        if (!file) {
            Serial.println("[SET] Using defaults");
            return false;
        }
        // Read and validate header
        SettingsFileHeader hdr;
        if (file.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr) ||
            hdr.magic != SETTINGS_MAGIC ||
            hdr.version != SETTINGS_VERSION ||
            hdr.size != sizeof(SystemSettings)) {
            file.close();
            Serial.printf("[SET] Version mismatch (v%lu sz%lu != v%lu sz%lu) — using defaults\n",
                hdr.version, hdr.size, SETTINGS_VERSION, sizeof(SystemSettings));
            return false;
        }
        bool ok = (file.read((uint8_t*)&settings, sizeof(SystemSettings)) == sizeof(SystemSettings));
        file.close();
        if (ok) Serial.println("[SET] Loaded OK");
        return ok;
    }

    static bool save(fs::FS& sd, const SystemSettings& settings) {
        fs::File file = sd.open("/Config/settings.bin", "w");
        if (!file) {
            Serial.println("[SET] Save FAIL");
            return false;
        }
        SettingsFileHeader hdr{SETTINGS_MAGIC, SETTINGS_VERSION, sizeof(SystemSettings)};
        file.write((uint8_t*)&hdr, sizeof(hdr));
        bool ok = (file.write((uint8_t*)&settings, sizeof(SystemSettings)) == sizeof(SystemSettings));
        file.close();
        if (ok) Serial.println("[SET] Saved OK");
        return ok;
    }
};
