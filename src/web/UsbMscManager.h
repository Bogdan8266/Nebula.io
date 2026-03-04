#pragma once
/**
 * UsbMscManager.h — USB Mass Storage support for Nebula OS
 *
 * Exposes the SD_MMC card to a connected PC as a USB flash drive.
 * Optimized version with Zero-Copy DMA support and speed metrics.
 */
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <SD_MMC.h>
#include "../led/LEDManager.h"

extern "C" {
    #include "esp_vfs_fat.h"
    #include "driver/sdmmc_host.h"
    #include "sdmmc_cmd.h"
    #include "soc/soc_memory_types.h"
}

// Legacy macros removed: chunk sizing is now dynamic.

class UsbMscManager {
public:
    static UsbMscManager& getInstance() {
        static UsbMscManager instance;
        return instance;
    }

    void init() {
        _msc.vendorID("NEBULA");
        _msc.productID("Hi-Fi Player");
        _msc.productRevision("1.0");
        _msc.onRead(UsbMscManager::_onRead);
        _msc.onWrite(UsbMscManager::_onWrite);
        _msc.onStartStop(UsbMscManager::_onStartStop);
        _msc.mediaPresent(false);
        _msc.begin(0, 512); 
    }

    bool activate(int clk, int cmd, int d0, uint32_t numSectors, uint32_t freqKhz, int chunkSectors) {
        if (_active) return true;
        _numSectors = numSectors;
        _totalBytesProcessed = 0;
        _lastSpeedUpdate = millis();
        _bytesSinceLastUpdate = 0;

        _chunkSectors = chunkSectors;
        uint32_t reqSize = _chunkSectors * 512;

        Serial.printf("[MSC] Activating: %u sectors, %d KHZ, %d chunk size\n", _numSectors, freqKhz, _chunkSectors);

        if (_chunkBuf && _lastAllocatedSize != reqSize) {
            free(_chunkBuf);
            _chunkBuf = nullptr;
        }

        if (!_chunkBuf) {
            _chunkBuf = (uint8_t*)malloc(reqSize);
            _lastAllocatedSize = reqSize;
            if (!_chunkBuf) {
                Serial.println("[MSC] ERROR: Failed to allocate secondary DMA buffer!");
                return false;
            }
        }

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_1;
        host.max_freq_khz = freqKhz; 

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1; 
        slot_config.clk = (gpio_num_t)clk;
        slot_config.cmd = (gpio_num_t)cmd;
        slot_config.d0 = (gpio_num_t)d0;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_err_t ret = sdmmc_host_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            Serial.printf("[MSC] host_init err: 0x%x\n", ret);
        }

        ret = sdmmc_host_init_slot(host.slot, &slot_config);
        if (ret != ESP_OK) return false;

        ret = sdmmc_card_init(&host, &_card);
        if (ret != ESP_OK) {
            Serial.printf("[MSC] card_init err: %d\n", ret);
            return false;
        }

        _msc.begin(_numSectors, 512); 
        _msc.mediaPresent(true);
        
        _active = true;
        Serial.printf("[MSC] Active. HW Freq: %d MHz\n", _card.max_freq_khz / 1000);
        return true;
    }

    void deactivate() {
        if (!_active) return;
        _msc.mediaPresent(false);
        _msc.end();
        sdmmc_host_deinit();
        _active = false;
    }

    bool isActive() const { return _active; }

    void setReadOnly(bool ro) { _readOnly = ro; }
    bool isReadOnly() const { return _readOnly; }

    float getSpeedKBps() {
        uint32_t now = millis();
        uint32_t diff = now - _lastSpeedUpdate;
        if (diff < 500) return _currentSpeed;

        _currentSpeed = (float)(_bytesSinceLastUpdate) / (float)diff;
        _bytesSinceLastUpdate = 0;
        _lastSpeedUpdate = now;
        return _currentSpeed;
    }

    uint32_t getTotalProcessedMB() const { return (uint32_t)(_totalBytesProcessed / (1024 * 1024)); }

private:
    UsbMscManager() {}
    USBMSC _msc;
    bool _active = false;
    bool _readOnly = false;
    uint32_t _numSectors = 0;
    
    static uint32_t _totalBytesProcessed;
    static uint32_t _bytesSinceLastUpdate;
    static uint32_t _lastSpeedUpdate;
    static float _currentSpeed;

    static sdmmc_card_t _card;
    static uint8_t* _chunkBuf;
    static uint32_t _lastAllocatedSize;
    static int _chunkSectors;

    static int32_t _onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
        // Zero-Copy Optimization:
        // Try reading directly into the provided buffer if it's DMA capable.
        if (esp_ptr_dma_capable(buffer)) {
            esp_err_t err = sdmmc_read_sectors(&_card, buffer, lba, bufsize / 512);
            if (err == ESP_OK) {
                LEDManager::getInstance().setTransferActive(true);
                _totalBytesProcessed += bufsize;
                _bytesSinceLastUpdate += bufsize;
                return bufsize;
            }
        }

        // Fallback to chunked buffered transfer
        if (!_chunkBuf) return -1;
        uint32_t sectorsTotal = bufsize / 512;
        uint8_t* outPtr = (uint8_t*)buffer;
        uint32_t currentLba = lba;
        uint32_t remainingSectors = sectorsTotal;

        while (remainingSectors > 0) {
            uint32_t toRead = (remainingSectors > _chunkSectors) ? _chunkSectors : remainingSectors;
            esp_err_t err = sdmmc_read_sectors(&_card, _chunkBuf, currentLba, toRead);
            if (err != ESP_OK) return -1;

            memcpy(outPtr, _chunkBuf, toRead * 512);
            outPtr += (toRead * 512);
            currentLba += toRead;
            remainingSectors -= toRead;
        }

        _totalBytesProcessed += bufsize;
        _bytesSinceLastUpdate += bufsize;
        LEDManager::getInstance().setTransferActive(true);
        return bufsize;
    }

    static int32_t _onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
        if (UsbMscManager::getInstance().isReadOnly()) return -1;

        // Zero-Copy Optimization:
        // Try writing directly from the provided buffer if it's DMA capable.
        if (esp_ptr_dma_capable(buffer)) {
            esp_err_t err = sdmmc_write_sectors(&_card, buffer, lba, bufsize / 512);
            if (err == ESP_OK) {
                LEDManager::getInstance().setTransferActive(true);
                _totalBytesProcessed += bufsize;
                _bytesSinceLastUpdate += bufsize;
                return bufsize;
            }
        }

        // Fallback to chunked buffered transfer
        if (!_chunkBuf) return -1;
        uint32_t sectorsTotal = bufsize / 512;
        uint8_t* inPtr = (uint8_t*)buffer;
        uint32_t currentLba = lba;
        uint32_t remainingSectors = sectorsTotal;

        while (remainingSectors > 0) {
            uint32_t toWrite = (remainingSectors > _chunkSectors) ? _chunkSectors : remainingSectors;
            memcpy(_chunkBuf, inPtr, toWrite * 512);
            esp_err_t err = sdmmc_write_sectors(&_card, _chunkBuf, currentLba, toWrite);
            if (err != ESP_OK) return -1;

            inPtr += (toWrite * 512);
            currentLba += toWrite;
            remainingSectors -= toWrite;
        }

        _totalBytesProcessed += bufsize;
        _bytesSinceLastUpdate += bufsize;
        LEDManager::getInstance().setTransferActive(true);
        return bufsize;
    }

    static bool _onStartStop(uint8_t power_condition, bool start, bool load_eject) {
        return true;
    }
};

sdmmc_card_t UsbMscManager::_card = {0};
uint8_t* UsbMscManager::_chunkBuf = nullptr;
uint32_t UsbMscManager::_lastAllocatedSize = 0;
int UsbMscManager::_chunkSectors = 128;
uint32_t UsbMscManager::_totalBytesProcessed = 0;
uint32_t UsbMscManager::_bytesSinceLastUpdate = 0;
uint32_t UsbMscManager::_lastSpeedUpdate = 0;
float UsbMscManager::_currentSpeed = 0.0f;
