# <div align="center"> 🌌 NEBULA <br>  <sub>PORTABLE MODULAR CYBER-DECK PLATFORM</sub></div>  <div align="center">  <!-- BADGES: STATUS & INFO --> [![Status](https://img.shields.io/badge/Status-Prototyping-orange?style=for-the-badge&logo=fire)](https://github.com/yourusername/nebula) [![Version](https://img.shields.io/badge/Hardware-v1.0_Alpha-red?style=for-the-badge)](https://github.com/yourusername/nebula/releases) [![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)](LICENSE) <!-- BADGES: TECH STACK --> [![Platform](https://img.shields.io/badge/Platform-ESP32--S3-32a852?style=for-the-badge&logo=espressif&logoColor=white)](https://espressif.com) [![EDA](https://img.shields.io/badge/Designed_In-EasyEDA-blue?style=for-the-badge&logo=easyeda)](https://easyeda.com) [![Display](https://img.shields.io/badge/Display-E--Ink_SSD1681-white?style=for-the-badge&logo=files)](https://weactstudio.com) [![App](https://img.shields.io/badge/App-Kotlin_%2B_Compose-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](https://android.com) [![Design](https://img.shields.io/badge/UI-Material_3_Expressive-black?style=for-the-badge&logo=materialdesign)](https://m3.material.io) </div>
  
--- 
## 💀 BRIEFING
 **Nebula** is not just another ESP32 dev-board. It is an ultra-compact, modular handheld platform designed for advanced sensor monitoring, high-current PWM control, and researching airflow dynamics. 
 Built around the **ESP32-S3 WROOM-1**, featuring an Always-On **E-Ink display** and a complex sensor array. The device is powered by a high-density energy stack and encased in a custom shell. 
 > **Disclaimer:** This device includes high-current output drivers. It was designed for educational purposes, pneumatics research, and thermal control experiments. 
 ---
 ## 📷 VISUAL INTEL 
 <!-- TABLE FOR IMAGES: Replace '#' with your real links to images in the /images folder --> 
 | **The Core (PCB)**  |  **The Shell (3D)** | 
 |:---:|:---:| 
 |<img src="Image/Desing/desing 1.png" alt="PCB Top View" width="300"/> |  |<img src="Image/Desing/desing 2.png" alt="PCB Top View" width="300"/> | 
 |<img src="Image/Desing/desing 2.png" alt="3D Render" width="300"/> |  |<img src="Image/Desing/desing 3.png" alt="PCB Top View" width="300"/> | 
 | *8-Layer PCB, ENIG Finish* | *Custom Manifold Design* |
  > *See full gallery in [`/media`](/media) folder.* 
  > ---
  
## 🔧 TECH SPECS 

### 🧠 The Brains 
* **MCU:** Espressif **ESP32-S3 WROOM-1** (N16R8 supported). 
* * **Core:** Dual-Core Xtensa LX7 @ 240 MHz. 
* * **Conn:** WiFi + BLE 5.0 (Mesh capable). ### 👁️ Interface 
* * **Display:** 1.54" E-Ink (ePaper) Panel. 
* * **Driver:** SSD1681 (WeAct Studio). 
* * **Res:** 200x200 px. 
* * **Vibe:** Monochromatic aesthetics with partial refresh. 
* * **Controls:** 
* * 4x **Tactile Micro-Switches** (3.7x3.7x0.35mm Ultra-Low Profile). 
* * **IMU Motion Control** (Gestures/Wake-on-Lift).
---
 ### 📡 Sensor Array (Inputs) 
 1. **Airflow / Pressure:** `BMP388` (Precision Barometric Sensor). Used for pneumatics triggers. 
 2.  **Inertial:** `MPU6050` (6-Axis Gyro + Accelerometer). 
 3.  **Power Mon:** `INA226` (Bi-directional Current/Power Monitor). High-side sensing. 
 4. **Triggers:** Custom analog pneumatic switch (Puff-sensor based). 
 5. **Internal:** Hall Effect & Temperature. 
 6. ---
 ### ⚡ Power System 
 * **Source:** Li-Po 651732 cell (20C High Drain). 
 * **Charging:** `TP4056` Linear Charger logic. 
 * * **Protection:** Hardware voltage dividers + Software Low Voltage Cutoff (LVC). 
 * * **Output Stage:** Dual **AO3400 (N-MOSFET)** setup for PWM control of heating elements. 
 * * **Storage Gate:** **AO3401 (P-MOSFET)** for hard power-gating the MicroSD card (Deep Sleep optimization). 
---
### 💾 Storage 
* **Slot:** MicroSD (TF Card). 
* * **Format:** FAT32/exFAT. 
* * **Use Case:** Data logging (CSV), OTA Payloads, AOD Image Assets. 
---

## 📱 NEBULA COMPANION APP 
A bleeding-edge mobile application for telemetry and control. 
* **Stack:** Native Android (Kotlin). 
* * **UI:** Jetpack Compose + **Material 3 Expressive**. 
* * **Requirements:** Android 16 (Baklava) or higher. Only for the brave. 
* * **Features:** * BLE Real-time telemetry. * Pixel-Art Editor for E-Ink wallpaper. * Firmware OTA Updates. * "Cyber-Pairing" UX. --- ## 🛠️ MANUFACTURE INFO The PCB pushes the limits of DIY manufacturing: * **EDA:** Designed in **EasyEDA**. 
* * **Stack:** **8-Layers** (Signal Integrity & Thermal dissipation focus). 
* * **Finish:** ENIG (Immersion Gold). 
* * **Features:** V-Cut daughterboard, Kelvin connections for shunt resistors. 
* **[📂 DOWNLOAD GERBER FILES](./hardware/gerbers)**
*  **[📂 VIEW SCHEMATICS](./hardware/schematics)** --- ## 📜 LICENSE This project is open-source under the 
* **MIT License**. Use it, hack it, build it. Just don't blame me if you burn your sensors. --- <div align="center"> *"Any sufficiently advanced technology is indistinguishable from magic."* <br><sub>(c) 1823 Nebula Project</sub>  </div>


