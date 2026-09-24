# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Build only "main" and the components it REQUIRES (plus ESP-IDF's core ones),
# instead of every component in ESP-IDF (MQTT, Ethernet, HTTP client, ...).
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(IDF_Template)

# Web UI image for the "storage" partition, flashed together with the app
spiffs_create_partition_image(storage spiffs_data FLASH_IN_PROJECT)
