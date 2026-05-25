# Outdoor Air Quality Monitoring Module (AQM)

## Project Overview
This repository contains a solution for an outdoor air quality monitoring device.
It is designed to integrate with smart home systems and heat recovery ventilation (HRV) units. By analyzing ambient air pollution in real time, the module can provide signalization to automatically trigger ventilation bypasses, preventing polluted air from entering the building.

## Core Technologies & System Design

### 1. Hardware Architecture
* **Microcontroller:** **ESP32-S3**
* **Sensors:**
  * **Sensirion SEN55:** Optical module for precise measurement of particulate matter (PM1.0, PM2.5, PM4.0, PM10.0), alongside VOC and NOx indices.
  * **SGX Sensortech MiCS-6814:** Chemoresistive gas sensor array for the detection of reducing, oxidizing, and ammonia-based gases (CO, NO2, NH3).
  * **SGX-4H2S-100 & SGX-4SO2-20:** Industrial electrochemical sensors for highly precise detection of Hydrogen Sulfide and Sulfur Dioxide. The analog signals are processed via custom analog circuitry (transimpedance amplifiers) and digitized using an external 16-bit ADC (ADS1115).
* **Actuation & Interfaces:** * Integrated relay intended for status signalization (e.g., triggering external HRV inputs or alarms).
  * **RS-485** physical layer for wired Modbus RTU communication.
  * **Wi-Fi** physical layer for wireless Modbus TCP connectivity.

### 2. Firmware
* **Development Environment:** The firmware is entirely managed and built using **PlatformIO**. *Note: A properly configured PlatformIO environment with the ESP-IDF toolchain is required to open, compile, or modify this project.*
* **Framework & OS:** Written in C on top of the **ESP-IDF** framework, utilizing **FreeRTOS**.
* **Signal Processing:** Implements digital filtering via moving average algorithms with variable integration windows (5, 30, and 60 seconds) to mitigate noise and stabilize output data.

### 3. Communication
* **Modbus RTU:** Serial communication operating over a half-duplex RS-485 bus, featuring explicit hardware flow control.
* **Modbus TCP:** Networked protocol variant operating concurrently over Wi-Fi.
* **Dual-Mode Web Server:** An embedded HTTP server operating in both Station (STA) and Access Point (AP) modes. The AP mode acts as a fallback, allowing users to configure the device, manage Wi-Fi credentials, and control hardware peripherals even during network outages.

### 4. Data Analytics
* **Raspberry Pi:** A local Raspberry Pi is configured as a data collection hub hosting an **InfluxDB** server. Using a USB-to-RS485 converter, custom Python scripts (specifically `aqm_monitor.py`) continuously poll the module.
The acquired data is simultaneously stored into the InfluxDB database for real-time monitoring and logged into structured CSV files for offline analysis.
* **Thermal Compensation Model:** A dedicated MATLAB script (`teplotní_komora_v2.m`) is used to process datasets collected during thermal chamber testing. This script analyzes the temperature dependency of the sensors and derives the exact compensation coefficients utilized by the firmware.

---

## Repository Structure
├── lib/               # Custom application components and peripheral drivers
│   ├── ads1115/       # Driver for the 16-bit external Analog-to-Digital Converter
│   ├── aqm_config/    # System configuration
│   ├── aqm_datastore/ # Data handling and Modbus register mapping
│   ├── aqm_gpio/      # GPIO wrapper
│   ├── aqm_i2c/       # I2C wrapper
│   ├── aqm_mics6814/  # Driver and data processing for the chemoresistive sensor
│   ├── aqm_modbus/    # Modbus RTU and TCP stack implementation
│   ├── aqm_tasks/     # FreeRTOS task definitions
│   ├── aqm_wifi/      # Wi-Fi STA/AP provisioning and web server
│   └── sen55/         # Driver for the Sensirion SEN55 environmental node
├── managed_components/# Automatically managed ESP-IDF external dependencies
├── src/               # Application entry point
│   ├── CMakeLists.txt # Build configuration
│   ├── idf_component.yml # Component manifest
│   └── main.c         # Hardware initialization and start of tasks
└── test/              # Validation scripts and datasets
    ├── csv_data/      # Raw and processed data
    ├── aqm_monitor.py # Python script for Modbus data logging
    ├── config.yaml    # Configuration parameters for telemetry scripts
    └── temperature_compensation.m # MATLAB script for thermal compensation calculation