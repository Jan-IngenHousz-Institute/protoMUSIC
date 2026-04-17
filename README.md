# Ambyte IoT Ludo

ESP32-S3 firmware for the Ambyte IoT device: BLE-provisioned Wi-Fi + MQTT-over-TLS telemetry to AWS IoT Core, with local buffering (LittleFS + SQLite) and Lua-scriptable handlers.

- **Hardware target:** ESP32-S3 DevKitM-1 (16 MB flash), secondary target for Adafruit Feather ESP32-S3.
- **Framework:** ESP-IDF 5.5 via PlatformIO.
- **Transport:** MQTTS → AWS IoT Core, mutual TLS (device-cert + private key provisioned over BLE).
- **Storage:** LittleFS for logs, SQLite for structured persistence, NVS for credentials.

---

## Quick start

```sh
# 1. Clone with submodules
git clone <repo-url>
cd ambyte-iot-ludo
git submodule update --init --recursive

# 2. Drop in the gitignored certs component implementation (see §4)
cp /path/to/your/certs.c components/certs/certs.c

# 3. Build & flash
pio run -e esp32-s3-devkitm-1 -t upload
pio device monitor -b 115200

# 4. Provision (first-time only)
cp .env.example .env && $EDITOR .env
uv run docs/ambyte_prov.py
```

Everything below is the long-form version of those four steps plus troubleshooting.

---

## 1. Prerequisites

- [PlatformIO Core](https://platformio.org/install) (VS Code extension or `pio` CLI)
- [`uv`](https://docs.astral.sh/uv/) for the Python side (provisioning script + lockfile)
- Python 3.13 (auto-managed by `uv`); the PlatformIO/ESP-IDF side tolerates 3.9–3.13
- Git with submodule support

## 2. Clone with submodules

```sh
git clone <repo-url>
cd ambyte-iot-ludo
git submodule update --init --recursive
```

If `components/sqlite3` fails with `not our ref`, the recorded commit was force-pushed away upstream — see [§5](#5-sqlite3-submodule-compat-patches).

## 3. Install ESP-IDF Python requirements

PlatformIO creates an ESP-IDF venv at `~/.platformio/penv/.espidf-5.5.3/` but on some systems it's left empty. If builds fail with `ModuleNotFoundError: No module named 'idf_component_manager'`:

```sh
~/.platformio/penv/.espidf-5.5.3/bin/python -m pip install \
  -r ~/.platformio/packages/framework-espidf/tools/requirements/requirements.core.txt
```

## 4. Provide `components/certs/certs.c`

This file is `.gitignore`'d (may embed device-specific code or credentials). It implements the API declared in [components/certs/include/certs.h](components/certs/include/certs.h):

- `certs_init()` — open NVS namespace `"certs"` and load PEM buffers
- `certs_are_provisioned()` — true iff CA, device cert, device key all present
- `certs_get_*()` — non-NULL PEM string getters
- `certs_set_*()` — persist PEM to NVS and update the in-memory buffer

Obtain from a teammate or restore from backup. Without it, the build fails at CMake-configure time.

## 5. sqlite3 submodule compat patches

Upstream `siara-cc/esp32-idf-sqlite3` master is not ESP-IDF 5.x-compatible. Two local patches are committed in-tree; re-apply if the submodule gets reset:

**[components/sqlite3/esp32.c](components/sqlite3/esp32.c)** — swap the obsolete header:

```diff
-#include <esp_spi_flash.h>
 #include <esp_system.h>
+#include <esp_random.h>
+#include <spi_flash_mmap.h>
```

**[components/sqlite3/CMakeLists.txt](components/sqlite3/CMakeLists.txt)** — add the deps needed by the header swap:

```diff
-                       PRIV_REQUIRES console spiffs)
+                       PRIV_REQUIRES console spiffs spi_flash esp_system)
```

The submodule is pinned to upstream master `6919392` (the originally recorded commit was force-pushed away upstream).

## 6. Build

```sh
pio run -e esp32-s3-devkitm-1
```

## 7. Flash + monitor

Connect the ESP32-S3 via USB. PlatformIO auto-detects the port; override with `--upload-port` if needed.

```sh
pio run -e esp32-s3-devkitm-1 -t upload
pio device monitor -b 115200
```

### Factory reset

To re-provision a device that's already been through BLE provisioning (Wi-Fi, MQTT, and certs stored in NVS), erase flash first:

```sh
pio run -e esp32-s3-devkitm-1 -t erase_flash -t upload
```

Without this, re-running the provisioning script will fail at "Wi-Fi apply" because the device is already connected. Alternatively, pass `--skip-wifi` to the provisioning script to update only MQTT config and certs.

## 8. BLE provisioning

After first flash the device waits for BLE provisioning. The helper script [docs/ambyte_prov.py](docs/ambyte_prov.py) sends Wi-Fi creds, MQTT config, and TLS certs in one 120-second BLE session.

The repo root is a `uv` project ([pyproject.toml](pyproject.toml), [uv.lock](uv.lock)). All Python deps (`bleak`, `protobuf`, `cryptography`) are declared there:

```sh
uv run docs/ambyte_prov.py
```

`uv` auto-creates `.venv/` from the lockfile on first run. No manual `pip install` needed.

### Config resolution order

Each field resolves top-down (first wins):

1. CLI flag — `--ssid foo`
2. Shell env var — `AMBYTE_SSID=foo`
3. `.env` file at the repo root — auto-loaded, gitignored
4. `~/.ambyte_prov.json` — auto-written after the first successful run, chmod 600
5. Interactive prompt

Bootstrap your `.env` once:

```sh
cp .env.example .env
$EDITOR .env
chmod 600 .env
```

After that, `uv run docs/ambyte_prov.py` alone will prompt for anything missing. The Wi-Fi password is **never** cached to disk — it's read only from `$AMBYTE_PASSWORD`, `--password`, or a one-shot `getpass` prompt.

### Device certs

Put your AWS IoT Core PEM files under [device_certs/](device_certs/) (gitignored). `.env.example` assumes that location. Paths accept both `/` and `\` separators, so PowerShell-pasted paths work on Linux.

### Full CLI example (first run, everything explicit)

```sh
uv run docs/ambyte_prov.py \
  --ssid       "YOUR_WIFI" \
  --password   "YOUR_WIFI_PASS" \
  --mqtt_uri   "mqtts://XXXX.iot.REGION.amazonaws.com:8883" \
  --client_id  "thing-001" \
  --topic_root "ambyte/prod" \
  --device_id  "thing-001" \
  --device_name     "AmbyteOnAir" \
  --device_version  "1" \
  --device_firmware "1" \
  --firmware_version "1" \
  --protocol_id     "3517" \
  --ca_cert   device_certs/AmazonRootCA1.pem \
  --dev_cert  device_certs/<thing>-certificate.pem.crt \
  --dev_key   device_certs/<thing>-private.pem.key
```

### Platform notes

- **Linux:** if `bleak` permission-denies, grant cap_net_raw to the venv python: `sudo setcap cap_net_raw,cap_net_admin+eip $(readlink -f .venv/bin/python)`.
- **Windows PowerShell:** run as Administrator for BLE access. Use backticks (`` ` ``) for line continuation instead of backslashes.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `No module named 'idf_component_manager'` | §3 |
| `Failed to resolve component 'sqlite3'` | §2 (submodules) + §5 |
| `components/certs/certs.c: No such file` | §4 |
| `littlefs/lfs.h: No such file` | `git submodule update --init --recursive` |
| `KeyError: 'IDF_PATH'` from `esp_prov` | Pull latest `docs/ambyte_prov.py` (self-sets `IDF_PATH`) |
| `ModuleNotFoundError: No module named 'google'` from `esp_prov` | Run via `uv run docs/ambyte_prov.py` (not bare `python`) |
| `RuntimeError: Wi-Fi apply failed` | Device already provisioned. Either `pio run -t erase_flash -t upload` or re-run with `--skip-wifi` |

## Repository layout

```
components/          # ESP-IDF components
  certs/             # (private) TLS cert store, certs.c is gitignored
  hal/               # board HAL (i2c_bus, ...)
  littlefs/          # submodule — flash filesystem
  sqlite3/           # submodule — local DB (patched for IDF 5.x)
  ambyte_status/     # LED/status indicators
  bme280/            # temp/humidity/pressure sensor
  device_commands/   # remote command dispatcher
  device_config/     # NVS-backed device config
  domain/            # domain models
  CLI/               # serial CLI
  lua/ lua_runner/   # embedded Lua
  mqtt_client/       # AWS IoT MQTT client
  pcf2131tfy_rtc/    # RTC driver
  persistence/       # DB-backed persistence layer
  sd_card/           # SD card driver
  uart_sensors/      # UART-attached sensors
  wifi_manager/      # Wi-Fi provisioning + reconnect

main/                # app entry point
docs/                # provisioning script + protocol notes
device_certs/        # (gitignored) AWS IoT PEM files
.env / .env.example  # provisioning defaults
pyproject.toml       # Python deps (uv)
platformio.ini       # PlatformIO env config
```
