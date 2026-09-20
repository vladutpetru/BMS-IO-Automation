# ESP32 Local HTTP Server – Battery Monitor & GPIO Control Automation

## Project Overview

Develop a production-ready firmware implementation for an ESP32 microcontroller using the **native ESP-IDF framework** (v5.x or newer, **not** Arduino). The system operates as a standalone local HTTP server over a Wi-Fi Access Point (AP) mode (no cloud dependencies). It monitors a 10 mV/°C analog temperature sensor, manages GPIO outputs, and provides a mobile-responsive web interface to configure operational parameters stored persistently in NVS (Non-Volatile Storage).

---

## Hardware Specifications

* **MCU Target:** ESP32 (ESP32-WROOM-32)
* **Temperature Sensor:** Analog Sensor (10 mV/°C linear output) connected to ADC1 Channel 0 (**GPIO36 / VP**)
* **UART Interface:** UART2 configured on **GPIO17 (TX)** and **GPIO16 (RX)**
* **Control Outputs (GPIOs):** GPIO4, GPIO18, GPIO19, GPIO21

---

## Software & Technology Stack

* **Framework:** ESP-IDF (Target Version: v5.1+)
* **Build System:** CMake / `idf.py`
* **Networking:** Wi-Fi Access Point (AP) mode only
* **Web Server:** `esp_http_server` (Native ESP-IDF component)
* **Storage:** SPIFFS / LittleFS (for serving static web content) and NVS (for persistent settings)
* **Frontend:** Responsive, mobile-first HTML5/CSS/JS embedded bundle

---

## Functional Requirements

### 1. Networking & Web Infrastructure

* **Wi-Fi AP:** Initialize Wi-Fi in AP mode with a configurable SSID/password. Output the assigned IP address via ESP-IDF serial logging upon startup.
* **HTTP Server:** Launch an HTTP server on port 80.
* **Static Assets:** Serve static web assets stored in the SPIFFS partition (`spiffs_data/`).
* **Asynchronous Execution:** Ensure all HTTP handlers are non-blocking or execute within short timeframes to prevent stalling the web server task.

### 2. Monitoring & Control Interface (UI & Endpoints)

* **Live Monitoring:** Display real-time temperature readings on the UI.
* **Boiler Threshold Configuration:** Provide two dedicated fields on the web page to set the **Minimum** and **Maximum** boiler temperature thresholds.
* **GPIO Action Parameters:** Provide input fields allowing users to configure values for GPIO control actions.
* **Form Controls & Submission:**
* Each configuration field must feature a descriptive title and a dedicated submission button.
* Form submissions must use **HTTP POST requests** (ensure parameters are **not** exposed in the URL query string).


* **Validation & Persistence:**
* Enforce server-side and client-side input validation: accept only integer values between `0` and `100`.
* Upon successful validation, store user-configured parameters persistently using the ESP-IDF **NVS API**.



---

## Architecture & Coding Standards

* **Project Structure:** Adhere strictly to the standard ESP-IDF project layout (`main/`, `components/`, `CMakeLists.txt`).
* **Modularity:** Separate sensor acquisition logic (ADC driver), Wi-Fi/HTTP services, and GPIO control into distinct modules/components.
* **Concurrency:** Run periodic temperature sampling inside a dedicated, non-blocking FreeRTOS task with appropriate stack allocation and priority.
* **Error Handling:** Explicitly check and log all return values of type `esp_err_t`.
* **Logging:** Use the native `ESP_LOGI`, `ESP_LOGW`, and `ESP_LOGE` macros exclusively for system outputs (avoid raw `printf`).

---

## Expected Deliverables from AI

1. **Source Code:** Provide complete, compilable C files (`main.c`, component sources, headers, and CMake files) adhering to ESP-IDF standards.
2. **Configuration (`sdkconfig`):** List all required `menuconfig` configurations (e.g., SPIFFS partition table setup, HTTP server options, ADC configuration).
3. **Hardware Considerations:** Detail any external pull-up/pull-down resistor requirements, ADC noise filtering/calibration recommendations, or wiring constraints for GPIO36 and UART2.
4. **API Testing:** Provide complete HTTP POST C handlers alongside corresponding `curl` commands for local testing of each endpoint.