#pragma once
/**
 * AudioPlayer.h — Nebula OS Audio Playback Engine
 *
 * Pipeline: SdFat File → chunks → EncodedAudioStream(FLACDecoderFoxen → I2SStream) → PCM5102
 *
 * Background Task: Decodes and pushes data on Core 0 to avoid UI stutters.
 */
#include <Arduino.h>
#include <FS.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecFLACFoxen.h"
#include "../db/SettingsManager.h"

using namespace audio_tools;

#define I2S_BCLK_PIN 4
#define I2S_LRC_PIN  5
#define I2S_DOUT_PIN 6

class NebulaPlayer {
public:
    NebulaPlayer() : _volume(_i2s) {
        _mutex = xSemaphoreCreateMutex();
    }

    bool begin(fs::FS& sd, const SystemSettings& settings) {
        _sd = &sd;
        _settings = &settings;
        
        auto cfg = _i2s.defaultConfig(TX_MODE);
        cfg.pin_bck         = I2S_BCLK_PIN;
        cfg.pin_ws          = I2S_LRC_PIN;
        cfg.pin_data        = I2S_DOUT_PIN;
        
        cfg.sample_rate     = settings.audio.sampleRate;
        cfg.channels        = settings.audio.channels;
        cfg.bits_per_sample = settings.audio.bitsPerSample;
        cfg.buffer_size     = settings.audio.bufferSize;
        cfg.buffer_count    = settings.audio.bufferCount;

        if (!_i2s.begin(cfg)) {
            Serial.println("[AUD] I2S FAIL");
            return false;
        }

        _volume.begin(cfg);
        _volume.setVolume(settings.audio.volume);

        _enc.setDecoder(&_foxen);
        _enc.setOutput(&_volume);
        
        // Створення задачі згідно з налаштуваннями
        xTaskCreatePinnedToCore(
            _audioTask,
            "AudioTask",
            4096,
            this,
            settings.audio.taskPriority,
            &_taskHandle,
            settings.audio.coreID
        );

        Serial.printf("[AUD] Background Engine OK (Core %d, Prio %d)\n", 
                      settings.audio.coreID, settings.audio.taskPriority);
        return true;
    }

    void applySettings(const SystemSettings& settings) {
        _settings = &settings;
        _volume.setVolume(settings.audio.volume);
        // Balance is applied in the task loop
    }

    bool play(const char* path) {
        stop();
        if (!_hasExt(path, ".flac") && !_hasExt(path, ".FLAC")) {
            Serial.println("[AUD] Only .flac supported");
            return false;
        }

        xSemaphoreTake(_mutex, portMAX_DELAY);
        _file = _sd->open(path, "r");
        if (!_file) {
            Serial.printf("[AUD] Cannot open: %s\n", path);
            xSemaphoreGive(_mutex);
            return false;
        }
        _enc.begin();
        _playing = true;
        _paused  = false;
        xSemaphoreGive(_mutex);

        Serial.printf("[AUD] Playing: %s\n", path);
        return true;
    }

    void stop() {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_playing) {
            _enc.end();
            _file.close();
            _playing = false;
            _paused  = false;
        }
        xSemaphoreGive(_mutex);
    }

    void togglePause() {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_playing) _paused = !_paused;
        xSemaphoreGive(_mutex);
    }

    void setVolume(float v) {
        _volume.setVolume(v);
    }

    bool isPlaying() const { return _playing && !_paused; }
    bool isPaused()  const { return _paused; }
    void setEofCallback(void (*cb)()) { _eofCb = cb; }

    void loop() {
        if (_shouldSignalEof) {
            _shouldSignalEof = false;
            if (_eofCb) _eofCb();
        }
    }

private:
    I2SStream            _i2s;
    VolumeStream         _volume;
    FLACDecoderFoxen     _foxen;
    EncodedAudioStream   _enc;
    fs::File             _file;
    fs::FS*              _sd = nullptr;
    const SystemSettings* _settings = nullptr;
    volatile bool        _playing = false;
    volatile bool        _paused  = false;
    volatile bool        _shouldSignalEof = false;
    
    void                 (*_eofCb)() = nullptr;
    TaskHandle_t         _taskHandle = nullptr;
    SemaphoreHandle_t    _mutex;

    static void _audioTask(void* pvParameters) {
        NebulaPlayer* player = (NebulaPlayer*)pvParameters;
        uint8_t buf[1024];

        for (;;) {
            if (player->_playing && !player->_paused) {
                xSemaphoreTake(player->_mutex, portMAX_DELAY);
                int n = player->_file.read(buf, sizeof(buf));
                
                if (n > 0) {
                    // Apply Balance if needed (only for 16-bit stereo)
                    if (player->_settings && player->_settings->audio.channels == 2 && 
                        player->_settings->audio.bitsPerSample == 16) 
                    {
                        int16_t* samples = (int16_t*)buf;
                        int sampleCount = n / 2;
                        float bal = player->_settings->audio.balance;
                        
                        float lMult = (bal > 0) ? (1.0f - bal) : 1.0f;
                        float rMult = (bal < 0) ? (1.0f + bal) : 1.0f;

                        for (int i = 0; i < sampleCount; i += 2) {
                            samples[i]   = (int16_t)(samples[i]   * lMult); // Left
                            samples[i+1] = (int16_t)(samples[i+1] * rMult); // Right
                        }
                    }

                    player->_enc.write(buf, n);
                    xSemaphoreGive(player->_mutex);
                    vTaskDelay(1); 
                } else {
                    player->_playing = false;
                    player->_enc.end();
                    player->_file.close();
                    player->_shouldSignalEof = true;
                    xSemaphoreGive(player->_mutex);
                    Serial.println("[AUD] EOF");
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }

    static bool _hasExt(const char* p, const char* ext) {
        const char* d = strrchr(p, '.');
        return d && strcasecmp(d, ext) == 0;
    }
};
