#!/bin/bash
# flash_monitor.sh - build, flash, and monitor ESP32

# Load ESP-IDF environment variables
. $HOME/esp-idf/export.sh
# Port for the ESP32
PORT=${1:-/dev/ttyUSB0}  # default to /dev/ttyUSB0, or override by passing as argument

# Build the project
idf.py build || { echo "Build failed"; exit 1; }

# Flash the ESP32
idf.py -p "$PORT" flash || { echo "Flash failed"; exit 2; }

# Open monitor
idf.py -p "$PORT" monitor





