#pragma once
/**
 * MPU6050Manager.h — IMU Driver Wrapper for Nebula OS
 * Reads accelerometer, gyroscope, temperature from MPU6050 via I2C.
 * Provides pitch/roll for 3D cube visualization.
 */
#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>
#include "../db/SettingsManager.h"

struct Vec3f { float x, y, z; };

class MPU6050Manager {
public:
    bool begin(uint8_t sda = 8, uint8_t scl = 9) {
        Wire.begin(sda, scl);
        _mpu.initialize();
        _connected = _mpu.testConnection();
        if (_connected) {
            Serial.println("[MPU] Connected OK");
            _mpu.setFullScaleAccelRange(0);  // ±2g default
            _mpu.setFullScaleGyroRange(0);   // ±250°/s default
        } else {
            Serial.println("[MPU] Connection FAILED");
        }
        return _connected;
    }

    bool isConnected() const { return _connected; }

    void applySettings(const MPU6050Settings& cfg) {
        if (!_connected) return;
        _mpu.setFullScaleAccelRange(cfg.accelRange);
        _mpu.setFullScaleGyroRange(cfg.gyroRange);
        _invertX = cfg.invertX;
        _invertY = cfg.invertY;
        _invertZ = cfg.invertZ;
        _cfg = cfg;
    }

    void update() {
        if (!_connected) return;
        int16_t ax, ay, az, gx, gy, gz;
        _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

        // Scale factors based on configured range
        float accelDiv = _getAccelDivisor();
        float gyroDiv  = _getGyroDivisor();

        _accel.x = (float)ax / accelDiv;
        _accel.y = (float)ay / accelDiv;
        _accel.z = (float)az / accelDiv;

        _gyro.x = (float)gx / gyroDiv;
        _gyro.y = (float)gy / gyroDiv;
        _gyro.z = (float)gz / gyroDiv;

        // Apply axis inversion
        if (_invertX) { _accel.x = -_accel.x; _gyro.x = -_gyro.x; }
        if (_invertY) { _accel.y = -_accel.y; _gyro.y = -_gyro.y; }
        if (_invertZ) { _accel.z = -_accel.z; _gyro.z = -_gyro.z; }

        // Temperature: raw / 340.0 + 36.53
        _temp = (float)_mpu.getTemperature() / 340.0f + 36.53f;

        // Compute pitch/roll from accelerometer (simple, no DMP)
        _pitch = atan2f(_accel.x, sqrtf(_accel.y * _accel.y + _accel.z * _accel.z));
        _roll  = atan2f(_accel.y, sqrtf(_accel.x * _accel.x + _accel.z * _accel.z));
    }

    Vec3f accel() const { return _accel; }
    Vec3f gyro()  const { return _gyro; }
    float temp()  const { return _temp; }
    float pitch() const { return _pitch; }
    float roll()  const { return _roll; }

    // String labels for ranges
    static const char* accelRangeStr(uint8_t r) {
        const char* labels[] = {"±2g", "±4g", "±8g", "±16g"};
        return (r < 4) ? labels[r] : "?";
    }
    static const char* gyroRangeStr(uint8_t r) {
        const char* labels[] = {"±250", "±500", "±1K", "±2K"};
        return (r < 4) ? labels[r] : "?";
    }

private:
    MPU6050 _mpu;
    bool _connected = false;
    Vec3f _accel = {0,0,0};
    Vec3f _gyro  = {0,0,0};
    float _temp  = 0;
    float _pitch = 0;
    float _roll  = 0;
    bool _invertX = false, _invertY = false, _invertZ = false;
    MPU6050Settings _cfg;

    float _getAccelDivisor() {
        // LSB/g values: ±2g=16384, ±4g=8192, ±8g=4096, ±16g=2048
        const float divs[] = {16384.0f, 8192.0f, 4096.0f, 2048.0f};
        return (_cfg.accelRange < 4) ? divs[_cfg.accelRange] : 16384.0f;
    }
    float _getGyroDivisor() {
        // LSB/(°/s): ±250=131, ±500=65.5, ±1000=32.8, ±2000=16.4
        const float divs[] = {131.0f, 65.5f, 32.8f, 16.4f};
        return (_cfg.gyroRange < 4) ? divs[_cfg.gyroRange] : 131.0f;
    }
};
