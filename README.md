# Ambyte IoT Ludo

ESP32-S3 firmware for the Ambyte IoT device: MQTT-over-TLS telemetry to AWS IoT Core, with local buffering (LittleFS + SQLite) and Lua-scriptable handlers. Provisioning data (Wi-Fi, MQTT, TLS certs) is generated on the host from `.env` + `device_certs/<bundle>/` and flashed into the NVS partition next to the firmware — no BLE companion app, no runtime provisioning round-trip.

- **Hardware target:** ESP32-S3 DevKitM-1 (16 MB flash), secondary target for Adafruit Feather ESP32-S3.
- **Framework:** ESP-IDF 5.5 via PlatformIO.
- **Transport:** MQTTS → AWS IoT Core, mutual TLS (device-cert + private key delivered via NVS pre-pop).
- **Storage:** LittleFS for logs, SQLite for structured persistence, NVS for credentials.

---

## Quick start

```sh
# 1. Clone + init the littlefs submodule (sqlite3 and certs.c are vendored in-tree)
git clone <repo-url>
cd ambyte-iot-ludo
git submodule update --init --recursive   # pulls components/littlefs and its nested lfs

# 2. Set up provisioning data (first time only)
cp .env.example .env && $EDITOR .env       # Wi-Fi creds, MQTT URI, topic root, ...
# Drop your AWS IoT thing's PEM bundle into device_certs/<thing-name>/

# 3. Build & flash (NVS pre-pop happens automatically via tools/extra_script.py)
pio run -e esp32-s3-devkitm-1 -t upload
pio device monitor -b 115200
```

Everything below is the long-form version plus troubleshooting.

---

## 1. Prerequisites

- [PlatformIO Core](https://platformio.org/install) (VS Code extension or `pio` CLI)
- [`uv`](https://docs.astral.sh/uv/) for the Python side (provisioning script + lockfile)
- Python 3.13 (auto-managed by `uv`); the PlatformIO/ESP-IDF side tolerates 3.9-3.13
- Git with submodule support

## 2. Clone with submodules

```sh
git clone <repo-url>
cd ambyte-iot-ludo
git submodule update --init --recursive
```

Only [components/littlefs](components/littlefs) is a submodule today. Its nested `littlefs/src/littlefs` submodule is pulled by `--recursive`. `components/sqlite3` and `components/certs/certs.c` are vendored directly in the tree.

## 3. Install ESP-IDF Python requirements

PlatformIO creates an ESP-IDF venv at `~/.platformio/penv/.espidf-5.5.3/` but on some systems it's left empty. If builds fail with `ModuleNotFoundError: No module named 'idf_component_manager'`, use `uv` to populate it:

```sh
uv pip install \
  --python ~/.platformio/penv/.espidf-5.5.3/bin/python \
  -r ~/.platformio/packages/framework-espidf/tools/requirements/requirements.core.txt
```

We can't move the venv itself — PlatformIO's build scripts invoke that exact interpreter path — but `uv pip --python <path>` gives us a much faster resolver than plain `pip`.

The NVS pre-pop step (see §6) also needs `esp_idf_nvs_partition_gen` in that same env. Install it once:

```sh
# Linux/macOS
uv pip install --python ~/.platformio/penv/.espidf-5.5.0/bin/python esp_idf_nvs_partition_gen
```

```powershell
# Windows PowerShell
uv pip install --python "$env:USERPROFILE\.platformio\penv\.espidf-5.5.0\Scripts\python.exe" esp_idf_nvs_partition_gen
```

Adjust `.espidf-5.5.0` to whatever version PlatformIO created under `~/.platformio/penv/`. Symptom if you skip this: build fails with `No module named esp_idf_nvs_partition_gen`.

## 4. Build

```sh
pio run -e esp32-s3-devkitm-1
```

## 5. Flash + monitor

Connect the ESP32-S3 via USB. PlatformIO auto-detects the port; override with `--upload-port` if needed.

```sh
pio run -e esp32-s3-devkitm-1 -t upload
pio device monitor -b 115200
```

### Factory reset / re-provision

To re-provision a device, edit `.env` (or swap the bundle) and re-flash. The NVS image is regenerated on every build:

```sh
pio run -e esp32-s3-devkitm-1 -t erase_flash -t upload
```

`erase_flash` is strictly only needed if you want to wipe the persistence layer (SQLite / LittleFS). To update provisioning **without** wiping data, a plain `pio run -t upload` is enough — the new NVS image overwrites the old.

To intentionally flash stock firmware **without** any provisioning data, set `AMBYTE_NVS_SKIP=1` for the build. The device will boot, log `Device not provisioned`, and continue running for CLI debugging (Wi-Fi won't connect).

## 6. Provisioning (host-side NVS pre-pop)

Provisioning runs as a PlatformIO `pre:` hook ([tools/extra_script.py](tools/extra_script.py)) every build. It calls [tools/build_nvs_image.py](tools/build_nvs_image.py), which:

1. Loads `.env` from the repo root and resolves a cert bundle under `device_certs/`.
2. Generates an NVS CSV with namespaces `device_cfg`, `certs`, `wifi_prov`, `wifi_creds` — keyed exactly as the firmware expects.
3. Runs ESP-IDF's `nvs_partition_gen.py` to produce a 24 KB binary that PlatformIO flashes to offset `0x9000` alongside the firmware.

On first boot the device picks up the seeded Wi-Fi credentials, applies them via `esp_wifi_set_config()`, erases the seed, and connects.

The repo root is a `uv` project ([pyproject.toml](pyproject.toml), [uv.lock](uv.lock)). `uv` auto-creates `.venv/` from the lockfile on first run.

### Config resolution order

Each field resolves top-down (first wins):

1. Shell env var — `AMBYTE_SSID=foo`
2. `.env` file at the repo root — auto-loaded, gitignored
3. Cert paths can also be set explicitly via `AMBYTE_CA_CERT` / `AMBYTE_DEV_CERT` / `AMBYTE_DEV_KEY`, otherwise the bundle resolver picks them up automatically.

Bootstrap your `.env` once:

```sh
cp .env.example .env
$EDITOR .env
chmod 600 .env
```

The build fails loudly with the list of missing `AMBYTE_*` values if anything required isn't set — better than burning a flash cycle on a half-provisioned device.

### Device certs — bundle layout

Put each thing's AWS IoT Core PEM files in its own subdirectory under [device_certs/](device_certs/) (gitignored). The subdirectory name **must match the AWS IoT thing name** — it doubles as `AMBYTE_CLIENT_ID`.

```
device_certs/
  <thing-name-a>/
    AmazonRootCA1.pem
    <hash>-certificate.pem.crt
    <hash>-private.pem.key
  <thing-name-b>/
    ...
```

Pick the bundle via `AMBYTE_CERT_BUNDLE` in `.env`:

```sh
AMBYTE_CERT_BUNDLE=dom_ludo_prototype_ambyte_thing_v2
```

- `AMBYTE_CLIENT_ID` auto-derives from the bundle name — AWS IoT binds each cert to a specific thing and rejects any mismatched client id, so linking them makes the common case foolproof. Uncomment the `AMBYTE_CLIENT_ID` line in `.env` only if you deliberately need a different value.
- With only one subfolder, it's auto-selected silently. With several, the scripts list them and prompt on a TTY.
- Explicit `AMBYTE_CA_CERT` / `AMBYTE_DEV_CERT` / `AMBYTE_DEV_KEY` override bundle auto-discovery for their slot.
- `AMBYTE_TOPIC_ROOT` is **not** auto-derived; if the thing name appears inside it, update it by hand when switching bundles (AWS IoT policy scopes typically restrict publish/subscribe topics to strings containing the thing name).

Paths accept both `/` and `\` separators, so PowerShell-pasted paths work on Linux.

### Manual NVS image build (no flashing)

To inspect what would be flashed without actually uploading, run the generator directly:

```sh
uv run python tools/build_nvs_image.py --out /tmp/nvs.bin
# inspect with ESP-IDF's decoder
python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py decode /tmp/nvs.bin
```

## 7. MQTT TLS test client

[docs/mqtt_tls_test_client.py](docs/mqtt_tls_test_client.py) is a host-side paho-mqtt client that connects to your AWS IoT endpoint with the same device cert + key you provisioned — useful for reproducing issues without the firmware in the loop. It auto-loads `.env` (host, cert paths, client id, topic root), so no connection flags are needed:

```sh
# Subscribe to everything under your topic root (defaults to $AMBYTE_TOPIC_ROOT/#)
uv run docs/mqtt_tls_test_client.py --subscribe

# Publish a dummy measurement to $AMBYTE_TOPIC_ROOT (dummy JSON built from AMBYTE_* env)
uv run docs/mqtt_tls_test_client.py --publish --mqtt5

# Publish a custom message to a specific topic
uv run docs/mqtt_tls_test_client.py --publish some/topic --message 'hello from python'
```

Any flag overrides its `.env` counterpart. Remember AWS IoT Core's 7-slash limit on topic filters — see the device's publish topic shape at [components/device_commands/device_commands.c](components/device_commands/device_commands.c).

## Troubleshooting

| Symptom | Fix |
|---|---|
| `No module named 'idf_component_manager'` | §3 |
| `littlefs/lfs.h: No such file` | `git submodule update --init --recursive` |
| `ambyte-nvs: missing required value(s)` at build time | Add the listed `AMBYTE_*` keys to `.env`, or set `AMBYTE_NVS_SKIP=1` to flash without provisioning |
| `nvs_partition_gen.py not found at ...` | Point `IDF_PATH` at your ESP-IDF install (PlatformIO normally provides `~/.platformio/packages/framework-espidf`) |
| Device boots and logs `Device not provisioned` | NVS image wasn't flashed. Confirm `extra_scripts = pre:tools/extra_script.py` is set in `platformio.ini` and that `.env` resolves to non-empty values |
| MQTT connects then immediately disconnects (AWS kicks the client) | `AMBYTE_CLIENT_ID` doesn't match the thing the cert is bound to — align `AMBYTE_CERT_BUNDLE` with the thing name and let client_id auto-derive |

## Repository layout

```
components/          # ESP-IDF components
  certs/             # TLS cert store (NVS-backed)
  hal/               # board HAL (i2c_bus, ...)
  littlefs/          # submodule — flash filesystem
  sqlite3/           # vendored — local DB (siara-cc/esp32-idf-sqlite3, patched for IDF 5.x)
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
  wifi_manager/      # Wi-Fi connect + reconnect (NVS-seeded)

main/                # app entry point
tools/               # host-side scripts: NVS image builder + PlatformIO hook
docs/                # MQTT TLS test client (BLE provisioner is deprecated)
planning/            # LLM prompts, phase plans, hardware-test notes
device_certs/        # (gitignored) AWS IoT PEM files
.env / .env.example  # provisioning defaults
pyproject.toml       # Python deps (uv)
platformio.ini       # PlatformIO env config
```
