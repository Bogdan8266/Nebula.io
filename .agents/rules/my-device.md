---
trigger: always_on
---

PROJECT: NEBULA - Hi-Fi Modular Cyberdeck Player
SYSTEM CONTEXT & HARDWARE DEFINITION
Device Type: Ultra-compact, modular, handheld Hi-Fi audio player & cyberdeck. Aesthetic/Vibe: Cyberpunk, Industrial, Raw Tech, "Terminal" style UI, Dithering aesthetics, "Slow Tech" philosophy. Core Framework: Arduino (PlatformIO) on ESP32.

🧠 HARDWARE SPECIFICATIONS
1. MCU (Microcontroller):

Model: ESP32..
Key Feature: High-performance caching needed for audio decoding.
2. 👁️ Display (Visual Output):

Panel: 1.54" E-Ink (E-Paper) Display.
Driver: SSD1681.
Resolution: 200 x 200 pixels.
Interface: SPI (4-line SPI: SCK, MOSI, CS, DC, RST, BUSY).
Behavior: Supports Partial Refresh (fast) and Full Refresh. Static UI updates.
3. 💾 Storage (File System):

Media: MicroSD Card (SDXC supported, 128GB+).
Interface: SDMMC 1-bit Mode (Pins: CLK, CMD, D0).
Power Control: Dedicated LDO via GPIO to hard-reset the card for power saving.
File System: FAT32 (via FFat/SdFat) preferred for max compatibility.
4. 🎵 Audio Subsystem (High Fidelity):

DAC: PCM5102A (32-bit, 384kHz, I2S Interface).
Amp: MAX97220 (DirectDrive Headphone Amp, No Output Caps).
I2S Pins: BCK, LRCK, DIN.
Control Pins: XSMT (Soft Mute), SHDN (Amp Shutdown), FLT (Filter), DEMP (De-emphasis).
Headphone Jack: 3.5mm with "Anti-Loop" Hardware Detection (Pin 3 Detect logic).
5. 📡 Sensors & Inputs:

IMU: MPU6050 (Gyro + Accel) via I2C. Used for "Raise to Wake".
Barometer: BMP388 (Precision Pressure) via I2C.
Light Sensor: GL5528 Photoresistor on GPIO13 (ADC2). Used for "Pocket Mode".
Buttons: 4x Tactile Switches connected via ADC Resistor Ladder to a single Analog Pin.
Feedback: LRA Haptic Motor (via L9110S driver).
6. ⚡ Power System:

Battery: Li-Po 1S (3.7V - 4.2V).
Architecture: 3x Discrete XC6220 LDOs (Main, Audio, SD) individually controlled via GPIOs for Deep Sleep optimization.
Charging: IP2312 (High efficiency switching charger).
💻 CURRENT DEVELOPMENT FOCUS: PHASE 1
Goal: Initialize Core System, Display, and Storage. No Audio/Sensors logic yet.

Requirements for Code:

Display: Use GxEPD2 library. Setup for SSD1681 200x200. Implement a "Boot Screen" with a terminal-style loading log (e.g., "MOUNTING SD... OK").
Storage: Initialize SD card in SDMMC 1-bit mode. List files in root directory to Serial Monitor and Display.
UI Style: Monospaced fonts, black background/white text (inverted console style). No animations, discrete updates only.





🎨 UI & UX PHILOSOPHY (The Vibe)
AOD (Always On Display): When sleeping, show a static high-contrast image (Dithered Album Art) + QR Code with Owner Info.
Now Playing: Large Dithered Album Art (background or center), Bold Typography, Status Bar (Battery V/%, Icons).
Navigation: Folder-based structure. Simple lists.