#pragma once
/**
 * WiFiQRCode.h — Generate WiFi QR Code for easy network sharing
 */
#include <Arduino.h>
#include <qrcode.h>

class WiFiQRCode {
public:
    // Generate WiFi QR code string in standard format
    // Format: WIFI:S:SSID;T:WPA;P:PASSWORD;;
    // For open networks: WIFI:S:SSID;T:nopass;;
    static String generateWiFiString(const char* ssid, const char* password, bool isOpen = false) {
        String qr = "WIFI:S:";
        qr += ssid;
        qr += ";";
        
        if (isOpen) {
            qr += "T:nopass;;";
        } else {
            qr += "T:WPA;P:";
            qr += password;
            qr += ";;";
        }
        
        return qr;
    }
    
    // Generate QR code data - returns pointer to QRCode struct
    // Caller must provide buffer of sufficient size
    static QRCode* generate(const char* ssid, const char* password, bool isOpen, uint8_t* buffer) {
        String wifiString = generateWiFiString(ssid, password, isOpen);
        
        // Use version 3 (29x29 modules) - good balance for WiFi credentials
        // Version 3 can encode ~50 chars which is enough for most WiFi strings
        static QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(3)];
        
        qrcode_initText(&qrcode, qrcodeData, 3, 0, wifiString.c_str());
        
        // Copy to output buffer
        memcpy(buffer, qrcodeData, qrcode_getBufferSize(3));
        
        return &qrcode;
    }
    
    // Get required buffer size for version 3 (70 bytes for version 3)
    static int getBufferSize() { return 70; }
};
