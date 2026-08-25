# UPS-monitor
DYI UPS monitoring software for ESP32


requirements:
- Async TCP (from ESP32Async) v.3.5.0+
- ESP Async WebServer (from ESP32Async) v 3.12+
- ElegantOTA Lite v3.1.7+
- ArduinoJson 7.4.3


Default AP SSID: UPS-Monitor-Setup
Default AP password: 12345678

OTA user and password should be changed in config.h

System Password Reset: press Reset, then BOOT button for over 5 sec after reset. Device reset all passwords and requires setup Wifi/admin pass from scratch.

ina3221_test.ino is used for calibrate INA3221 after changing shunts
Note: disable external power (DC/DC module) when is using USB connections to avoid power collisions and damage of ESP module


