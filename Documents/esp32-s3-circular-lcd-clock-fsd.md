# ESP32-S3 Circular LCD Clock — Functional Specification Document (FSD)

Version: 1.0
Date: 2026-05-22
Repository: https://github.com/JMBMdes/Clock

---

## 1. System Overview

### 1.1 Purpose

The ESP32-S3 Circular LCD Clock is an embedded real-time clock device that displays the current time on a 2.8-inch circular TFT LCD. Time is sourced from a public NTP server over WiFi, ensuring continuous accuracy without a battery-backed RTC.

### 1.2 Problem Statement

Conventional desk clocks drift over time and require manual adjustment. This device eliminates drift by synchronizing continuously with public NTP infrastructure, delivering accurate local-time display without user intervention after initial setup.

### 1.3 Users / Stakeholders

- **End user**: Person who places the device on a desk or shelf and expects accurate time displayed at all times.
- **Developer**: Builder and maintainer of the firmware; provisions the device, flashes firmware, and runs tests via the ESP32 Workbench.

### 1.4 Goals & Non-Goals

**Goals:**
- Display accurate local time on a circular LCD, synchronized via NTP over WiFi.
- Provide simple WiFi and clock provisioning via an HTTP captive portal (no mobile app required).
- Operate autonomously and recover from WiFi or NTP failures without user intervention.
- Build firmware entirely with ESP-IDF (no Arduino framework).

**Non-Goals:**
- Alarm or timer functionality.
- Battery-backed operation or external RTC chip integration.
- Bluetooth configuration interface.
- Remote display or networked multi-device synchronization.
- OTA firmware updates (deferred to a future release).

### 1.5 High-Level System Flow

```
Power-on
   │
   ▼
NVS Init ──► No credentials? ──► AP Mode ──► Captive Portal ──► Save config ──► Reboot
   │
   ▼  Has credentials
WiFi STA Connect
   │
   ▼
SNTP Sync ──► Apply timezone ──► Set SNTP_SYNCED_BIT
   │
   ▼
Clock Render Loop (1 Hz)
   ├──► Update display (analog face + date)
   ├──► Feed watchdog
   └──► Periodic SNTP resync (every 60 min)
```

---

## 2. System Architecture

### 2.1 Logical Architecture

The system operates in two distinct modes:

**Provisioning Mode (AP):**
```
[Phone / Laptop] ──WiFi──► [ESP32-S3 SoftAP + HTTP Server] ──► [NVS]
```

**Clock Mode (STA):**
```
[NTP Server] ◄──WiFi──► [ESP32-S3 SNTP Client] ──► [Clock Task] ──► [Display]
```

**Subsystems:**

| Subsystem | Responsibility |
|-----------|---------------|
| Display | LVGL + GC9A01 SPI driver; renders clock face at 1 Hz |
| Time | ESP-IDF SNTP component; applies POSIX timezone via `setenv("TZ",...)` |
| WiFi | ESP-IDF WiFi stack; STA mode for operation, AP mode for provisioning |
| Provisioning | ESP-IDF HTTP server; serves captive portal pages |
| Config | ESP-IDF NVS; persists all configuration parameters |
| Reliability | TWDT software watchdog, hardware WDT, heap monitor |

### 2.2 Hardware / Platform Architecture

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 (Xtensa LX7 dual-core, 240 MHz) |
| Display | 2.8" circular TFT LCD, GC9A01 driver (assumed), 240×240 px |
| Display interface | SPI (SPI2 / HSPI), DMA-backed |
| CONFIG button | GPIO0 / BOOT button on DevKit (assumed) — held 5 s for AP mode or factory reset |
| Power | USB-C or 5 V input via onboard regulator |
| Connectivity | 802.11 b/g/n WiFi (built-in) |

**Display pinout (assumed — verify against board schematic before Phase 1):**

| Signal | ESP32-S3 GPIO |
|--------|---------------|
| MOSI   | GPIO 35 |
| SCLK   | GPIO 36 |
| CS     | GPIO 34 |
| DC     | GPIO 4  |
| RST    | GPIO 5  |
| BLK    | GPIO 6  |

### 2.3 Software Architecture

**Firmware stack:**

| Layer | Component |
|-------|-----------|
| Framework | ESP-IDF v5.x |
| Graphics | LVGL v8.x |
| Display driver | GC9A01 (SPI, DMA-backed) (assumed) |
| Time | `esp_sntp` SNTP component |
| WiFi | `esp_wifi` (STA + AP) |
| HTTP server | `esp_http_server` (captive portal) |
| Persistence | NVS (`esp_nvs`) |
| Reliability | `esp_task_wdt` (TWDT) |

**FreeRTOS task map:**

| Task | Core | Stack | Priority | Description |
|------|------|-------|----------|-------------|
| `clock_task` | 0 | 8 KB | 5 | Reads system time; updates LVGL every 1 s |
| `wifi_task` | 0 | 6 KB | 4 | Manages STA connection and reconnection |
| `prov_task` | 0 | 8 KB | 3 | Runs HTTP server in AP mode (on-demand) |
| `watchdog_task` | 1 | 4 KB | 6 | Feeds TWDT; monitors heap; logs health |

**Boot sequence:**
1. Hardware init: SPI bus, GPIO configuration.
2. NVS init: load configuration, or apply defaults.
3. Display init: driver and LVGL initialization; show boot screen ("Connecting…").
4. WiFi: if credentials in NVS → STA connect; else → AP mode + captive portal.
5. On STA connect: start SNTP; await `SNTP_SYNCED_BIT`; update display.
6. Start `clock_task` render loop.
7. Start `watchdog_task`.
8. Background: periodic SNTP resync every 60 min, WiFi event-driven reconnection.

---

## 3. Implementation Phases

### 3.1 Phase 1 — Hardware Bring-Up & Foundation

**Scope:**
- ESP-IDF project creation and GitHub repository initialization.
- SPI display driver integration (GC9A01 + LVGL).
- Static analog clock face rendering (no real time yet).
- Serial logging infrastructure.

**Deliverables:**
- Buildable ESP-IDF project committed to `https://github.com/JMBMdes/Clock`.
- Display initializes and renders a static analog clock face (fixed hands).
- SPI DMA transfer confirmed working; no IDF errors or warnings.

**Exit Criteria:**
- TC-DISP-100 passes (display initializes without error).
- TC-DISP-101 passes (clock face renders correctly).
- Project builds cleanly with `idf.py build`.

**Dependencies:**
- ESP32-S3 board with display wired per Section 2.2 pinout.
- ESP32 Workbench available for flashing.

### 3.2 Phase 2 — WiFi, NTP & Provisioning

**Scope:**
- WiFi STA connection using credentials from NVS.
- SNTP time sync and POSIX timezone application.
- Live analog clock rendering (1 Hz update from system time).
- Captive portal AP mode for first-time and reconfiguration.
- NVS persistence for all config parameters.
- Factory reset via CONFIG button.

**Deliverables:**
- Device connects to configured WiFi and displays accurate local time.
- Captive portal accessible for provisioning and reconfiguration.
- NVS stores WiFi credentials, NTP server, timezone, and brightness.
- Factory reset clears NVS and enters AP mode.

**Exit Criteria:**
- TC-NTP-100, TC-NTP-103 pass (sync + timezone).
- TC-CP-100, TC-CP-101 pass (portal provisioning).
- TC-NVS-100, TC-NVS-101, TC-NVS-102 pass (persistence + factory reset).
- WIFI-001, WIFI-003 pass (connect + auto-reconnect).

**Dependencies:**
- Phase 1 complete.
- Test WiFi AP available (via ESP32 Workbench or home router).

### 3.3 Phase 3 — Reliability & Polish

**Scope:**
- Software and hardware watchdog (TWDT).
- Heap memory monitoring and preventive reboot.
- NTP sync status visual indicator on display.
- Display brightness control (PWM on BLK pin, value from NVS).
- 24-hour soak test.

**Deliverables:**
- Watchdog task running and verified by deliberate hang test.
- Heap monitor with serial warnings and automatic reboot at critical threshold.
- Brightness controlled per NVS config.
- 24-hour soak test passed.

**Exit Criteria:**
- TC-WDT-100, TC-WDT-101, TC-WDT-102 pass.
- TC-PERF-100 passes (CPU < 40%).
- TC-SOAK-100 passes (24-hour continuous operation, no crashes).

**Dependencies:**
- Phase 2 complete.

---

## 4. Functional Requirements

### 4.1 Functional Requirements (FR)

#### Display & Clock Rendering

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-1.1 | Must | The system shall render a real-time clock face on the 2.8" circular LCD. |
| FR-1.2 | Must | The display shall update the shown time at 1 Hz (once per second). |
| FR-1.3 | Should | The clock face shall display time in an analog format with hour, minute, and second hands. |
| FR-1.4 | Should | The display shall show the current date (weekday, day, month) in a secondary area of the face. |
| FR-1.5 | Should | The display shall show a visual indicator of NTP synchronization status (synced / syncing). |
| FR-1.6 | May | Display backlight brightness shall be configurable from 0% to 100% via the captive portal. |

#### NTP Time Synchronization

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-2.1 | Must | The system shall synchronize system time via SNTP upon WiFi connection. |
| FR-2.2 | Must | The NTP server address shall be user-configurable (default: `pool.ntp.org`) and stored in NVS. |
| FR-2.3 | Must | The system shall support timezone configuration as a POSIX TZ string, stored in NVS (default: `UTC0`). |
| FR-2.4 | Should | The system shall perform an SNTP resync every 60 minutes. |
| FR-2.5 | Should | Prior to the first successful sync, the display shall show a "syncing" placeholder rather than uninitialized time. |
| FR-2.6 | Should | NTP sync events (success, failure, time-correction delta) shall be logged via serial. |

#### WiFi Connectivity

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-3.1 | Must | The system shall connect to the configured WiFi network in STA mode. |
| FR-3.2 | Must | WiFi credentials shall be stored encrypted in NVS. |
| FR-3.3 | Must | The system shall automatically attempt reconnection after any WiFi disconnection. |
| FR-3.4 | Should | WiFi connection status changes (connect, disconnect, reconnect) shall be logged via serial. |
| FR-3.5 | Must | The system shall support WPA2/WPA3 Personal authentication. |

#### WiFi Provisioning

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-4.1 | Must | The system shall enter AP mode automatically when no valid WiFi credentials exist in NVS. |
| FR-4.2 | Must | The system shall enter AP mode when the CONFIG button is held for 5 seconds. |
| FR-4.3 | Should | The AP SSID shall use the format `CLOCK-{MAC_LAST_4}`. |
| FR-4.4 | Should | The AP shall run a DHCP server and assign itself IP 192.168.4.1. |
| FR-4.5 | Must | The AP shall serve an HTTP captive portal for device configuration. |
| FR-4.6 | Must | The captive portal shall allow entry of the WiFi SSID and password. |
| FR-4.7 | Should | The captive portal shall allow configuration of NTP server, timezone, and brightness. |
| FR-4.8 | Should | The captive portal shall validate all input fields before accepting the form. |
| FR-4.9 | Must | Configuration shall be written to NVS before the device reboots into STA mode. |
| FR-4.10 | May | AP mode shall auto-deactivate after 5 minutes with no connected client. |

#### Configuration & NVS

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-5.1 | Must | All device configuration shall be stored in NVS. |
| FR-5.2 | Must | Configuration shall persist across reboots and power cycles. |
| FR-5.3 | Must | The system shall apply built-in default values when NVS is empty or corrupt. |
| FR-5.4 | Must | The system shall support a factory reset that clears all NVS configuration. |
| FR-5.5 | Should | Factory reset shall be triggerable by holding the CONFIG button for 5 seconds. |
| FR-5.6 | Must | After factory reset, the system shall automatically enter AP provisioning mode. |
| FR-5.7 | Must | WiFi credentials shall not appear in plaintext in serial logs or debug output. |

#### Reliability

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-6.1 | Must | The system shall implement a software watchdog (TWDT) subscribed to by all FreeRTOS tasks. |
| FR-6.2 | Must | The system shall reboot automatically upon watchdog timeout. |
| FR-6.3 | Must | The watchdog shall not false-trigger during normal WiFi reconnection or SNTP events. |
| FR-6.4 | Should | A hardware watchdog shall be enabled as an independent failsafe. |
| FR-6.5 | Should | The system shall monitor free heap and log a serial warning when it drops below 30 KB. |
| FR-6.6 | Should | The system shall trigger a preventive reboot when free heap drops below 10 KB. |

### 4.2 Non-Functional Requirements (NFR)

#### Performance

| ID | Priority | Requirement |
|----|----------|-------------|
| NFR-1.1 | Must | Clock display shall update at 1 Hz with per-step jitter < 50 ms. |
| NFR-1.2 | Should | SNTP synchronization shall complete within 5 seconds of WiFi association. |
| NFR-1.3 | Should | Timekeeping accuracy shall be within ±1 second between NTP syncs (using `esp_timer`). |

#### Reliability

| ID | Priority | Requirement |
|----|----------|-------------|
| NFR-2.1 | Must | The system shall recover from WiFi disconnection automatically, without user intervention. |
| NFR-2.2 | Should | The system shall operate for ≥30 days without manual intervention (verified by 24-hour soak proxy test). |
| NFR-2.3 | Must | Configuration data shall not be lost across any reboot or power cycle. |

#### Resources

| ID | Priority | Requirement |
|----|----------|-------------|
| NFR-3.1 | Should | Average CPU utilization shall remain below 40% during normal clock operation. |
| NFR-3.2 | Should | Free heap shall remain above 20 KB during normal operation. |

### 4.3 Constraints

| ID | Constraint |
|----|-----------|
| C-1 | Firmware shall be built exclusively with ESP-IDF (no Arduino framework). |
| C-2 | All device flashing and automated testing shall use the ESP32 Workbench toolchain. |
| C-3 | Source code shall be maintained in the GitHub repository `https://github.com/JMBMdes/Clock`. |

---

## 5. Risks, Assumptions & Dependencies

### 5.1 Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| GC9A01 driver incompatible with chosen LVGL ESP-IDF component | Medium | High | Validate driver in Phase 1 before committing to it; substitute SPI driver if needed |
| NTP unreachable in target environment (firewall, no internet) | Low | Medium | Allow custom NTP server entry in portal; display continues with `esp_timer` drift |
| LVGL render time blocks clock task beyond 1 s window | Low | Medium | Use LVGL on flush-callback + SPI DMA; profile in Phase 1 |
| WDT false-trigger during WiFi reconnect storm | Medium | Medium | Extend TWDT timeout during reconnect; decouple watchdog from WiFi state |
| SPI bus contention with other peripherals | Low | Low | Dedicate SPI2 exclusively to the display |

### 5.2 Assumptions

- Display is GC9A01-based circular TFT, 240×240 px, connected via SPI. (assumed)
- Default clock face style is analog with three hands. (assumed)
- CONFIG / factory-reset button is connected to GPIO0 (BOOT button on DevKit). (assumed)
- Timezone is configured as a POSIX TZ string (e.g., `BRT3` for Brasília UTC-3). (assumed)
- No hardware RTC (DS3231 etc.) — system relies entirely on SNTP and `esp_timer`.
- No OTA firmware update in Phase 1 or Phase 2; deferred to a future release.
- No alarm, timer, or secondary time-zone display.

### 5.3 External Dependencies

| Dependency | Owner | Notes |
|-----------|-------|-------|
| `pool.ntp.org` | Public NTP Pool Project | Requires internet access; configurable to a private server |
| ESP32 Workbench | SensorsIot / Developer | Flashing and automated test execution |
| GitHub (`https://github.com/JMBMdes/Clock`) | Developer | Source control |
| LVGL v8.x | LVGL project | Apache-2.0 licensed; ESP-IDF component available via IDF Component Manager |

---

## 6. Interface Specifications

### 6.1 External Interfaces

#### SNTP / NTP

| Field | Value |
|-------|-------|
| Protocol | SNTP (RFC 4330) over UDP |
| Port | 123 |
| Server | Configurable (default: `pool.ntp.org`) |
| Resync interval | 3600 s (60 min) |
| ESP-IDF component | `esp_sntp` |

#### WiFi

| Field | Value |
|-------|-------|
| STA mode | WPA2/WPA3 Personal |
| AP SSID format | `CLOCK-{MAC_LAST_4}` |
| AP authentication | Open (for ease of provisioning) (assumed) |
| AP IP | 192.168.4.1 |
| AP DHCP range | 192.168.4.2–192.168.4.10 |

#### Captive Portal HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Redirect to `/config` |
| `/config` | GET | Render configuration form (WiFi, NTP, timezone, brightness) |
| `/config` | POST | Validate and save configuration to NVS; reboot |
| `/scan` | GET | Return JSON array of scanned SSIDs with RSSI |
| `/reset` | POST | Factory reset: erase NVS and reboot |

**POST `/config` payload (application/x-www-form-urlencoded):**
```
ssid=<SSID>&password=<PSK>&ntp_server=<hostname>&timezone=<POSIX_TZ>&brightness=<0-100>
```

### 6.2 Internal Interfaces

#### `clock_task` → LVGL
- `clock_task` calls LVGL arc/line APIs each second to update hand angles.
- LVGL marks dirty regions and flushes via SPI DMA on its flush callback.
- LVGL and `clock_task` share a mutex to prevent render/update races.

#### SNTP → `clock_task`
- `clock_task` reads system time via `time()` / `localtime_r()`.
- Timezone is applied globally with `setenv("TZ", tz_string, 1)` + `tzset()`.
- Sync state communicated via FreeRTOS event group bit `SNTP_SYNCED_BIT`.

### 6.3 Data Models / Schemas

**NVS namespace: `clock_cfg`**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `wifi_ssid` | String | — | WiFi network SSID |
| `wifi_pass` | Blob (NVS encrypted) | — | WiFi password |
| `ntp_server` | String | `pool.ntp.org` | NTP server hostname |
| `timezone` | String | `UTC0` | POSIX TZ string |
| `brightness` | U8 | 80 | Backlight brightness (0–100) |

---

## 7. Operational Procedures

### 7.1 First-Time Setup (Provisioning)

1. Power on the device.
2. Device enters AP mode automatically (no credentials in NVS). Display shows "Setup mode".
3. On a phone or laptop, connect to WiFi SSID `CLOCK-{MAC_LAST_4}`.
4. Open a browser; the captive portal configuration page appears automatically.
5. Select the home WiFi network from the scan list; enter the password.
6. Optionally set NTP server, timezone, and brightness.
7. Submit the form. Device saves config to NVS and reboots.
8. Device connects to the configured WiFi network and syncs time via SNTP.

### 7.2 Reconfiguration (Change WiFi or Settings)

1. Hold the CONFIG button for 5 seconds; display shows "Setup mode".
2. AP mode activates with SSID `CLOCK-{MAC_LAST_4}`.
3. Follow Steps 3–8 from Section 7.1.

### 7.3 Factory Reset

1. Hold the CONFIG button for 5 seconds until the display shows "Resetting…".
2. Release the button. NVS is erased and the device reboots into AP mode.
3. Reprovision using Section 7.1.

### 7.4 Normal Operation

1. Device powers on, reads NVS config, connects to WiFi.
2. SNTP sync completes; display shows accurate local time.
3. Clock face updates every second; SNTP resync occurs every 60 minutes.
4. No user interaction is required during normal operation.

### 7.5 Flashing & Monitoring (Developer)

```bash
# Build and flash via ESP32 Workbench (RFC2217 remote serial)
idf.py build
idf.py -p rfc2217://workbench.local:4000 flash monitor

# Erase NVS partition
esptool.py --port rfc2217://workbench.local:4000 erase_region 0x9000 0x6000
```

### 7.6 Error Recovery Reference

| Condition | Device Behavior |
|-----------|----------------|
| WiFi disconnection | Auto-reconnect with exponential backoff; clock continues via `esp_timer` |
| NTP sync failure | Retain last synced time; display unsynced indicator; retry at next interval |
| Watchdog timeout | Automatic reboot; reconnects and resyncs |
| NVS corrupt on boot | Apply defaults; enter AP mode for reconfiguration |
| Heap critical (< 10 KB) | Preventive reboot logged to serial |

---

## 8. Verification & Validation

### 8.1 Phase 1 Verification

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-DISP-100 | Display initialization | Flash firmware; power on; inspect display and serial | Display illuminates cleanly; no garbage pixels; no IDF error in serial |
| TC-DISP-101 | Clock face rendering | Observe display after boot | Analog clock face with three hands and date area rendered correctly on circular screen |
| TC-DISP-102 | 1 Hz update | Let device run 60 s; count second-hand steps | Exactly 60 steps in 60 s; per-step jitter < 50 ms |

### 8.2 Phase 2 Verification

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-NTP-100 | Initial SNTP sync | Connect to WiFi; observe serial and display | "SNTP synced" in serial within 5 s; display shows correct local time |
| TC-NTP-101 | Periodic resync | Run device 65 min; monitor serial | Second sync event logged at ~60 min mark |
| TC-NTP-102 | SNTP failure handling | Block UDP port 123 at router; power on | Display shows "syncing" indicator; retries logged; no crash |
| TC-NTP-103 | Timezone configuration | Set timezone to `BRT3` via portal; verify display | Time displayed is UTC-3; matches reference |
| TC-DISP-103 | Sync status indicator | Observe display before and after SNTP sync | Unsynced indicator visible before sync; indicator clears after sync |
| WIFI-001 | WiFi STA connect | Configure SSID via portal; power on | Device associates; DHCP IP assigned |
| WIFI-003 | Auto-reconnect | Disable AP; wait 30 s; restore AP | Device reconnects; no reboot required; NTP resyncs |
| TC-CP-100 | First-boot portal | Erase NVS; power on; connect phone to AP; configure | Portal accessible; credentials saved; device reboots and connects |
| TC-CP-101 | Reconfiguration via button | Hold CONFIG 5 s while running | AP mode activates; portal accessible; new config saved |
| TC-CP-102 | Network scan in portal | Open portal WiFi page | Available SSIDs listed with RSSI values |
| EC-CP-203 | Portal input validation | Submit empty SSID and password < 8 chars | Validation errors returned; config not saved |
| TC-NVS-100 | Config persistence | Configure device; reboot; check all values | All values identical before and after reboot and power cycle |
| TC-NVS-101 | First-boot defaults | Erase NVS; boot | Serial shows default values; device enters AP mode |
| TC-NVS-102 | Factory reset | Hold CONFIG 5 s | NVS cleared; device enters AP mode; credentials gone |
| EC-NVS-203 | Credential security | Configure WiFi; monitor all serial output | No WiFi password appears in plaintext in serial logs |

### 8.3 Phase 3 Verification

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-WDT-100 | WDT task-hang detection | Flash test firmware that stops `clock_task` heartbeat | TWDT triggers within 60 s + margin; device reboots and resumes |
| TC-WDT-101 | Hardware WDT failsafe | Disable TWDT feed from `watchdog_task` (test firmware) | Hardware WDT triggers panic and reboots |
| TC-WDT-102 | Heap monitor | Repeatedly allocate memory via test firmware | Warning logged at < 30 KB; reboot at < 10 KB |
| EC-WDT-200 | WDT stability during WiFi loss | Disconnect WiFi AP for 5 min | No TWDT false-trigger; reconnects and resyncs when AP restored |
| TC-PERF-100 | CPU utilization | Run `xTaskGetRunTimeStats()` during 10 min normal operation | Average CPU < 40% across all tasks |
| TC-SOAK-100 | 24-hour soak | Run device unattended 24 hours with production firmware | No crashes; no manual intervention; clock accurate ±1 s at end |

### 8.4 Acceptance Tests

**AT-1: End-to-End Provisioning and Accuracy**
1. Start with factory-erased device (NVS blank).
2. Complete full provisioning via captive portal (WiFi + timezone).
3. Observe clock face for 5 minutes; compare to reference NTP source.
4. Pass criteria: displayed time matches reference within ±2 seconds; date is correct.

**AT-2: Disconnection and Recovery**
1. Device running normally in clock mode.
2. Physically disable the WiFi AP for 10 minutes.
3. Restore the AP.
4. Pass criteria: device reconnects within 60 s; clock accuracy recovers to ±1 s after SNTP resync; no user action required.

**AT-3: Long-Run Stability (Soak)**
1. Run device unattended in normal environment for 24 hours.
2. Pass criteria: no crashes, no manual intervention, clock accurate ±1 s at end of run; serial log shows no error-level events.

### 8.5 Traceability Matrix

| Requirement | Priority | Test Case(s) | Status |
|------------|----------|-------------|--------|
| FR-1.1 | Must | TC-DISP-100, TC-DISP-101 | Covered |
| FR-1.2 | Must | TC-DISP-102 | Covered |
| FR-1.3 | Should | TC-DISP-101 | Covered |
| FR-1.4 | Should | TC-DISP-101 | Covered |
| FR-1.5 | Should | TC-DISP-103 | Covered |
| FR-1.6 | May | — | Optional |
| FR-2.1 | Must | TC-NTP-100 | Covered |
| FR-2.2 | Must | TC-NTP-100, TC-NVS-100 | Covered |
| FR-2.3 | Must | TC-NTP-103 | Covered |
| FR-2.4 | Should | TC-NTP-101 | Covered |
| FR-2.5 | Should | TC-NTP-102, TC-DISP-103 | Covered |
| FR-2.6 | Should | TC-NTP-100, TC-NTP-102 | Covered |
| FR-3.1 | Must | WIFI-001, AT-1 | Covered |
| FR-3.2 | Must | EC-NVS-203 | Covered |
| FR-3.3 | Must | WIFI-003, AT-2 | Covered |
| FR-3.4 | Should | WIFI-003 | Covered |
| FR-3.5 | Must | WIFI-001 | Covered |
| FR-4.1 | Must | TC-CP-100, TC-NVS-101 | Covered |
| FR-4.2 | Must | TC-CP-101 | Covered |
| FR-4.3 | Should | TC-CP-100 | Covered |
| FR-4.4 | Should | TC-CP-100 | Covered |
| FR-4.5 | Must | TC-CP-100 | Covered |
| FR-4.6 | Must | TC-CP-100 | Covered |
| FR-4.7 | Should | TC-CP-100 | Covered |
| FR-4.8 | Should | EC-CP-203 | Covered |
| FR-4.9 | Must | TC-NVS-100 | Covered |
| FR-4.10 | May | — | Optional |
| FR-5.1 | Must | TC-NVS-100 | Covered |
| FR-5.2 | Must | TC-NVS-100 | Covered |
| FR-5.3 | Must | TC-NVS-101 | Covered |
| FR-5.4 | Must | TC-NVS-102 | Covered |
| FR-5.5 | Should | TC-NVS-102 | Covered |
| FR-5.6 | Must | TC-NVS-102 | Covered |
| FR-5.7 | Must | EC-NVS-203 | Covered |
| FR-6.1 | Must | TC-WDT-100 | Covered |
| FR-6.2 | Must | TC-WDT-100 | Covered |
| FR-6.3 | Must | EC-WDT-200 | Covered |
| FR-6.4 | Should | TC-WDT-101 | Covered |
| FR-6.5 | Should | TC-WDT-102 | Covered |
| FR-6.6 | Should | TC-WDT-102 | Covered |
| NFR-1.1 | Must | TC-DISP-102 | Covered |
| NFR-1.2 | Should | TC-NTP-100 | Covered |
| NFR-1.3 | Should | TC-NTP-100, TC-SOAK-100 | Covered |
| NFR-2.1 | Must | WIFI-003, AT-2 | Covered |
| NFR-2.2 | Should | TC-SOAK-100, AT-3 | Covered |
| NFR-2.3 | Must | TC-NVS-100 | Covered |
| NFR-3.1 | Should | TC-PERF-100 | Covered |
| NFR-3.2 | Should | TC-WDT-102 | Covered |

---

## 9. Troubleshooting Guide

| Symptom | Likely Cause | Diagnostic Steps | Corrective Action |
|---------|-------------|-----------------|-------------------|
| Display stays black after power-on | SPI wiring error or wrong GPIO config | Check serial for "display init failed"; verify pinout | Re-check wiring against Section 2.2 pinout table; correct GPIO in `sdkconfig` |
| `CLOCK-xxxx` AP SSID not visible | Device failed to enter AP mode | Monitor serial; check for NVS or WiFi init errors | Erase NVS; power-cycle; verify CONFIG button GPIO |
| Time shows 00:00:00 or Unix epoch | SNTP sync never completed | Check serial for SNTP events; verify UDP 123 is not blocked | Confirm internet access; try alternate NTP server via portal |
| Time shows wrong timezone offset | POSIX TZ string incorrect or not applied | Serial: check "TZ set to…" log line at boot | Re-enter correct POSIX TZ string via portal (see Appendix 10.2) |
| Device reboots repeatedly | Watchdog timeout or heap exhaustion | Check serial for `rst:0xc` and heap warning lines | Capture serial log; check for memory leak in `clock_task` |
| Captive portal not accessible | HTTP server not started or IP mismatch | Check serial for "HTTP server started"; ping 192.168.4.1 | Ensure phone is connected to `CLOCK-xxxx` and not home WiFi; power-cycle |
| Clock drifts noticeably between syncs | `esp_timer` drift without SNTP correction | Check if SNTP periodic resync is running (serial) | Verify `FR-2.4` config; reduce resync interval if needed |

---

## 10. Appendix

### 10.1 Configuration Defaults

| Parameter | Default Value |
|-----------|--------------|
| NTP server | `pool.ntp.org` |
| Timezone | `UTC0` |
| Brightness | 80% |
| SNTP resync interval | 3600 s (60 min) |
| TWDT timeout | 60 s |
| Heap warning threshold | 30 KB |
| Heap critical threshold | 10 KB |
| AP mode auto-off timeout | 300 s (5 min, no client) |
| AP SSID pattern | `CLOCK-{MAC_LAST_4}` |
| AP IP | 192.168.4.1 |

### 10.2 POSIX Timezone String Reference

| Location | POSIX TZ String |
|----------|----------------|
| UTC | `UTC0` |
| Brazil / Brasília (BRT, no DST) | `BRT3` |
| US Eastern (no DST) | `EST5` |
| US Eastern (with DST) | `EST5EDT,M3.2.0,M11.1.0` |
| Central Europe (CET/CEST) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| UK (GMT/BST) | `GMT0BST,M3.5.0/1,M10.5.0` |

### 10.3 ESP-IDF Reset Reason Codes

| Code | Meaning |
|------|---------|
| `rst:0x1` | Power-on reset |
| `rst:0x3` | Software reset (`esp_restart()`) |
| `rst:0xc` | RTC WDT reset (watchdog) |
| `rst:0xf` | Brownout reset |

### 10.4 Useful Developer Commands

```bash
# Build and flash via Workbench RFC2217
idf.py build
idf.py -p rfc2217://workbench.local:4000 flash monitor

# Erase NVS partition only
esptool.py --port rfc2217://workbench.local:4000 erase_region 0x9000 0x6000

# Read NVS partition for inspection
esptool.py --port rfc2217://workbench.local:4000 read_flash 0x9000 0x6000 nvs_dump.bin
```

---

## 11. Related

- [[esp32-workbench-runbook]] — Workbench flashing and test execution procedures
- [[lvgl-esp32-integration]] — LVGL component setup for ESP-IDF
