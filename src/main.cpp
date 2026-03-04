/**
 * ╔══════════════════════════════════════════════════╗
 * ║           N E B U L A   O S   v0.1              ║
 * ║    Hi-Fi Modular Cyberdeck Player Firmware       ║
 * ║    Phase 3: Library Menu + Album Art             ║
 * ╚══════════════════════════════════════════════════╝
 *
 * Controls (Serial Monitor 115200):
 *   Menu:       1=UP  2=SELECT  3=DOWN  4=BACK
 *   NowPlaying: 1=PREV  2=PLAY  3=NEXT  4=→Menu
 *   r = Force rescan    d = Dump library
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <FS.h>
#include <SD_MMC.h>

#include "input/SerialInput.h"
#include "ui/StatusWidget.h"
#include "ui/NowPlaying.h"
#include "ui/MenuScene.h"
#include "ui/CoverFlowScene.h"
#include "ui/ArtRenderer.h"
#include "ui/MainMenuScene.h"
#include "db/MediaDB.h"
#include "db/MediaScanner.h"
#include "web/WebManager.h"
#include "web/UsbMscManager.h"
#include "audio/AudioPlayer.h"

#include "ui/SettingsScene.h"
#include "ui/SensorsScene.h"
#include "db/SettingsManager.h"
#include "sensors/MPU6050Manager.h"

// ── Wi-Fi ────────────────────────────────────────────────────────
#define WIFI_SSID "Xiaomi_14F8"
#define WIFI_PASS "123456789042"

// ── Pins ─────────────────────────────────────────────────────────
#define EPD_CS    10
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  18

#define SD_MMC_CLK 14
#define SD_MMC_CMD 15
#define SD_MMC_D0  2

// ── Display ───────────────────────────────────────────────────────
using Display = GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>;
Display display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ── SD + DB ───────────────────────────────────────────────────────
fs::FS&      sd = SD_MMC;
MediaDB      mediaDB;
MediaScanner scanner;

// ── Settings ──────────────────────────────────────────────────────
SystemSettings sysSettings;
SettingsScene  settingsScene;
SensorsScene   sensorsScene;
MPU6050Manager mpuManager;

// ── Scenes ────────────────────────────────────────────────────────
MenuScene<Display>    menu;
CoverFlowScene<Display> coverFlow;
MainMenuScene<Display, fs::FS> mainMenu;
StatusWidget          systemStatus;
NowPlaying<Display>   nowPlaying(display);

enum class AppState { BOOT, MAIN_MENU, COVER_FLOW, SONG_LIST, NOW_PLAYING, SETTINGS, USB_SYNC, SENSORS };
AppState appState = AppState::BOOT;
NebulaPlayer audioPlayer;
WebManager   webManager(sd);

// Dynamic coloring based on settings
#define CL_BG (sysSettings.display.inverted ? GxEPD_BLACK : GxEPD_WHITE)
#define CL_FG (sysSettings.display.inverted ? GxEPD_WHITE : GxEPD_BLACK)

// К-ть треків у поточному альбомі та поточний індекс для PREV/NEXT
static TrackRecord  currentAlbumTracks[8]; // Reduced from 16 to save 4KB
static uint16_t     currentAlbumTrackCount = 0;
static int16_t      currentTrackIdx        = -1;

// ─────────────────────────────────────────────────────────────────
//  Memory Management for NowPlaying Background
// ─────────────────────────────────────────────────────────────────
static uint8_t* currentNpBgBitmap = nullptr;

void clearNpBgBitmap() {
    if (currentNpBgBitmap) {
        free(currentNpBgBitmap);
        currentNpBgBitmap = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────
//  Boot log (terminal style)
// ─────────────────────────────────────────────────────────────────
static int  logLine  = 0;
static const int LINE_H   = 14;
static const int MARGIN_X =  4;
static const int MARGIN_Y = 12;

void epd_log(const char* text) {
    if (logLine > 11) return;
    int y = MARGIN_Y + logLine++ * LINE_H;
    display.setPartialWindow(0, y - LINE_H + 2, 200, LINE_H);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(MARGIN_X, y);
        display.print(text);
    } while (display.nextPage());
}

void printBanner() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(MARGIN_X, MARGIN_Y);         display.print("NEBULA OS v0.1");
        display.setCursor(MARGIN_X, MARGIN_Y + LINE_H); display.print("----------------");
    } while (display.nextPage());
    logLine = 2;
}

// ─────────────────────────────────────────────────────────────────
//  SD + MediaDB
// ─────────────────────────────────────────────────────────────────
bool initSD() {
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    // Use 20MHz for stability in 1-bit mode, especially for MSC
    return SD_MMC.begin("/sd", true, false, 20000); 
}

void initMediaDB(bool forceRescan = false) {
    mediaDB.close();
    if (!forceRescan && mediaDB.open(sd)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "DB: %lu tracks", mediaDB.trackCount());
        Serial.println(buf); epd_log(buf);
        return;
    }
    Serial.println("[DB] Building library...");
    epd_log("SCANNING SD...");

    uint32_t t0 = millis();
    uint32_t count = scanner.scan(sd, "/music");
    uint32_t elapsed = (millis() - t0) / 1000;

    char buf[48];
    if (count > 0) {
        snprintf(buf, sizeof(buf), "DB: %lu tracks (%lus)", count, elapsed);
        Serial.println(buf); epd_log(buf);
        mediaDB.open(sd);
    } else {
        epd_log("NO MUSIC FOUND!");
        Serial.println("[DB] No music found");
    }
}

// ─────────────────────────────────────────────────────────────────
//  Scene transitions
// ─────────────────────────────────────────────────────────────────

void switchToNowPlaying(const TrackRecord& track, int16_t trackIdx = -1) {
    appState = AppState::NOW_PLAYING;
    if (trackIdx >= 0) currentTrackIdx = trackIdx;

    systemStatus.setTrack(track.title);
    systemStatus.setPlaying(true);
    
    nowPlaying.setTrack(track.title, track.artist);
    nowPlaying.setPlaying(true);
    nowPlaying.status.setSD(true);
    nowPlaying.status.setBattery(3.85f);

    // Спробувати завантажити album art
    bool hasArt = false;
    char artPath[256] = {};
    if (track.artist[0] && track.album[0]) {
        ArtExtractor::getArtPath(track.artist, track.album, artPath, sizeof(artPath));
        if (ArtExtractor::artExists(sd, artPath)) {
            hasArt = true;
        }
    }

    nowPlaying.setHasArt(hasArt);

    if (hasArt) {
        clearNpBgBitmap();
        currentNpBgBitmap = (uint8_t*)malloc(5000);  // 200x200 1-bit
        uint8_t* artBitmap = (uint8_t*)malloc(2520); // 140x140 1-bit
        
        if (currentNpBgBitmap && artBitmap) {
            bool hasBg = ArtRenderer::renderTo(sd, artPath, currentNpBgBitmap, 200, 200, ArtRenderMode::BACKGROUND);
            nowPlaying.setBgBitmap(hasBg ? currentNpBgBitmap : nullptr);

            if (ArtRenderer::renderTo(sd, artPath, artBitmap, 140, 140, ArtRenderMode::NORMAL)) {
                Serial.println("[NP] Drawing with art...");
                display.setFullWindow();
                display.firstPage();
                
                // Color logic for art: Natural is 1=WHITE, 0=BLACK
                uint16_t artFG = GxEPD_WHITE;
                uint16_t artBG = GxEPD_BLACK;
                
                if (!sysSettings.display.skipArtInvert) {
                    artFG = GxEPD_BLACK;
                    artBG = GxEPD_WHITE;
                }

                nowPlaying.setBgColors(artFG, artBG);

                do {
                    if (hasBg) display.drawBitmap(0, 0, currentNpBgBitmap, 200, 200, artFG, artBG);
                    else display.fillScreen(CL_BG);
                    
                    nowPlaying.drawFull(); 
                    display.drawBitmap(30, 30, artBitmap, 140, 140, artFG, artBG);
                } while (display.nextPage());
            } else {
                hasArt = false; 
            }
        } else {
            Serial.println("[NP] MALLOC FAIL for art");
            hasArt = false;
        }
        
        // bgBitmap (currentNpBgBitmap) is NOT freed here, it persists for partial updates
        if (artBitmap) free(artBitmap);
    }

    if (!hasArt) {
        clearNpBgBitmap();
        nowPlaying.setBgBitmap(nullptr);
        nowPlaying.setHasArt(false);
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(CL_BG);
            nowPlaying.drawFull(); 
        } while (display.nextPage());
    }

    audioPlayer.play(track.path);
}

void switchToMenu() {
    clearNpBgBitmap();
    appState = AppState::MAIN_MENU;
    mainMenu.setSettings(sysSettings);
    mainMenu.init(display, sd);
    mainMenu.drawFull();
}

static uint8_t* usbConnectBitmap = nullptr;

void enterUsbSync() {
    Serial.println("[APP] Entering USB SYNC Mode...");
    audioPlayer.stop();
    epd_log("USB SYNC...");
    
    // Read connect bitmap into memory before unmounting
    if (usbConnectBitmap) { free(usbConnectBitmap); usbConnectBitmap = nullptr; }
    File f = sd.open("/Bitmaps/Connect.bin", "r");
    if (f) {
        usbConnectBitmap = (uint8_t*)malloc(5000); // 200x200 / 8 = 5000 bytes
        if (usbConnectBitmap) f.read(usbConnectBitmap, 5000);
        f.close();
    }
    
    // Get card size BEFORE unmounting
    uint32_t sectors = SD_MMC.cardSize() / 512;
    
    // 1. Unmount SD from ESP32 to prevent corruption
    SD_MMC.end();
    
    // 2. Clear scene state if needed
    clearNpBgBitmap();
    
    // 3. Start MSC with manual driver init
    UsbMscManager::getInstance().activate(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0, sectors, sysSettings.usb.freqKhz, sysSettings.usb.chunkSectors);
    
    appState = AppState::USB_SYNC;
    
    // Initial draw
    display.setFullWindow();
    display.firstPage();
    do {
        if (usbConnectBitmap) {
            // Inverted for better aesthetic: swap FG/BG
            display.drawBitmap(0, 0, usbConnectBitmap, 200, 200, CL_BG, CL_FG);
        } else {
            display.fillScreen(CL_BG);
        }
        
        // Top Bar
        display.fillRect(0, 0, 200, 25, CL_FG);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(CL_BG);
        
        String title = "USB STORAGE";
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor((200 - w) / 2, 18);
        display.print(title);
    } while (display.nextPage());
}

void exitUsbSync() {
    Serial.println("[APP] Exiting USB SYNC Mode...");
    UsbMscManager::getInstance().deactivate();
    
    if (usbConnectBitmap) { free(usbConnectBitmap); usbConnectBitmap = nullptr; }
    
    // Re-mount SD
    if (initSD()) {
        initMediaDB(false);
        switchToMenu();
    } else {
        epd_log("SD REMOUNT FAIL");
    }
}

// ─────────────────────────────────────────────────────────────────
//  Debug
// ─────────────────────────────────────────────────────────────────
void dumpLibrary() {
    if (!mediaDB.isValid()) { Serial.println("[DB] not valid"); return; }
    Serial.printf("\n=== LIBRARY DUMP (%lu tracks) ===\n", mediaDB.trackCount());
    
    // Використовуємо forEach замість великих статичних буферів
    mediaDB.forEach([&](const TrackRecord& r) {
        Serial.printf("ID:%-4lu | %-20s | %-20s | %s\n", r.id, r.artist, r.album, r.title);
        return true;
    });
    Serial.println("=== END DUMP ===\n");
}

// ─────────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────────
void setup() {
    UsbMscManager::getInstance().init();
    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n=== NEBULA OS v0.1 ===");

    pinMode(EPD_CS, OUTPUT); digitalWrite(EPD_CS, HIGH);
    // Legacy SD SPI pins setup removed – using SDMMC

    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.setTextWrap(false);
    printBanner();
    epd_log("INIT OK");

    // ── MPU6050 IMU ───────────────────────────────────────────────
    if (mpuManager.begin(8, 9)) {
        epd_log("IMU OK");
    } else {
        epd_log("IMU N/C");
    }

    // ── Налаштування ─────────────────────────────────────────────
    if (SettingsManager::load(sd, sysSettings)) {
        epd_log("SET LOADED");
    } else {
        epd_log("SET DEFAULT");
    }

    // Apply colors to all scenes based on boot settings
    systemStatus.setTime(13, 37);
    systemStatus.setBattery(3.85f);

    mainMenu.setColors(CL_FG, CL_BG);
    mainMenu.status = &systemStatus;

    menu.setColors(CL_FG, CL_BG);
    menu.status = &systemStatus;

    coverFlow.setColors(CL_FG, CL_BG, sysSettings.display.skipArtInvert);
    coverFlow.status = &systemStatus;

    nowPlaying.setColors(CL_FG, CL_BG);

    // ── Ініціалізація I2S / PCM5102 ────────────────────────────────
    if (audioPlayer.begin(sd, sysSettings)) {
        epd_log("DAC OK");
    } else {
        epd_log("DAC FAIL!");
    }

    Serial.print("[SD] Mounting... ");
    if (!initSD()) {
        Serial.println("FAIL");
        epd_log("SD MOUNT FAIL!");
        return;
    }
    Serial.println("OK");
    epd_log("SD OK");
    
    // Create necessary folder structure
    webManager.initDirectories();

    initMediaDB(false);

    // ── Wi-Fi / Web Manager ───────────────────────────────────────
    epd_log("WIFI START");
    webManager.begin(WIFI_SSID, WIFI_PASS);

    // Перейти в головне меню
    appState = AppState::MAIN_MENU;
    mainMenu.setSettings(sysSettings);
    mainMenu.init(display, sd);
    mainMenu.drawFull();

    Serial.println("=== READY ===");
    Serial.println("Grid: 1=UP 2=SEL 3=DOWN | NP: 1=PREV 2=PLAY 3=NEXT 4=MENU");
    UsbMscManager::getInstance().init();
    Serial.println("r=Rescan  d=Dump");
    
}

// ─────────────────────────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // Читати Serial
    char raw = 0;
    if (Serial.available()) {
        raw = Serial.read();
        while (Serial.available()) Serial.read();
    }

    // Глобальні команди
    if (raw == 'r') {
        Serial.println(">> Force rescan...");
        logLine = 0; printBanner();
        initMediaDB(true);
        switchToMenu();
        return;
    }
    if (raw == 'd') { dumpLibrary(); return; }

    // ── MAIN MENU ────────────────────────────────────────────────
    if (appState == AppState::MAIN_MENU) {
        if      (raw == '1') mainMenu.onPrev();
        else if (raw == '3') mainMenu.onNext();
        else if (raw == '2') {
            uint8_t sel = mainMenu.selectedIndex();
            if (sel == 0) { // MEDIA LIBRARY
                appState = AppState::COVER_FLOW;
                coverFlow.setSettings(sysSettings);
                coverFlow.setColors(CL_FG, CL_BG, sysSettings.display.skipArtInvert);
                coverFlow.init(mediaDB, display, sd);
                coverFlow.drawFull();
            }
            else if (sel == 1) { /* TELEMETRY */ }
            else if (sel == 2) { /* EXTRAS */ }
            else if (sel == 3) { // SETTINGS
                appState = AppState::SETTINGS;
                settingsScene.init(display, sd, sysSettings);
                settingsScene.drawFull();
            }
        }
        else if (raw == '4') {
             // Redraw
             mainMenu.drawFull();
        }
    }

    // ── SETTINGS ────────────────────────────────────────────────
    else if (appState == AppState::SETTINGS) {
        if      (raw == '1') settingsScene.onUp();
        else if (raw == '3') settingsScene.onDown();
        else if (raw == '2') {
            settingsScene.onSelect();
            // Check if user selected SENSORS
            if (settingsScene.wantsSensors()) {
                appState = AppState::SENSORS;
                mpuManager.applySettings(sysSettings.mpu);
                sensorsScene.init(display, sysSettings, mpuManager);
                sensorsScene.drawFull();
            }
        }
        else if (raw == '4') {
            if (settingsScene.onBack()) {
                // Apply changes to all components immediately
                audioPlayer.applySettings(sysSettings);
                mpuManager.applySettings(sysSettings.mpu);
                
                mainMenu.setColors(CL_FG, CL_BG);
                menu.setColors(CL_FG, CL_BG);
                coverFlow.setColors(CL_FG, CL_BG, sysSettings.display.skipArtInvert);
                nowPlaying.setColors(CL_FG, CL_BG);

                appState = AppState::MAIN_MENU;
                mainMenu.drawFull();
            }
        }
        else if (raw == '5') {
            if (sysSettings.usb.mode == 0) {
                // SERIAL - Do nothing
            } else if (sysSettings.usb.mode == 1) {
                enterUsbSync();
            } else if (sysSettings.usb.mode == 2) {
                // FLASH (SLOWBOOT)
                uint8_t* flashBitmap = nullptr;
                File f = sd.open("/Bitmaps/Connect.bin", "r");
                if (f) {
                    flashBitmap = (uint8_t*)malloc(5000);
                    if (flashBitmap) f.read(flashBitmap, 5000);
                    f.close();
                }

                display.setFullWindow();
                display.firstPage();
                do {
                    if (flashBitmap) {
                        display.drawBitmap(0, 0, flashBitmap, 200, 200, CL_BG, CL_FG);
                    } else {
                        display.fillScreen(CL_BG);
                    }
                    display.fillRect(0, 0, 200, 25, CL_FG);
                    display.setFont(&FreeMonoBold9pt7b);
                    display.setTextColor(CL_BG);
                    
                    String title = "FIRMWARE FLASH";
                    int16_t x1, y1; uint16_t w, h;
                    display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
                    display.setCursor((200 - w) / 2, 18);
                    display.print(title);
                } while (display.nextPage());
                
                if (flashBitmap) free(flashBitmap);
                
                delay(1000);
                REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
                esp_restart();
            }
        }
    }

    // ── SENSORS ──────────────────────────────────────────────────
    else if (appState == AppState::SENSORS) {
        if      (raw == '1') sensorsScene.onUp();
        else if (raw == '3') sensorsScene.onDown();
        else if (raw == '2') sensorsScene.onSelect();
        else if (raw == '4') {
            if (sensorsScene.onBack()) {
                // Return to settings
                appState = AppState::SETTINGS;
                SettingsManager::save(sd, sysSettings);
                settingsScene.init(display, sd, sysSettings);
                settingsScene.drawFull();
            }
        }
        // Tick for 3D cube live updates
        sensorsScene.tick(now);
    }

    // ── USB SYNC ────────────────────────────────────────────────
    else if (appState == AppState::USB_SYNC) {
        static uint32_t lastDisplayUpdate = 0;
        float speed = UsbMscManager::getInstance().getSpeedKBps();
        
        // Throttled display update (E-ink is slow)
        // Now using 10 seconds and partial refresh
        uint32_t interval = (speed > 0.1f) ? 10000 : 20000; 
        
        if (now - lastDisplayUpdate > interval) {
            lastDisplayUpdate = now;
            
            // Stats overlay on the left side (60px width)
            display.setPartialWindow(0, 25, 60, 175);
            display.firstPage();
            do {
                // Do not clear the background completely.
                // Erase only the text area if necessary, but leaving the dither pattern untouched.
                // Actually, to just overwrite text without background fill, set text color with no bg.
                // Or fill Rect purely where text belongs:
                display.fillRect(0, 25, 60, 175, CL_BG);
                
                display.setFont(nullptr); // back to default 5x7 font for small text
                display.setTextColor(CL_FG);
                
                display.setCursor(2, 40);
                if (speed > 0.1f) {
                    display.print(String(speed, 1));
                    display.setCursor(10, 40); display.print("KB/s");
                } else {
                    display.print("IDLE");
                }
                
                display.setCursor(2, 90);
                display.print(String(UsbMscManager::getInstance().getTotalProcessedMB()));
                display.setCursor(10, 90); display.print("MB");
                
                display.setCursor(2, 160);
                display.print("EXIT [4]");
            } while (display.nextPage());
        }

        if (raw == '4') {
            exitUsbSync();
        }
    }

    // ── COVER FLOW (Library) ─────────────────────────────────────
    else if (appState == AppState::COVER_FLOW) {
        if      (raw == '1') coverFlow.onPrev();
        else if (raw == '3') coverFlow.onNext();
        else if (raw == '4') {
            appState = AppState::MAIN_MENU;
            mainMenu.init(display, sd);
            mainMenu.drawFull();
        }
        else if (raw == '2') {
            AlbumRecord album = coverFlow.selectedAlbum();
            menu.init(mediaDB, display, sd, false); // init without draw
            menu.loadAlbumOnly(album.artist, album.album);
            appState = AppState::SONG_LIST;
        }
    }

    // ── SONG LIST ────────────────────────────────────────────────
    else if (appState == AppState::SONG_LIST) {
        if      (raw == '1') menu.onUp();
        else if (raw == '3') menu.onDown();
        else if (raw == '4') {
            appState = AppState::COVER_FLOW;
            coverFlow.drawFull();
        }
        else if (raw == '2') {
            bool selected = menu.onSelect();
            if (selected) {
                TrackRecord track = menu.selectedTrack();
                // ... same playback logic ...
                appState = AppState::NOW_PLAYING;
                // find tracks in album for NP
                currentAlbumTrackCount = mediaDB.getAlbumTracks(track.artist, track.album, currentAlbumTracks, 8);
                currentTrackIdx = 0;
                for (uint16_t i = 0; i < currentAlbumTrackCount; i++) {
                    if (strncmp(currentAlbumTracks[i].path, track.path, 255) == 0) {
                        currentTrackIdx = (int16_t)i; break;
                    }
                }
                switchToNowPlaying(track, currentTrackIdx);
            }
        }
        if (sysSettings.display.partialRefresh) menu.updateListIfDirty();
        else menu.drawFull();
    }

    // ── NOW PLAYING ───────────────────────────────────────────────
    else if (appState == AppState::NOW_PLAYING) {
        if (raw == '4') {
            audioPlayer.stop();
            clearNpBgBitmap();
            appState = AppState::COVER_FLOW;
            coverFlow.drawFull();
            return;
        }

        // PREV
        if (raw == '1') {
            if (currentTrackIdx > 0) {
                currentTrackIdx--;
                switchToNowPlaying(currentAlbumTracks[currentTrackIdx], currentTrackIdx);
                return;
            }
        }
        // PLAY/PAUSE
        else if (raw == '2') {
            audioPlayer.togglePause();
            bool p = audioPlayer.isPlaying();
            nowPlaying.setPlaying(p);
            systemStatus.setPlaying(p);
            nowPlaying.updateButtons();
        }
        // NEXT
        else if (raw == '3') {
            if (currentTrackIdx < (int16_t)currentAlbumTrackCount - 1) {
                currentTrackIdx++;
                switchToNowPlaying(currentAlbumTracks[currentTrackIdx], currentTrackIdx);
                return;
            }
        }
        // Pump audio data (keep playing)
        audioPlayer.loop();

        nowPlaying.tick(now);
        nowPlaying.updateHeaderIfDirty();
    }

    // Web server handler
    webManager.loop();

    delay(2); 
}

