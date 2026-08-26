# UPS Monitor

*[Українською](README.md)*

DIY monitoring for a self-built UPS based on ESP32-S3 and INA3221. Measures voltage/current/power on three channels (mains input, load output, battery), exposes the data over the network as a **Network UPS (NUT protocol)** — compatible with, among others, QNAP QTS ("Network UPS slave") — and provides a simple web UI for monitoring and configuration.

## Hardware

- ESP32-S3 (N16R8 or compatible), I2C on GPIO8 (SDA) / GPIO9 (SCL)
- INA3221 — 3-channel voltage/current monitor, address `0x40` (A0 → GND)
- Factory R100 shunts paralleled with hand-soldered R010 (~9 mOhm effective per channel)
- Channels (labels set in `config.h`, currently): `Battery`, `PSU in`, `UPS out`

## Requirements (libraries)

Install via the Arduino Library Manager:

| Library | Version |
|---|---|
| Async TCP (ESP32Async) | ≥ 3.5.0 |
| ESP Async WebServer (ESP32Async) | ≥ 3.12 |
| ElegantOTA Lite | ≥ 3.1.7 |
| ArduinoJson | ≥ 7.4.3 |

`build_opt.h` contains `-DELEGANTOTA_USE_ASYNC_WEBSERVER=1` — required, otherwise ElegantOTA tries to spin up its own synchronous server and conflicts with `ESPAsyncWebServer`.

Board: `ESP32S3 Dev Module`, PSRAM: `OPI PSRAM`, Flash Size: `16MB`.

## Features

- Reads all 3 INA3221 channels (bus voltage, shunt voltage, current, power) with per-channel shunt resistance + zero offset calibration, plus shunt polarity correction (`config.h → CH_CAL`, `currentSign` field) so current sign always matches physical meaning (battery: `+` charging / `-` discharging; mains input and load output are always non-negative, since both channels are physically one-directional)
- Current deadband (`CURRENT_DEADBAND_MA`) — removes phantom current caused by the offset on unpowered/unconnected channels
- Battery state-of-charge (SOC%) via **coulomb counting** — integrating battery current over time, with IR compensation for voltage sag under load (`BATTERY_INTERNAL_RESISTANCE_OHM`) and self-calibration against two voltage reference points (SOC 100%/0%): reaching the "full" voltage resets the counter to 100%, reaching the "empty" voltage resets it to 0% and slowly adjusts the stored full capacity toward the actually observed capacity of the last discharge cycle (accounts for capacity fade over time)
- Reading cache refreshed once a second (`SAMPLE_INTERVAL_MS`) in `loop()` — both HTTP and NUT read only from this cache, avoiding concurrent I2C access from different async contexts
- Web UI (`/`) — voltage/current/power table per channel with battery SOC% and UPS-output load% shown right in the row label, refreshed every second, **UK/EN language switch**
- Settings section (`/settings`) — WiFi, access password, date & time (NTP server, timezone: dropdown of common cities or a manual POSIX TZ field), battery calibration (capacity, SOC 100%/0% voltages), history retention period (actual history storage isn't implemented yet — this setting is ahead of that feature)
- WiFi: credentials stored in `Preferences`, auto-connect on boot (up to 5 min of retries), fallback AP mode for initial setup, non-blocking reconnect in `loop()` if the link drops
- OTA firmware updates via `/update` (ElegantOTA)
- NUT server (port 3493) for integration with a NAS or other UPS monitoring systems
- Single access password (username is always `admin`) for the Settings section, reboot and OTA; changeable from the UI
- Factory reset via the physical BOOT button

## First boot

1. Flash the board (Arduino IDE, `ESP32S3 Dev Module`)
2. Connect to the access point **`UPS-Monitor-Setup`**, password **`12345678`**
3. Open `http://192.168.4.1/`, enter your home network's SSID/password
4. The device reboots and connects to the given network
5. Change the default access password via `/settings/password` (Settings → "Access password") — default is `change-me`, set in `config.h → AUTH_DEFAULT_PASS`

## Web UI and API

| Route | Method | Auth | Purpose |
|---|---|---|---|
| `/` | GET | none | Main monitoring page, or WiFi setup form in AP mode |
| `/api/data` | GET | none | JSON with current channel readings, battery SOC%, load% (telemetry — intentionally open) |
| `/settings` | GET | yes* | Hub page linking to all settings sections |
| `/wifi` | GET | yes* | WiFi settings form |
| `/wifi/save` | POST | yes* | Save WiFi SSID/password, reboot |
| `/settings/password` | GET | yes* | Access password change form |
| `/api/set-password` | POST | yes* | Change access password (min. 8 chars), reboot |
| `/settings/ntp` | GET | yes* | NTP server / timezone form |
| `/settings/ntp/save` | POST | yes* | Save NTP/TZ, applied immediately without reboot |
| `/api/ntp-settings` | GET | yes* | JSON with current NTP settings (for the form) |
| `/settings/battery` | GET | yes* | Battery calibration form (capacity, SOC 100%/0%) |
| `/settings/battery/save` | POST | yes* | Save battery calibration |
| `/api/battery-settings` | GET | yes* | JSON with current battery calibration (for the form) |
| `/settings/history` | GET | yes* | History retention period form |
| `/settings/history/save` | POST | yes* | Save history retention period |
| `/api/history-settings` | GET | yes* | JSON with current retention period (for the form) |
| `/api/reboot` | POST | yes* | Manual device reboot |
| `/update` | — | yes (ElegantOTA) | OTA firmware update |

*\* HTTP Basic Auth (username `admin`) is skipped while the device is in AP mode (initial setup) — physical/WiFi proximity to the device is already required at that stage anyway.*

## NUT server (Network UPS Tools)

- Port: **3493** (standard NUT port)
- UPS name: `esp32ups` (`config.h → NUT_UPS_NAME`)
- Supported commands: `USERNAME`, `PASSWORD`, `LOGIN`, `LOGOUT`, `PRIMARY`/`MASTER`, `STARTTLS` (proper rejection), `VER`, `NETVER`, `LIST UPS/VAR/RW/CMD/CLIENT`, `GET VAR/TYPE/DESC/UPSDESC/NUMLOGINS`
- **Authentication is not required for reads** — `LIST VAR`/`GET VAR` don't require a prior login. This is intentional: QNAP QTS (External Device → UPS → Network UPS slave) has no login/password fields at all, only a server IP address — so for compatibility, reads stay open on the local network
- `NUT_PASS` is only checked if a client actually sends a `PASSWORD` command (e.g. `upsmon` on Linux)

### Variables served

| Variable | Source |
|---|---|
| `device.type`, `device.mfr`/`ups.mfr`, `ups.model`, `ups.firmware` | static |
| `ups.status` | `OL`/`OB` based on voltage on the `PSU in` channel (`INPUT_PRESENT_V` threshold); `LB` is added when battery SOC < `LOW_BATTERY_SOC_PERCENT`; `CHRG`/`DISCHRG` — based on the battery current's sign |
| `battery.voltage`, `battery.current` | `Battery` channel (signed current: `+` charging, `-` discharging) |
| `battery.charge` | **coulomb counting** — current integrated over time, with IR compensation and self-calibration against the SOC 100%/0% voltage points (see Features) |
| `battery.runtime` | estimated remaining runtime in seconds — only while actually discharging (`0` when on mains or current is within the deadband), computed from the current charge state (mAh) and instantaneous current |
| `input.voltage` | `PSU in` channel |
| `output.voltage`, `output.current` | `UPS out` channel (current is always non-negative) |
| `ups.power`/`ups.realpower`, `ups.load` | computed from the `UPS out` channel, `ups.load` — % of `UPS_RATED_POWER_W` |

## INA3221 calibration

A separate sketch, `ina3221_test.ino`, is used to re-calibrate after replacing/re-soldering shunts. Method: apply two different known currents to a channel (verified with a multimeter in series), solve `V = offset + I·R` for `rShunt_mOhm` and `offset_mV`, plug the values into `config.h → CH_CAL`. A single-point calibration (based on only one known current) produces a noticeable error at low currents — that's why the model uses two parameters (slope + offset) rather than a simple proportion.

Besides resistance and offset, each channel also has a `currentSign` field (`+1`/`-1`) that corrects for shunt wiring polarity, so the current sign always matches physical meaning (for `Battery`: `+` charging / `-` discharging; for `PSU in` and `UPS out` current is always reported non-negative, since both channels are physically one-directional). Verify empirically: connect the PSU with a partially discharged battery and check the sign of `battery.current` — if it's negative while charging, flip the `Battery` channel's `currentSign` to `-1`.

`BATTERY_INTERNAL_RESISTANCE_OHM` (pack internal resistance) affects the accuracy of the voltage IR compensation used for SOC estimation — the starting value is an approximation; refine it empirically by applying a known load and measuring `ΔV/ΔI`.

## Security

- **Power:** don't connect USB while the DC-DC module is also supplying external power — both sources would try to drive the same 3.3V pin, which can damage the onboard regulator
- **Password reset:** hold the **BOOT** button for 5+ seconds during startup (Reset → hold BOOT) — resets WiFi credentials and the access password to factory defaults. There is intentionally no network-based password recovery mechanism — that would require a built-in "backdoor," an unacceptable security trade-off for a device on a home network
- Before real deployment, make sure to change the default values in `config.h`: `AP_PASS`, `AUTH_DEFAULT_PASS`, `NUT_PASS`

## Roadmap / known limitations

- The "amount discharged since the last full-charge anchor" counter (used to self-calibrate battery capacity during coulomb counting) lives only in RAM — if the device reboots between the SOC100/SOC0 reference points, capacity learning for that particular discharge cycle is lost (the charge state itself is unaffected, since it's persisted separately)
- `NUT_PASS` is currently only set in `config.h`, not via the UI (unlike `AUTH_DEFAULT_PASS`)
- No NUT instant commands (`LIST CMD` is always empty) — control actions (e.g. a test discharge) are not implemented
- No history of readings is stored — only the current state (the retention-period setting already exists, the history itself doesn't yet)
