# UPS-monitor
DYI UPS monitoring software for ESP32


requirements:
- Async TCP (from ESP32Async) v.3.5.0+
- ESP Async WebServer (from ESP32Async) v 3.12+
- ElegantOTA Lite v3.1.7+


Default AP SSID: UPS-Monitor-Setup
Default AP password: 12345678

OTA user and password should be changed in config.h


ina3221_test.ino is used for calibrate INA3221 after changing shunts
Note: disable external power (DC/DC module) when is using USB connections to avoid power collisions and damage of ESP module
