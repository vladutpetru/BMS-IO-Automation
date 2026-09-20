# Project: ESP32 Local HTTP Server – Temp Monitor & GPIO Control

## Overview
This project builds firmware for an ESP32 using ESP-IDF (not Arduino framework). The device runs a local HTTP server on the same Wi-Fi network so a user can view sensor data and control GPIO pins from a browser or simple HTTP client (no cloud, no external services).

## Hardware
- MCU: ESP32 (specify exact variant, e.g. ESP32-WROOM-32, if relevant)
- Temperature sensor: Analog temperature, GPIO36
- UART: GPIO17 (TX) / GPIO16 (RX) → UART2 for serial.
- Output/controllable pin(s): GPIO4, GPIO18, GPIO19, GPIO21

## Software stack
- Framework: ESP-IDF (target version: e.g. v5.x)
- Build system: idf.py / CMake
- Networking: Wi-Fi station mode connecting to an existing network (not AP mode, unless specified otherwise)
- HTTP server: esp_http_server component (native ESP-IDF, not a third-party library)
- Sensor driver:to be defined 

## Functional requirements
1. On boot, connect to Wi-Fi using stored credentials (via menuconfig or NVS) and print the assigned IP to serial log.
2. Start an HTTP server on port 80 once Wi-Fi is connected.
3. `GET /temperature` — returns current temperature reading as JSON, e.g. `{"temp_c": 23.5}`.
4. `GET /pin?id=5` — returns current state of the specified GPIO.
5. `POST /pin` — accepts JSON body like `{"pin": 5, "state": 1}` to set a GPIO pin's output state.
6. Basic input validation: reject invalid pin numbers or malformed requests with appropriate HTTP status codes (400, 404, etc.).
7. All handlers should be non-blocking or use short reads/writes to avoid stalling the HTTP server task.

## Coding conventions
- Use ESP-IDF's standard project layout (`main/`, `components/`, `CMakeLists.txt`).
- Use `ESP_LOGI`/`ESP_LOGE` for logging, not raw `printf`.
- Handle errors from IDF calls explicitly (check `esp_err_t` returns).
- Keep sensor reading and Wi-Fi/server logic in separate source files/components for clarity.
- Prefer FreeRTOS tasks with reasonable stack sizes and priorities where concurrency is needed (e.g. periodic sensor polling separate from HTTP handling).

## What I want from Claude in this project
- Give full, compilable ESP-IDF C code (not Arduino-style), with clear file/function structure.
- Explain any `menuconfig` settings or `sdkconfig` changes needed.
- Flag any hardware wiring or pull-up/pull-down resistor requirements for the sensor.
- When suggesting HTTP handlers, include both the C code and example `curl` commands to test them.
