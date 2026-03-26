# Plan: Restructure Ambyte IoT Firmware Toward DDD

## TL;DR

Incrementally refactor the ESP32 IoT firmware into a Domain-Driven Design architecture by introducing domain-layer abstractions (ports/interfaces), isolating infrastructure behind anti-corruption layers, and enforcing boundaries through CMake dependencies. The approach works within ESP-IDF's component model — no framework overhead, no big-bang rewrite.

---

## Current State Assessment

### DDD Layer Mapping (As-Is)

| Component | Implicit Layer | Clean API? | Key Issues |
|-----------|---------------|------------|------------|
| `hal/i2c_bus/` | Infrastructure (Shared Kernel) | ✓ Clean | None — good reference |
| `bme280/` | Infrastructure | ✓ C API clean | Arduino dependency leaked via `I2C_device` |
| `I2C_device/` | Infrastructure | ✗ Leaks Arduino | `TwoWire` in public header |
| `pcf2131tfy_rtc/` | Infrastructure | ✓ C wrapper clean | C++ hierarchy + register enums leaked in headers |
| `wifi_manager/` | Infrastructure | ✓ Clean | No issues |
| `sd_card/` | Infrastructure | ✓✓ Excellent | Best API in project |
| `certs/` | Infrastructure | ✓ Clean | Isolated config |
| `ambyte_status/` | Interface | ✓ Clean | No issues |
| `lua_runner/` | Application | ✗ | Public REQUIRES on 3 components; no module registration |
| `CLI/` | Interface | ✗ | PRIV_REQUIRES 3 sensors; hardcoded I2C addresses |
| `main/app_main.c` | Application | ✗ | REQUIRES all components; orchestration mixed with init |

### Key Dependency Violations
- **CLI → sensors directly** (interface layer bypasses application/domain)
- **lua_runner → sensors + storage directly** (application bypasses domain ports)
- **app_main → everything** (no dependency inversion)
- **BME280 → I2C_device → Arduino** (infrastructure leak into build)

---

## Bounded Context Map

| Bounded Context | Domain Concepts | Current Components |
|---|---|---|
| **Sensing** | Measurement, SensorReading, SensorId | `bme280/`, `pcf2131tfy_rtc/` |
| **Connectivity** | Connection, NetworkCredentials | `wifi_manager/`, `certs/` |
| **Persistence** | MeasurementLog, LogEntry, MeasurementRecord | `sd_card/`, `sqlite3` (new) |
| **Automation** | Script, MeasurementJob | `lua_runner/`, `lua/` |
| **Device Management** | DeviceStatus, Command | `CLI/`, `ambyte_status/` |

Relationships:
- Automation *consumes* Sensing (reads measurements) and *uses* Persistence (writes logs)
- Device Management *queries* Sensing and *controls* Device Status
- Sensing produces domain events (new measurement available)
- Persistence *subscribes* to domain events from Sensing

---

## Phase 0: Clean Up Redundancies

Remove dead code, unused files, and redundant abstraction layers before restructuring. This reduces noise and makes subsequent phases cleaner.

### Phase 0A: Quick Wins (zero risk)

0A.1. Delete `src/` folder — empty, only contains an auto-generated CMakeLists.txt
0A.2. Delete `dom_ludo_prototype_ambyte_thing_certs/` — leftover cert archive + macOS metadata; real certs are in `components/certs/`
0A.3. Remove dead `sensor_report_task()` function (lines ~31–85) and its commented-out call (`// app_start_sensor_task();` line ~227) from `main/app_main.c`
0A.4. Add `build/`, `temp_inipreview.txt` to `.gitignore`
0A.5. Move `AI prompts.txt`, `MQTT TLS test client.py`, `overall_architecture.txt` to `docs/` folder
0A.6. Update `platformio.ini`: change `board` to `esp32-s3-devkitm-1`, `board_upload.flash_size` to `16MB`, remove `arduino` from frameworks (only `espidf`), update `board_upload.maximum_size` to `16777216`, add `board_build.partitions = partitions.csv`
0A.7. Update `sdkconfig.defaults`: change `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y` to `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, change `CONFIG_ESPTOOLPY_FLASHSIZE="2MB"` to `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`
0A.8. Create `partitions.csv` in project root — custom partition table for 16MB flash:
  ```
  # Name,   Type, SubType, Offset,   Size,    Flags
  nvs,      data, nvs,     0x9000,   0x6000,
  phy_init, data, phy,     0xf000,   0x1000,
  factory,  app,  factory, 0x10000,  0x300000,
  storage,  data, fat,     0x310000, 0xCF0000,
  ```
  - factory app partition: 3MB (ample for firmware + SQLite + NimBLE + MQTT + Lua)
  - storage partition: ~13MB (available for future use, e.g., OTA slot or SPIFFS)
  - NVS: 24KB (WiFi credentials, device config)

### Phase 0B: Eliminate I2C_device Component (medium effort)

The `I2C_device/` component is an Arduino `Wire`-based C++ shim used **only** by `bme280_arduino_shim.cpp`. The RTC already demonstrates the correct pattern: direct I2C via `i2c_bus` without Arduino. Removing `I2C_device` also removes the dependency on `framework-arduinoespressif32`.

0B.1. Refactor `components/bme280/bme280_arduino_shim.cpp` — replace `I2C_device`-based I2C access with direct `i2c_bus_lock()` + `i2c_master_cmd_begin()` calls (follow the pattern in `PCF2131_I2C.cpp`)
0B.2. Remove `I2C_device` from `bme280/CMakeLists.txt` PRIV_REQUIRES
0B.3. Remove `framework-arduinoespressif32` from `bme280/CMakeLists.txt` PRIV_REQUIRES (and from `platformio.ini` frameworks if no other component needs it)
0B.4. Delete entire `components/I2C_device/` directory
0B.5. Verify `pio run` succeeds and BME280 init + read still work

### Phase 0C: Flatten PCF2131 RTC Hierarchy (medium effort, *parallel with 0B*)

The 3-class hierarchy (`RTC_NXP` → `PCF2131_base` → `PCF2131_I2C`) is over-engineered: only one concrete class is ever instantiated and the public API is already C-only.

0C.1. Merge `RTC_NXP` and `PCF2131_base` into `PCF2131_I2C` — keep only the methods actually called by `pcf2131tfy_rtc_api.cpp` (init, set, get time, oscillator_stop)
0C.2. Delete `RTC_NXP.cpp`/`.h` and `PCF2131_base.cpp`/`.h` files
0C.3. Move the merged `PCF2131_I2C.h` from `include/` to the source directory (private) — only `pcf2131tfy_rtc_api.h` remains public
0C.4. Remove dead register enums and virtual methods not used by the C API
0C.5. Verify `pio run` succeeds and RTC init + time read still work

### Phase 0D: Miscellaneous (optional, *parallel with 0B/0C*)

0D.1. Remove `#if defined(BME280_ENABLE_ADAFRUIT_SENSOR_API)` ifdef block from `bme280_adafruit_private.hpp` — flag is never set

**Verification for Phase 0:** `pio run` succeeds. Flash and confirm all features (BME280, RTC, LED, WiFi, CLI, Lua) still work.

---

## Phase 1: Domain Layer — Introduce Ports (Header-Only Contracts)

Create a `components/domain/` component containing only header files that define the contracts (ports) each bounded context exposes. No implementation, no ESP-IDF deps.

**Steps:**

1.1. Create `components/domain/` with `CMakeLists.txt` (`REQUIRES esp_common` — needed for `esp_err_t` type; this is purely type definitions, no infrastructure dependency)

1.2. Define **Sensing port** — `components/domain/include/sensing_port.h`:
  - `typedef struct { float temperature_c; float humidity_percent; float pressure_pa; } measurement_t;`
  - `typedef esp_err_t (*sensor_read_fn)(measurement_t *out);`
  - `typedef esp_err_t (*clock_read_fn)(time_t *out);`
  - This replaces direct includes of `bme280.h` and `pcf2131tfy_rtc_api.h` in consumers

1.3. Define **Persistence port** — `components/domain/include/persistence_port.h`:
  - `typedef struct { int64_t sensor_id; int64_t measure_id; char measure_type[32]; time_t timestamp; char data_type[32]; float value; } measurement_record_t;`
  - Fixed-size char arrays avoid heap allocation and ownership ambiguity. SQLite callbacks `strncpy` directly into these fields. ~88 bytes per struct; queries capped at 64 records max (~5.5KB).
  - **sensorID**: identifies the physical sensor instance (e.g., device-prefixed, from `channel_config`). Distinct from measureID which groups data points within a single reading.
  - **Device-prefixed measureID**: `measure_id` format = `<device_id_prefix> * 1000000 + <sequence>`. E.g., device 1 → IDs 1000001, 1000002, ...; device 2 → 2000001, etc. The prefix is set at init time via configuration (NVS or compile-time define). This allows merging DBs from multiple devices without ID collisions.
  - EAV schema: one row per data point, grouped by `measure_id` + `measure_type`. Example: a single BME280 reading produces 3 rows with the same `measure_id` and `measure_type="bme280"`, each with a different `data_type` ("temperature_c", "humidity_pct", "pressure_pa").
  - Repository operations:
    - `typedef esp_err_t (*measurement_store_fn)(const measurement_record_t *records, size_t count);` — stores a batch of records sharing the same measure_id
    - `typedef esp_err_t (*measurement_query_fn)(const char *measure_type, time_t from, time_t to, measurement_record_t *out, size_t max, size_t *count);`
    - `typedef esp_err_t (*measurement_count_fn)(const char *measure_type, size_t *count);`
    - `typedef esp_err_t (*measurement_next_id_fn)(int64_t *out_id);` — returns next device-prefixed measure_id
  - This replaces the old text-file approach and supports any sensor type without schema changes

1.4. Define **Device Status port** — `components/domain/include/device_status_port.h`:
  - `typedef esp_err_t (*status_set_fn)(uint8_t r, uint8_t g, uint8_t b);`

1.5. ~~Sensing service~~ — **REMOVED from domain/**: `sensing_ctx_t` and its registration functions are application-layer orchestration, not pure domain. Moved into `device_commands/` (Phase 3.1) where `device_commands_config_t` aggregates all ports directly. `domain/` stays truly header-only (port typedefs only, no .c files).

**Verification:** `domain/` component builds with `REQUIRES esp_common` only (no SRCS), headers compile standalone.

---

## Phase 2: Adapt Infrastructure — Implement Ports

Wrap existing components behind port interfaces without rewriting their internals.

**Steps:**

2.1. **BME280 adapter** — modify `components/bme280/` to add a function that returns a `sensor_read_fn` conforming to the sensing port. The existing `bme280_read()` already matches closely; add a thin adapter in `bme280.c`. Add `REQUIRES domain` to its CMakeLists.txt. (I2C_device already eliminated in Phase 0B)

2.2. **RTC adapter** — modify `components/pcf2131tfy_rtc/` similarly: add a `clock_read_fn` adapter around `pcf2131tfy_rtc_get_time()`. Add `REQUIRES domain`. (Hierarchy already flattened in Phase 0C)

2.3. **SD card + SQLite adapter** — Refactor `components/sd_card/` to implement the persistence port using SQLite:
  - **Prerequisite**: Add `sd_card` to `main/CMakeLists.txt` REQUIRES. Add `sdcard_init_default()` + `sdcard_mount()` calls in `app_main.c` after NVS init and before any SQLite access. Currently app_main never initializes the SD card.
  - Add `siara-cc/esp32-idf-sqlite3` as a git submodule under `components/sqlite3/` (pure ESP-IDF, no Arduino, 173 stars, Apache-2.0)
  - Add a new source file `sd_card_sqlite.c` that implements `measurement_store_fn`, `measurement_query_fn`, and `measurement_count_fn`:
    - On init: `sqlite3_open("/sdcard/measurements.db", &db)`, create table if not exists
    - Table schema: `CREATE TABLE IF NOT EXISTS measurements (sensorID INTEGER NOT NULL, measureID INTEGER NOT NULL, measureType TEXT NOT NULL, timestamp INTEGER NOT NULL, dataType TEXT NOT NULL, dataValue REAL, PRIMARY KEY (measureID, dataType));`
    - Index: `CREATE INDEX IF NOT EXISTS idx_type_ts ON measurements (measureType, timestamp);`
    - Config table: `CREATE TABLE IF NOT EXISTS channel_config (channel TEXT PRIMARY KEY, sensor_name TEXT NOT NULL);`
    - `measurement_store_fn` → batch `INSERT INTO measurements (sensorID, measureID, measureType, timestamp, dataType, dataValue) VALUES (?, ?, ?, ?, ?, ?);` inside a transaction
    - `measurement_query_fn` → `SELECT * FROM measurements WHERE measureType = ? AND timestamp BETWEEN ? AND ? ORDER BY measureID, dataType;`
    - `measurement_count_fn` → `SELECT COUNT(DISTINCT measureID) FROM measurements WHERE measureType = ?;`
    - `measurement_next_id_fn` → device-prefixed: `SELECT COALESCE(MAX(measureID), <device_prefix> * 1000000) + 1 FROM measurements;`
  - Remove old text-file functions (`sdcard_write_line`, `sdcard_append_file`, `sdcard_read_line_at`, etc.) — all persistence goes through SQLite
  - Increase `max_files` from 5 to 8 (SQLite may need multiple file handles concurrently)
  - Add `REQUIRES domain sqlite3` to `sd_card/CMakeLists.txt`
  - **Thread safety**: SQLite compiled with `SQLITE_THREADSAFE=2` (multi-thread mode). `siara-cc/esp32-idf-sqlite3` custom VFS does NOT provide file-locking. Solution: add a dedicated `s_sqlite_mutex` (`SemaphoreHandle_t`) in `sd_card_sqlite.c`. All `measurement_store_fn`, `measurement_query_fn`, `measurement_count_fn` implementations take this mutex before any `sqlite3_*` call and release after. Separate from SD card mount/unmount mutex (different granularity). This serializes all DB access — acceptable given short writes and periodic reads. Prevents data corruption when Core 0 (MQTT publish_measurement) and Core 1 (Lua db.store) access DB concurrently.

2.4. **Status adapter** — `ambyte_status` already fits. Add a function returning `status_set_fn`.

2.5. *(Moved to Phase 0C)* ~~Hide RTC internals~~

2.6. *(Moved to Phase 0B)* ~~Hide I2C_device~~

**Verification:** All existing code still compiles; no public API removals, only additions.

---

## Phase 3: Shared Command Layer — Lua as the Single Command Engine

CLI and Lua currently implement overlapping operations independently. Instead of maintaining two command implementations, **Lua becomes the single command engine**. CLI is reduced to a thin REPL that dispatches to Lua. The one exception is `i2cscan` (low-level I2C bus probe), which stays as a native C function but is also exposed to Lua.

### Design

**CLI = thin Lua REPL:**
- User types a command in CLI → CLI wraps it as a Lua call → Lua executes → result printed
- Example: user types `store` → CLI runs `device.store()` in Lua → result returned as string → CLI prints it
- No command logic in `CLI.c` anymore — only the REPL loop and `i2cscan` handler

**`i2cscan` stays native + exposed to Lua:**
- Requires direct I2C bus lock/probe — not practical in Lua
- Implemented as `cmd_i2c_scan()` in `device_commands`
- Exposed to Lua as `device.i2c_scan()`
- CLI keeps it as the only native command

**Lua gains all capabilities:**
- `device.set_rgb(r, g, b)` — LED control
- `device.read_rtc()` — RTC time
- `device.status()` — device status (sensors ready, WiFi, etc.)
- `device.read_env()` — BME280 environmental reading
- `device.i2c_scan()` — I2C bus scan (new — from CLI)
- `db.store()` — store measurement to SQLite
- `db.query(type, from, to)` — query measurements
- `db.count(type)` — measurement count
- `device.uart_query(channel, cmd, timeout)` — UART sensor query
- `device.uart_ping(channel)` — UART health check
- `device.uart_status()` — all channel states
- `device.mqtt_publish(topic, payload)` — MQTT publish
- `device.mqtt_status()` — MQTT connection status
- `device.sleep_ms(ms)` — task delay
- `device.log(msg)` — ESP_LOGI logging

### Current Command Inventory

| Operation | Old CLI | After: CLI dispatches to Lua | Lua binding |
|---|---|---|---|
| I2C scan | `i2cscan` (native, kept) | `i2cscan` stays native | `device.i2c_scan()` (new) |
| Set RGB LED | `red 0\|1` | removed — use Lua | `device.set_rgb(r,g,b)` |
| RTC read | `rtc` | removed — use Lua | `device.read_rtc()` |
| Sensor status | `status` | removed — use Lua | `device.status()` |
| Ping | `ping` | removed | — |
| Exit | `exit` | removed | — |
| BME280 read | — | — | `device.read_env()` |
| Store measurement | — | — | `db.store()` |
| Query measurements | — | — | `db.query(type, from, to)` |
| Count measurements | — | — | `db.count(type)` |
| UART query | — | — | `device.uart_query(ch, cmd, timeout)` |
| UART ping | — | — | `device.uart_ping(ch)` |
| MQTT publish | — | — | `device.mqtt_publish(topic, payload)` |

### Command function pattern

```
typedef struct {
    esp_err_t status;       // ESP_OK or error
    char message[128];      // Human-readable result or error text
} cmd_result_t;

cmd_result_t cmd_set_rgb(uint8_t r, uint8_t g, uint8_t b);
cmd_result_t cmd_read_rtc(time_t *out_time);
cmd_result_t cmd_device_status(bool *bme_ready, bool *rtc_ready, time_t *rtc_time);
cmd_result_t cmd_i2c_scan(uint8_t *found_addrs, size_t *count, size_t max);
cmd_result_t cmd_read_env(float *temp, float *hum, float *pres);
cmd_result_t cmd_log(const char *msg);
cmd_result_t cmd_store_measurement(void);            // reads sensors + RTC, auto-assigns measureID, stores EAV rows to SQLite
cmd_result_t cmd_query_measurements(const char *measure_type, time_t from, time_t to, ...);  // retrieves records filtered by type + time range
cmd_result_t cmd_measurement_count(const char *measure_type, size_t *count);   // distinct measureID count by type
cmd_result_t cmd_sleep_ms(uint32_t ms);
```

**CMakeLists.txt:** `REQUIRES domain` only (uses domain ports, not concrete components)

### Adapters

**CLI adapter** (in `CLI.c`):
- Thin REPL with a static dispatch table mapping command names to Lua C API calls
- Only native handler: `i2cscan` → calls `cmd_i2c_scan()` directly, prints formatted output
- All other commands: CLI parses command name + args → looks up dispatch table → calls handler that uses `lua_getglobal()` + `lua_pushinteger()`/`lua_pushstring()` + `lua_pcall()` (no string concatenation, no `luaL_dostring()` — zero injection risk)
- Example: user types `set_rgb 255 0 0` → CLI handler calls `lua_getglobal(L, "device")`, `lua_getfield(L, -1, "set_rgb")`, pushes 3 integers, `lua_pcall(L, 3, 1, 0)` → prints return value
- Dispatch table: `static const struct { const char *name; int (*handler)(lua_State *L, int argc, char **argv); } cli_commands[]`

**Lua adapter** (in `lua_runner.c`):
- Each Lua binding calls a `cmd_*` function
- On success: pushes result to Lua stack
- On failure (non-critical ops like DB queries): returns `nil, result.message`
- On failure (critical ops like set_rgb): calls `luaL_error(L, "%s", result.message)`
- Lua is the single source of command logic — CLI delegates to it

### Steps

3.1. Create `components/device_commands/` with `cmd_result_t` struct and all command functions listed above. `REQUIRES domain` only — receives all port function pointers via a config struct:
  ```
  typedef struct {
      sensor_read_fn   read_env;        // Phase 2
      clock_read_fn    read_clock;      // Phase 2
      measurement_store_fn  store;      // Phase 2
      measurement_query_fn  query;      // Phase 2
      measurement_count_fn  count;      // Phase 2
      measurement_next_id_fn next_id;   // Phase 2
      status_set_fn    set_status;      // Phase 2
      message_publish_fn publish;       // Phase 5, NULL if unavailable
      uart_sensor_query_fn uart_query;  // Phase 7, NULL if unavailable
      power_read_fn    read_power;      // Phase 8, NULL if unavailable
  } device_commands_config_t;
  ```
  `device_commands_init(const device_commands_config_t *cfg)` — single pointer arg. Each `cmd_*` function checks if its port is NULL before use (returns `ESP_ERR_NOT_SUPPORTED`). New ports are added as struct fields without changing the function signature.

3.2. **Build Lua bindings** — `lua_runner/CMakeLists.txt`:
  - Remove REQUIRES `ambyte_status pcf2131tfy_rtc sd_card`
  - Add REQUIRES `device_commands`
  - Register all `device.*` and `db.*` bindings calling `cmd_*()` functions
  - Add `device.i2c_scan()` binding (wraps `cmd_i2c_scan()`)

3.3. **Reduce CLI to dispatch-table REPL** — `CLI/CMakeLists.txt`:
  - Remove PRIV_REQUIRES `bme280 pcf2131tfy_rtc ambyte_status`
  - Add REQUIRES `device_commands lua_runner lua`
  - Strip all command handlers except `i2cscan`
  - Add dispatch table: static array mapping command name strings to handler functions
  - Each handler uses Lua C API (`lua_getglobal`, `lua_pushinteger`, `lua_pcall`) — no string interpolation, no `luaL_dostring()` for user input
  - Keep `i2cscan` as native: `cmd_i2c_scan()` → printf formatted output

3.4. **Rewire app_main.c** — becomes the **composition root**:
  - Initialize infrastructure components (sensors, sd, wifi)
  - Create domain port adapters (function pointers)
  - Call `device_commands_init()` with port adapters
  - Start Lua runner (registers all `device.*` / `db.*` bindings)
  - Start CLI (thin REPL + i2cscan)
  - `main/CMakeLists.txt` REQUIRES all infra components (correct — composition root wires everything)

**Verification:** `pio run` succeeds. CLI `i2cscan` works natively. All other commands work via Lua dispatch from CLI. Lua scripts produce same behavior.

---

## Phase 4: Enforce Boundaries in Build System

**Steps:**

4.1. Audit all `CMakeLists.txt` after refactoring — verify:
  - `domain/` REQUIRES only `esp_common` (type definitions only — no infra deps)
  - `device_commands/` REQUIRES only `domain`
  - Infra components (`bme280`, `sd_card`, etc.) REQUIRES `domain` + their drivers
  - `CLI/`, `lua_runner/` REQUIRES only `device_commands` (or `domain`)
  - Only `main/` REQUIRES everything (composition root)

4.2. Document the layer rules in a `.github/copilot-instructions.md` or project README section

**Verification:** Intentionally add a wrong dependency (e.g., `domain` REQUIRES `bme280`) and confirm build fails or is caught in review.

---

## Optional (Future): Full Ubiquitous Language Rename

Rename public APIs to use domain language. This is non-essential but improves readability.

| Current | Proposed |
|---------|----------|
| `bme280_read()` | `env_sensor_read()` (or keep behind port) |
| `pcf2131tfy_rtc_get_time()` | `rtc_read_time()` (or keep behind port) |
| `sdcard_write_line()` | `measurement_log_append()` |
| `sensor_report_task()` | `measurement_collection_task()` |
| `ambyte_status_set_rgb()` | `device_status_indicate()` |

**Excluded from plan**: This is cosmetic and can be done incrementally. Ports abstract the names already.

---

## Phase 5: MQTT Publisher/Subscriber

Add basic MQTT connectivity — publish measurement data and subscribe to commands. MQTT v5 is already enabled in sdkconfig (`CONFIG_MQTT_PROTOCOL_5=y`). Target broker: AWS IoT Core (mutual TLS).

### Bounded Context Addition

| Bounded Context | Domain Concepts | Components |
|---|---|---|
| **Messaging** | Message, Topic, Subscription, PublishResult | `mqtt_client/` (new) |

### Domain Port

5.0. Define **Messaging port** in `components/domain/include/messaging_port.h`:
  - `typedef esp_err_t (*message_publish_fn)(const char *topic, const char *payload, size_t len);`
  - `typedef void (*message_handler_fn)(const char *topic, const char *payload, size_t len);`
  - `typedef esp_err_t (*message_subscribe_fn)(const char *topic, message_handler_fn handler);`

### Steps

5.1. Create `components/mqtt_client/` — wraps ESP-MQTT (`esp_mqtt_client`):
  - `mqtt_client_init(const char *broker_uri, const char *client_id)` — configures with TLS certs from `components/certs/`
  - `mqtt_client_start()` / `mqtt_client_stop()`
  - Implements `message_publish_fn` and `message_subscribe_fn` ports
  - `REQUIRES domain certs` + `PRIV_REQUIRES mqtt esp_event`

5.2. Add MQTT commands to `device_commands`:
  - `cmd_mqtt_publish(const char *topic, const char *payload)` — publish arbitrary message
  - `cmd_mqtt_publish_measurement(int64_t measure_id)` — query SQLite by measure_id, format as JSON, publish
  - `cmd_mqtt_status()` — return connection status

5.3. Wire in `app_main.c`:
  - After WiFi connects, init MQTT client
  - Pass `message_publish_fn` to `device_commands_init()`
  - Subscribe to a command topic for receiving remote commands

5.4. Provision certs properly — currently empty placeholders in `components/certs/certs.c`. Options:
  - Embed at compile time (simple, current approach — just fill in the PEM strings)
  - Store in NVS and provision via BLE (Phase 6 — more secure)

**Verification:** Publish a test message; verify reception on AWS IoT console or MQTT test client.

---

## Phase 6: BLE Credential Provisioning

### Best Practice: ESP-IDF Wi-Fi Provisioning Manager

ESP-IDF provides a **built-in solution** (`wifi_prov_mgr`) that handles exactly this use case. It is the recommended best practice from Espressif:

**How it works:**
1. On first boot (or when `wifi_prov_mgr_is_provisioned()` returns false):
   - Start BLE GATT server with `wifi_prov_scheme_ble`
   - Espressif provides **free mobile apps** ([Android](https://play.google.com/store/apps/details?id=com.espressif.provble) / [iOS](https://apps.apple.com/in/app/esp-ble-provisioning/id1473590141)) — no custom app needed
   - User connects via app, scans nearby APs, selects SSID, enters password
   - Device receives credentials over encrypted BLE (X25519 + AES-CTR, Security 1)
   - Device attempts WiFi connection; reports success/failure to app
   - On success: credentials saved to NVS automatically, provisioning stops
2. **BLE memory is freed automatically** using `WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`
   - This frees both classic BT and BLE memory (~60-70 KB RAM) when provisioning manager is de-initialized
   - On subsequent boots, BLE is never initialized (credentials found in NVS → skip provisioning entirely)
3. Credentials **persist in NVS** across reboots — never need to re-provision unless NVS is erased

**Key config**: Use `WIFI_PROV_SECURITY_1` with a proof-of-possession (PoP) string printed on device label or shown on LED pattern.

### Steps

6.1. Extend `components/wifi_manager/` — add provisioning mode (no separate `ble_provisioning/` component):
  - Add `wifi_manager_is_provisioned(bool *out)` — checks NVS for stored credentials
  - Add `wifi_manager_start_provisioning(const char *device_name, const char *pop)` — internally calls `wifi_prov_mgr_init()` with BLE scheme, starts provisioning, waits for `WIFI_PROV_CRED_SUCCESS`, then deinits prov mgr (frees BLE RAM) and connects WiFi
  - BLE-specific code lives entirely *inside* `wifi_manager` — consumers never see BLE types
  - Update `wifi_manager/CMakeLists.txt`: add `PRIV_REQUIRES wifi_provisioning protocomm bt` (private — no BLE leak to consumers)

6.2. Modify startup flow in `app_main.c`:
  ```
  app_main()
    ├─ NVS init
    ├─ wifi_manager_init()
    ├─ Check: wifi_manager_is_provisioned()?
    │   ├─ NO → wifi_manager_start_provisioning("AMBYTE", pop_string)
    │   │       Internally: starts BLE, waits for credentials, connects WiFi
    │   │       On success: deinits prov mgr (frees BLE RAM)
    │   └─ YES → wifi_manager_connect_configured() (never inits BT stack → saves ~60KB RAM)
    ├─ Continue normal startup (I2C, sensors, SD, MQTT, CLI, Lua...)
  ```

6.3. Add custom BLE endpoint for device configuration (inside `wifi_manager`):
  - Register `device-config` endpoint via `wifi_prov_mgr_endpoint_create()` inside `wifi_manager_start_provisioning()`
  - Receives a JSON (or protobuf) payload with:
    - MQTT broker config: broker URI, client ID (optional, Phase 5)
    - AMBIT channel metadata (4 channels):
      ```
      {
        "ambit1": { "sensor_name": "MultispeQ v2.0" },
        "ambit2": { "sensor_name": "Soil Moisture Probe" },
        "ambit3": { "sensor_name": "" },
        "ambit4": { "sensor_name": "" }
      }
      ```
    - Empty `sensor_name` means channel is unused/disabled
  - Stores metadata in SQLite config table:
    - `CREATE TABLE IF NOT EXISTS channel_config (channel TEXT PRIMARY KEY, sensor_name TEXT NOT NULL);`
    - Inserted/updated during provisioning, persists on SD card
    - Device commands can query: `cmd_channel_config(uint8_t channel)` returns sensor_name
  - App sends device-config BEFORE WiFi config (per ESP-IDF requirement: custom endpoints must be targeted first)
  - On subsequent boots, metadata is read from SQLite — no BLE needed

6.4. Enable BLE in sdkconfig.defaults:
  - `CONFIG_BT_ENABLED=y`
  - `CONFIG_BT_NIMBLE_ENABLED=y` (NimBLE uses less RAM than Bluedroid)
  - `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` (if PSRAM available)

**Verification:** Flash, observe BLE advertising → connect with ESP BLE Provisioning app → enter WiFi credentials → device connects → reboot → device auto-connects without BLE.

---

## Phase 7: UART Sensor Querying (4 Channels, Non-Blocking)

### Hardware Constraint

ESP32-S3 has **3 hardware UART controllers** (UART0, UART1, UART2). Console uses USB Serial JTAG (not UART), so all 3 hardware UARTs are free. But you need **4 UART channels**. Options:
  - **Option A**: 3 HW UARTs + 1 time-multiplexed (GPIO matrix remap between queries — possible since queries are sequential with 2-min responses)
  - **Option B**: External UART multiplexer IC (e.g., SC16IS752 — 2-channel UART over I2C)
  - **Option C**: Soft UART (bit-banging) for the 4th channel — unreliable at higher baud rates

**Recommendation**: Option A (GPIO matrix remap). Since queries take up to 2 min each, sensors are queried sequentially. One UART controller can be remapped to different pins between queries using `uart_set_pin()`. This needs zero additional hardware.

### Dual-Core Architecture

ESP32-S3 has 2 cores. Pin tasks to specific cores to minimize blocking:

| Core | Tasks | Priority | Rationale |
|---|---|---|---|
| **Core 0** (PRO) | WiFi stack, MQTT client, data management, SQLite writes | High | WiFi requires Core 0; MQTT callbacks run here |
| **Core 1** (APP) | Lua runner, CLI, status LED, UART sensor queries | Mixed | Sensor queries block for up to 2 min; isolated from networking |

### Domain Port

7.0. Define **UART Sensor port** in `components/domain/include/uart_sensor_port.h`:
  - `typedef struct { uint8_t channel; const char *query; size_t query_len; } uart_query_t;`
  - `typedef struct { uint8_t channel; uint8_t *response; size_t response_len; esp_err_t status; } uart_response_t;`
  - `typedef esp_err_t (*uart_sensor_query_fn)(const uart_query_t *query, uart_response_t *response, uint32_t timeout_ms);`

### Steps

7.1. Create `components/uart_sensors/` — manages 4 UART sensor channels:
  - Configure 3 UART controllers + GPIO remap for 4th channel
  - Each query runs as a FreeRTOS task pinned to Core 1 via `xTaskCreatePinnedToCore(..., 1)`
  - Non-blocking pattern: send query → `uart_read_bytes()` with timeout → yield via `vTaskDelay()` between poll attempts
  - Response buffer managed per-channel (heap-allocated, freed after processing)
  - Mutex per UART controller prevents concurrent access
  - `REQUIRES domain driver` + `PRIV_REQUIRES i2c_bus` (if using Option B)

7.2. UART query task architecture (non-blocking):
  ```
  uart_sensor_task (pinned to Core 1):
    for each scheduled query:
      if channel needs GPIO remap: uart_set_pin(uart_num, new_tx, new_rx, ...)
      uart_write_bytes(uart_num, query, len)
      while (elapsed < timeout_ms):
        bytes = uart_read_bytes(uart_num, buf, len, pdMS_TO_TICKS(100))
        if (response_complete(buf)) break
        // Core 1 yields here — Core 0 (WiFi/MQTT) unaffected
      store result → notify data management task on Core 0
  ```

7.3. Add UART sensor commands to `device_commands`:
  - `cmd_uart_query(uint8_t channel, const char *query, uint32_t timeout_ms)` — send query, wait for response
  - `cmd_uart_ping(uint8_t channel)` — send a short probe (e.g., `"PING\n"`), expect response within ~2s. Returns connected/disconnected. Used by Lua before attempting a full measurement query.
  - `cmd_uart_status()` — show all 4 channels: connected/disconnected/busy/disabled (from `channel_config`)

7.3a. **Ping/health-check design**:
  - Each channel defines a lightweight probe: send a known short command, expect any response within a short timeout (e.g., 2000ms)
  - `uart_sensor_ping()` internally: flush RX buffer → `uart_write_bytes(ping_cmd)` → `uart_read_bytes(buf, 64, pdMS_TO_TICKS(2000))`
  - If bytes received > 0 → `SENSOR_CONNECTED`; if timeout → `SENSOR_DISCONNECTED`
  - Result cached per-channel with a TTL (e.g., 10s) so repeated checks don't hammer the bus
  - Lua usage: `if device.uart_ping(1) then device.uart_query(1, "MEASURE") end`
  - CLI usage: `ping_uart 1` → "AMBIT1: connected (MultispeQ v2.0)" or "AMBIT1: disconnected"
  - Status is also checked automatically before `cmd_uart_query()` — if ping fails, return error immediately instead of blocking for 2 min

7.4. Wire into `app_main.c`:
  - Init UART controllers with pin assignments
  - Register `uart_sensor_query_fn` port adapter
  - Pin sensor tasks to Core 1, networking tasks to Core 0

7.5. GPIO pin assignment for 4 UART channels:

  | Channel | RX Pin | TX Pin | HW UART |
  |---|---|---|---|
  | AMBIT1 | GPIO 3 | GPIO 46 | UART1 |
  | AMBIT2 | GPIO 17 | GPIO 18 | UART2 |
  | AMBIT3 | GPIO 47 | GPIO 48 | UART0 (remapped for query, then back) |
  | AMBIT4 | GPIO 40 | GPIO 41 | UART0 (remapped — time-shared with AMBIT3) |

  - AMBIT1 and AMBIT2 get dedicated HW UARTs (UART1, UART2) — can run concurrently
  - AMBIT3 and AMBIT4 share UART0 via GPIO matrix remap between queries
  - Console uses USB Serial JTAG (not UART0), so UART0 is free
  - AMBIT3/4 pins (47/48, 40/41) are NOT UART0 default pins (43/44) — GPIO matrix remap via `uart_set_pin()` maps UART0 to these custom pins. No conflict with default UART0 GPIOs.
  - No pin conflicts with: I2C (38, 39), SDMMC (9-14), USB JTAG (19, 20), status LED (45)

**Verification:** Send a test query to a sensor, verify response received. Monitor Core 0 WiFi connectivity during a 2-min query on Core 1 — no disconnections.

---

## Revised Bounded Context Map

| Bounded Context | Domain Concepts | Components |
|---|---|---|
| **Sensing** | Measurement, SensorReading, SensorId | `bme280/`, `pcf2131tfy_rtc/`, `uart_sensors/` (new) |
| **Connectivity** | Connection, NetworkCredentials, Provisioning | `wifi_manager/` (incl. BLE provisioning), `certs/` |
| **Messaging** | Message, Topic, Subscription | `mqtt_client/` (new) |
| **Persistence** | MeasurementLog, MeasurementRecord | `sd_card/`, `sqlite3` |
| **Automation** | Script, MeasurementJob | `lua_runner/`, `lua/` |
| **Device Management** | DeviceStatus, Command | `CLI/`, `ambyte_status/` |

---

## Relevant Files

- [main/app_main.c](main/app_main.c) — Refactor into composition root (Phase 3.4)
- [main/CMakeLists.txt](main/CMakeLists.txt) — Adjust REQUIRES after refactoring
- [components/CLI/CMakeLists.txt](components/CLI/CMakeLists.txt) — Remove direct sensor PRIV_REQUIRES (Phase 3.2)
- [components/CLI/CLI.c](components/CLI/CLI.c) — Refactor to dispatch-table REPL using Lua C API (Phase 3.3)
- [components/lua_runner/CMakeLists.txt](components/lua_runner/CMakeLists.txt) — Remove direct component REQUIRES (Phase 3.3)
- [components/lua_runner/lua_runner.c](components/lua_runner/lua_runner.c) — Refactor bindings (Phase 3.3)
- [components/bme280/CMakeLists.txt](components/bme280/CMakeLists.txt) — Remove I2C_device + Arduino deps (Phase 0B), add `REQUIRES domain` (Phase 2.1)
- [components/bme280/bme280_arduino_shim.cpp](components/bme280/bme280_arduino_shim.cpp) — Refactor to use i2c_bus directly (Phase 0B)
- [components/pcf2131tfy_rtc/CMakeLists.txt](components/pcf2131tfy_rtc/CMakeLists.txt) — Add `REQUIRES domain` (Phase 2.2)
- [components/pcf2131tfy_rtc/PCF2131_I2C.cpp](components/pcf2131tfy_rtc/PCF2131_I2C.cpp) — Merge hierarchy into this file (Phase 0C)
- [components/I2C_device/](components/I2C_device/) — DELETE entire component (Phase 0B)
- [components/wifi_manager/](components/wifi_manager/) — Extend with BLE provisioning (Phase 6), add `PRIV_REQUIRES wifi_provisioning protocomm bt`
- [components/sd_card/sd_card.c](components/sd_card/sd_card.c) — Add SQLite persistence, dedicated mutex (Phase 2.3)

**New files to create:**
- `partitions.csv` — Custom partition table for 16MB flash (Phase 0A.8)
- `components/domain/CMakeLists.txt` — Header-only component, `REQUIRES esp_common`
- `components/domain/include/sensing_port.h` — Sensing contracts
- `components/domain/include/persistence_port.h` — Persistence contracts (SQLite repository pattern, fixed-size char[32] fields)
- `components/domain/include/device_status_port.h` — Status contracts
- `components/domain/include/messaging_port.h` — MQTT publish/subscribe contracts (Phase 5)
- `components/domain/include/uart_sensor_port.h` — UART query/response contracts (Phase 7)
- `components/domain/include/power_port.h` — Battery/power ADC contracts (Phase 8)
- `components/sqlite3/` — Git submodule of `siara-cc/esp32-idf-sqlite3` (Phase 2.3)
- `components/sd_card/sd_card_sqlite.c` — SQLite-based persistence port implementation with dedicated `s_sqlite_mutex` (Phase 2.3)
- `components/device_commands/CMakeLists.txt` — Shared command layer, REQUIRES `domain` only
- `components/device_commands/device_commands.c` — All `cmd_*` implementations + `device_commands_config_t` struct aggregating all ports
- `components/device_commands/include/device_commands.h` — `cmd_result_t` + `device_commands_config_t` + all command signatures
- `components/mqtt_client/` — ESP-MQTT wrapper with TLS (Phase 5)
- `components/uart_sensors/` — 4-channel UART sensor manager, Core 1 tasks (Phase 7)
- `components/mp2731/` — MP2731 battery charger I2C driver (Phase 8)

---

## Verification

1. After each phase, run `pio run` — firmware must build successfully
2. After Phase 2: flash and confirm BME280, RTC, SD card, LED all still work
3. After Phase 3: test CLI commands (`status`, `read_sensors`, `i2c_scan`) still produce correct output
4. After Phase 3: test Lua scripts still execute and produce same behavior
5. After Phase 4: attempt to add `REQUIRES bme280` to `domain/CMakeLists.txt` — should violate the layering rule (caught by code review / CI)
6. Inspect `components/domain/CMakeLists.txt` has only `REQUIRES esp_common` throughout all phases (no infra deps)

---

## Decisions

- **No runtime abstraction overhead**: Ports use function pointers (single indirection), not vtables or heap allocation. This is acceptable for an embedded system.
- **ESP-IDF component model preserved**: DDD layers are enforced through `REQUIRES`/`PRIV_REQUIRES`, not filesystem reorganization into `domain/`, `infrastructure/` top-level folders. Components stay in `components/`.
- **Incremental migration**: Each phase is independently shippable. Phase 1-2 are additive (no breaking changes). Phase 3 is the first refactor.
- **Arduino dependency eliminated in Phase 0B**: Refactoring BME280 to use native ESP-IDF I2C removes `framework-arduinoespressif32` and the `I2C_device` component entirely.
- **SQLite for persistence**: Using `siara-cc/esp32-idf-sqlite3` (pure ESP-IDF, Apache-2.0). DB file lives on SD card at `/sdcard/measurements.db`. Flash budget is comfortable with 16MB.
- **All SD I/O through SQLite**: Text-file API (`sdcard_write_line`, etc.) will be removed. All persistence goes through the SQLite repository.
- **SD card text-file API removed**: Old functions deprecated and deleted during Phase 2.3.
- **Arduino framework fully removed**: After BME280 + RTC are both native ESP-IDF (Phase 0B/0C), `arduino` is removed from `platformio.ini` frameworks.
- **Board config updated**: Switch `platformio.ini` from `adafruit_feather_esp32s3` (2MB) to `esp32-s3-devkitm-1` (16MB) to match actual ESP32-S3-WROOM-1 hardware. Custom `partitions.csv` with 3MB factory app + NVS.
- **Lua standard libs kept**: All Lua standard library modules remain (user may use them in future scripts).
- **Device-prefixed measureID**: `measureID` includes a device ID prefix for future multi-device support.
- **Misc files moved to `docs/`**: `AI prompts.txt`, `MQTT TLS test client.py`, `overall_architecture.txt` moved to `docs/` folder.
- **Excluded**: Event-driven pub/sub between bounded contexts, NVS repository pattern. Can be layered on later.
- **MQTT v5**: Already enabled in sdkconfig. Uses ESP-MQTT with mutual TLS to AWS IoT Core.
- **BLE provisioning inside wifi_manager**: No separate `ble_provisioning/` component. `wifi_manager` gains `wifi_manager_is_provisioned()` and `wifi_manager_start_provisioning()` — BLE code is private (`PRIV_REQUIRES bt`). Consumers never see BLE types.
- **Dual-core split**: Core 0 = WiFi/MQTT/data. Core 1 = Lua/CLI/UART sensors. Prevents 2-min UART queries from blocking networking.
- **4 UART channels on 3 HW UARTs**: GPIO matrix remap for 4th channel (sequential queries allow time-sharing). No extra hardware needed.
- **domain/ REQUIRES esp_common only**: Needed for `esp_err_t` type. This is purely type definitions — no infrastructure dependency. Domain stays header-only (no .c files).
- **measurement_record_t uses fixed-size char[32] arrays**: No heap allocation, no ownership ambiguity. SQLite callbacks `strncpy` directly into fields. ~88 bytes per struct, queries capped at 64 records.
- **CLI uses Lua C API, not string interpolation**: CLI dispatch table calls `lua_getglobal()` + `lua_pushinteger()` + `lua_pcall()` — zero injection risk. No `luaL_dostring()` for user input.
- **device_commands_init() takes a config struct**: `device_commands_config_t` aggregates all port function pointers. New ports added as struct fields without changing function signature. NULL fields mean "not available" → `ESP_ERR_NOT_SUPPORTED`.
- **SQLite access serialized by dedicated mutex**: `s_sqlite_mutex` in `sd_card_sqlite.c`, separate from SD mount mutex. Prevents Core 0 / Core 1 concurrent DB corruption.
- **sensing_ctx_t folded into device_commands_config_t**: No `sensing_service.h` in domain/. Port aggregation is application-layer concern, lives in `device_commands/`.
- **SD card init added to app_main**: `sdcard_init_default()` + `sdcard_mount()` called before any SQLite access. `sd_card` added to `main/CMakeLists.txt` REQUIRES.
- **MP2731 register values documented**: Init comments specify target battery spec. Values confirmed correct (board config change is cosmetic, same physical circuit).

---

## Files to Delete (Phase 0)

- `src/` — empty directory
- `dom_ludo_prototype_ambyte_thing_certs/` — leftover cert archive
- `components/I2C_device/` — entire component (after BME280 refactored in 0B)
- `components/pcf2131tfy_rtc/include/RTC_NXP.h` — merged into PCF2131_I2C (0C)
- `components/pcf2131tfy_rtc/PCF2131_base.cpp` — merged (0C)
- `components/pcf2131tfy_rtc/RTC_NXP.cpp` — merged (0C)
- `main/app_main.c` lines ~31–85 — dead `sensor_report_task()` function
- `components/bme280/bme280_adafruit_private.hpp` — Adafruit sensor API ifdef block (0D)

---

## Phase 8: MP2731 Battery Charger I2C Driver

Port user's Arduino-based MP2731 driver to pure C ESP-IDF component using the shared `i2c_bus` kernel, following the PCF2131 RTC driver pattern.

### Phase 8A: Create `components/mp2731/` (no dependencies on Phases 1–7)

8A.1. Create `components/mp2731/include/mp2731.h` — public C API:
  - `#define MP2731_ADDR 0x4B`, register defines REG00–REG17
  - Structs: `mp2731_status_t`, `mp2731_fault_t`, `mp2731_adc_t`, `mp2731_data_t`
  - Functions: `mp2731_init()`, `mp2731_is_ready()`, `mp2731_read_status()`, `mp2731_read_fault()`, `mp2731_run_adc()`, `mp2731_read_adc()`, `mp2731_read_all()`, `mp2731_get_sensors_averaged()`, `mp2731_set_vin_limit()`, `mp2731_set_charge_current()`, `mp2731_read_register()`, `mp2731_dump_registers()` — all return `esp_err_t`

8A.2. Create `components/mp2731/mp2731.c` — implementation:
  - Static I2C helpers: `mp2731_i2c_read_reg()` / `mp2731_i2c_write_reg()` using i2c_bus_lock → cmd link → i2c_master_cmd_begin → unlock (same as PCF2131_I2C.cpp `_reg_r`/`_reg_w`)
  - Port Arduino functions: Wire → i2c_bus, delay → vTaskDelay, Serial.printf → ESP_LOGI
  - Fix bugs: `mp2731_adcInputCurrent()` → REG13 (not REG11); `mp2731_ICC()` → write `setting` to REG05 (not `data`)
  - Static `s_mp2731_ready` flag
  - Add comments in `mp2731_init()` documenting target battery spec: "Configured for single-cell Li-Ion 4.2V, ICC=640mA, IIN_LIM=current setting, VIN_MIN=4.3V" — same physical battery/charger circuit, board config change is cosmetic

8A.3. Create `components/mp2731/CMakeLists.txt`: `PRIV_REQUIRES driver i2c_bus`

8A.4. Wire `mp2731_init()` in `main/app_main.c` after `i2c_bus_init()`

### Phase 8B: Domain Integration (depends on Phases 1, 3)

8B.1. Add `power_port.h` to `components/domain/include/` — `power_adc_t`, `power_read_fn`
8B.2. Add `cmd_read_battery()`, `cmd_set_charge_current()`, `cmd_set_vin_limit()` to `device_commands`
8B.3. Add Lua bindings: `device.battery()`, `device.set_charge_current(ma)`, `device.set_vin_limit(v)`

### Verification
- `pio run` succeeds
- `mp2731_dump_registers()` prints 24 register values
- `mp2731_read_all()` returns plausible ADC values
- Interleaved I2C reads (MP2731 + BME280 + RTC) cause no bus corruption

### Relevant files
- [components/hal/i2c_bus/include/i2c_bus.h](components/hal/i2c_bus/include/i2c_bus.h) — shared kernel API
- [components/pcf2131tfy_rtc/PCF2131_I2C.cpp](components/pcf2131tfy_rtc/PCF2131_I2C.cpp) — I2C pattern template (`_reg_r`/`_reg_w`)
- New: `components/mp2731/mp2731.c`, `components/mp2731/include/mp2731.h`, `components/mp2731/CMakeLists.txt`
