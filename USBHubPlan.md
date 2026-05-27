# Migrate USB-C debug to UART; replace AMBIT pipeline with USB-host sensor channels

## Context

The ESP32-S3 has a **single** native USB peripheral that is currently configured as the Serial/JTAG console (see [sdkconfig.defaults](sdkconfig.defaults), `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). That same peripheral is the only path to USB-host mode, so it can either be the debug serial port **or** drive USB-device sensors, never both at once. To add USB-connected sensors (similar role to today's AMBIT UART sensors) while keeping a wired debug link, we move the console onto a UART and reach it via an external UART-to-USB adapter on the FFC, freeing the native USB-C to act as USB host through a powered hub.

User decisions captured before planning:
- **AMBIT pipeline is going away entirely.** All four UART channels and every `cmd_ambit_*` / `device.ambit_*` / `device.uart_*` function are removed. This frees all three UART peripherals and ~2000 lines of C.
- **Console moves to UART0** (the IDF/console convention). UART1 and UART2 remain unused but available on the FFC for future use.
- **USB sensors enumerate as CDC-ACM**, so the firmware can use ESP-IDF's `espressif/usb_host_cdc_acm` managed component on top of the USB Host Library; no custom class driver needed.

## Hardware setup (no PCB respin needed for prototype)

- **Debug path**: PC ↔ USB-C ↔ external UART-to-USB adapter (FTDI / CP2102 / CH340) ↔ UART0 TX/RX/GND on the FFC. `pio device monitor` opens the adapter's COM port.
- **Sensor path**: ESP32 native USB-C ↔ USB-C-to-USB-A OTG adapter ↔ **self-powered** USB hub ↔ sensors. The hub's own wall supply sources VBUS; the ESP32 cannot.
- **Confirm before building**: which GPIOs on the FFC are wired to ESP32's UART0 TX/RX. ESP-IDF's default UART0 pins on ESP32-S3 are GPIO 43/44, but [components/uart_sensors/uart_sensors.c:553](components/uart_sensors/uart_sensors.c#L553) currently drives UART0 on GPIO 47/48 (and 40/41 in the shared remap). If the FFC carries 47/48, set `CONFIG_ESP_CONSOLE_UART_TX_GPIO=48` / `..._RX_GPIO=47` in `sdkconfig.defaults`.

## Step 1: Console migration

Replace the USB-Serial/JTAG console with UART0. In [sdkconfig.defaults](sdkconfig.defaults):

```
# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_UART_NUM=0
# Optional, only if your FFC's UART0 pins aren't the IDF defaults:
# CONFIG_ESP_CONSOLE_UART_TX_GPIO=48
# CONFIG_ESP_CONSOLE_UART_RX_GPIO=47
```

`platformio.ini` `monitor_speed = 115200` stays as-is. After flashing, open `pio device monitor` against the **adapter's** COM port, not the ESP32's.

## Step 2: Delete the AMBIT pipeline

Mechanical removal — every symbol containing `ambit` or `uart_sensor` goes:

- **Delete the component**: `components/uart_sensors/` (whole directory).
- **Delete domain port**: `components/domain/include/uart_sensor_port.h`.
- **Delete the protocol header**: `components/device_commands/include/ambit_protocol.h`.
- **`device_commands`** ([device_commands.c](components/device_commands/device_commands.c) + its header): remove every `cmd_ambit_*`, `cmd_uart_*`, the `ambit_ack_only` / `ambit_action` helpers, and the matching declarations in [device_commands.h](components/device_commands/include/device_commands.h). Drop the `uart_*` fn fields from `device_commands_config_t`.
- **`lua_runner`** ([lua_runner.c](components/lua_runner/lua_runner.c)): remove every `l_device_ambit_*` and `l_device_uart_*` function and their entries in the `device_api[]` table.
- **`app_main`** ([app_main.c](main/app_main.c)): remove `#include "uart_sensors.h"`, the `uart_sensors_init` block plus auto-ping loop, the `uart_available` flag, and the `.uart_query/.uart_ping/.uart_status` initialisers in `cmd_cfg`. Remove `uart_sensors` from `main/CMakeLists.txt` REQUIRES.

Leaves: `cmd_store_measurement`, `cmd_record_env`, the MQTT-publish path (`cmd_mqtt_publish_next_batch`), [sync_runner](components/sync_runner/sync_runner.c), the BME280 + RTC ports, and the new SQLite-v2 schema. The whole DB → MQTT pipeline (just rewritten — see the recent `measurements_v2` migration) keeps working unchanged.

## Step 3: New component `usb_sensors`

Parallel structure to the deleted `uart_sensors` — same public-port shape so it slots into `device_commands_config_t` without touching the rest of the codebase.

- **Files**:
  - `components/usb_sensors/include/usb_sensors.h` — public API
  - `components/usb_sensors/usb_sensors.c` — implementation
  - `components/usb_sensors/CMakeLists.txt` — `REQUIRES driver freertos`, `PRIV_REQUIRES usb`
  - `components/usb_sensors/idf_component.yml` — pulls `espressif/usb_host_cdc_acm: "^2.0.0"`
  - `components/domain/include/usb_sensor_port.h` — fn typedefs (`usb_sensor_query_fn`, `usb_sensor_ping_fn`, `usb_sensor_status_fn`)
- **Init responsibilities**:
  1. `usb_host_install()` with default config.
  2. Spawn a low-priority task that runs `usb_host_lib_handle_events` in a loop — required for the host library to make progress.
  3. `cdc_acm_host_install()`.
  4. Register a `usb_host_device_connection_cb` that opens any CDC-ACM device that appears with `cdc_acm_host_open()`, populates a per-port slot in a channel table, and tags the channel with the hub port number from `usb_host_device_info_t`.
- **Identification by hub port**: stable across reboots, doesn't depend on the manufacturer setting `iSerialNumber`. Channel index 0..N-1 maps to a fixed port-number list — define `USB_SENSOR_NUM_CHANNELS` once the hub size is known.
- **Public API** (mirrors the old uart counterparts):
  - `esp_err_t usb_sensors_init(void)`
  - `usb_sensor_query_fn usb_sensors_get_query_fn(void)`
  - `usb_sensor_ping_fn  usb_sensors_get_ping_fn(void)`
  - `usb_sensor_status_fn usb_sensors_get_status_fn(void)`
- **Query implementation**: `cdc_acm_host_data_tx_blocking(slot.cdc_dev, cmd, len, timeout)` then `cdc_acm_host_data_rx_blocking(slot.cdc_dev, buf, max, &got, timeout)`. Returns `ESP_ERR_NOT_FOUND` if the channel slot is empty (no device on that port).

## Step 4: Wire into `device_commands_config_t`

Add three new fields (`usb_query`, `usb_ping`, `usb_status`) to [device_commands.h](components/device_commands/include/device_commands.h), populated from `usb_sensors_get_*_fn()` in [app_main.c](main/app_main.c) where the AMBIT counterparts used to live. Optionally expose `device.usb_ping(ch)` / `device.usb_query(ch, cmd, expect_raw, timeout)` / `device.usb_status()` Lua bindings — same shape as the deleted `device.uart_*` bindings.

## Step 5: Polling loop and DB ingest

A new task in `usb_sensors.c` (or `app_main.c`, both fine) wakes every 10 s and walks the channel table. For each connected channel it sends the sensor's read command, parses the response, and writes rows via `cmd_store_measurement()` — same path Lua's `device.record_env` uses today, same PENDING-then-publish flow.

**Mapping a USB-sensor reading into the new schema** (the schema just landed in [persistence_port.h](components/domain/include/persistence_port.h) — see `measurements_v2` and the new `measurement_record_t`):

- One USB poll = one `measure_id` (allocated via `cmd_next_measure_id`). All quantities returned by that poll share the id.
- `device` = the module type string ("co2dot", "miniPAR", …) — non-NULL because these are external modules.
- `sensor` = the chip inside the module (e.g. "AS7343" for a miniPAR).
- `quantity` = the data-point label per row ("par", "leaf_temp", …).
- `value_real` for floats; `value_text` for status strings.
- `start_ticks_ms` / `end_ticks_ms` bracket the read.

**No changes to `sync_runner` needed** — the recent DB rewrite already dropped per-type rotation, so the background task picks up rows of whatever `quantity` exists. Adding a new sensor type is now zero lines in [sync_runner.c](components/sync_runner/sync_runner.c).

## Open items (need user input before implementation can start)

1. **FFC pin map for UART0**: confirm whether the FFC carries the IDF default UART0 pins or the project's existing 47/48 / 40/41 — drives the optional `CONFIG_ESP_CONSOLE_UART_*_GPIO` lines in Step 1.
2. **Hub port count**: sets `USB_SENSOR_NUM_CHANNELS`.
3. **Per-sensor wire protocol**: what command bytes the sensor expects, and the shape of the response (line-terminated ASCII? length-prefixed binary? fixed struct?). Same question that existed for AMBIT — without it the polling-loop body in Step 5 is a stub.
4. **`device` / `sensor` / `quantity` strings each sensor produces**: keys the receiver will see on AWS IoT, so they need to be agreed with whoever consumes the data.

## Verification

1. **Clean build** after Step 1 + Step 2 — `pio run -e esp32-s3-devkitm-1` succeeds with no unresolved symbols and the firmware shrinks noticeably (estimate ~30 KB) since AMBIT code is gone.
2. **Console on UART**: flash, connect the UART-to-USB adapter, open `pio device monitor` on the adapter's COM port. Expect the normal boot log; the native USB-C is silent.
3. **USB host install at boot** (Step 3): log line "USB host installed" with no panic; native USB-C with nothing connected is fine.
4. **Enumeration**: plug a powered hub + one CDC-ACM sensor into the native USB-C. Expect a log entry showing VID/PID/iSerial/`dev_addr` and the channel slot it landed on.
5. **Polling produces records**: every ~10 s a log line "queried channel N: K bytes" and a corresponding PENDING row in SQLite — visible via `SELECT * FROM measurements_v2 WHERE sync_state=0 ORDER BY start_ticks DESC LIMIT 10;`.
6. **End-to-end MQTT**: `sync_runner` picks up the new rows, `cmd_mqtt_publish_next_batch` emits them in the `sample: [...]` array with the correct `device`/`sensor`/`quantity`/`value`, PUBACK rc=0, AWS IoT receives them. No regression on the BME280 → `record_env` → publish path.
7. **Hot-plug**: unplug a sensor mid-run, replug, confirm the channel slot recovers and polling resumes.

## Critical files

- **New**: `components/usb_sensors/{usb_sensors.c,include/usb_sensors.h,CMakeLists.txt,idf_component.yml}`, `components/domain/include/usb_sensor_port.h`.
- **Modified**: [sdkconfig.defaults](sdkconfig.defaults), [main/app_main.c](main/app_main.c), [main/CMakeLists.txt](main/CMakeLists.txt), [components/device_commands/device_commands.c](components/device_commands/device_commands.c), [components/device_commands/include/device_commands.h](components/device_commands/include/device_commands.h), [components/lua_runner/lua_runner.c](components/lua_runner/lua_runner.c).
- **Deleted**: `components/uart_sensors/` (whole directory), `components/domain/include/uart_sensor_port.h`, `components/device_commands/include/ambit_protocol.h`.
- **Unchanged but relied upon**: `cmd_store_measurement` + `cmd_record_env` in [device_commands.c](components/device_commands/device_commands.c), the SQLite v2 persistence layer just landed, `sync_runner` (already batch-based), the existing `on_publish_ack` flow with its inflight-batch mutex.
