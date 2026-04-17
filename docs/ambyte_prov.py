#!/usr/bin/env python3
"""
Ambyte BLE Full Provisioning Script
=====================================
Sends over one BLE session (120-second window):
  1. Wi-Fi credentials        -> standard "prov-config" endpoint
  2. MQTT device config       -> custom "dev-cfg" endpoint
  3. TLS certificates         -> custom "cert-prov" endpoint (chunked)

After all three succeed the device reboots automatically (prov_ok || any_write).

Any field can be supplied via:
  - CLI flag                 (--ssid YOUR_WIFI)
  - Environment variable     (AMBYTE_SSID=YOUR_WIFI)
  - Cached config file       (~/.ambyte_prov.json, auto-written after first run)
  - Interactive prompt       (fallback for anything still missing)

Precedence is top-down — CLI beats env beats cache beats prompt. Subsequent
runs can therefore be just:

  python docs/ambyte_prov.py

and anything missing will be prompted. The Wi-Fi password is never cached on
disk; it's always read from $AMBYTE_PASSWORD, --password, or a getpass prompt.

Prerequisites (one-time):
  pip install bleak protobuf cryptography

Note: on Windows, run PowerShell as Administrator if BLE access is denied.
"""

import argparse
import asyncio
import getpass
import json
import os
import struct
import sys
from pathlib import Path

# ── Auto-load .env from repo root (no dotenv dependency) ─────────────
def _load_dotenv() -> None:
    env_path = Path(__file__).resolve().parent.parent / ".env"
    if not env_path.exists():
        return
    for raw in env_path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key   = key.strip()
        value = value.strip().strip('"').strip("'")
        # Existing shell env wins — .env only fills gaps.
        os.environ.setdefault(key, value)

_load_dotenv()

# ── Locate esp_prov inside PlatformIO's bundled ESP-IDF ──────────────
_IDF_PATH = os.environ.get("IDF_PATH") or os.path.expanduser("~/.platformio/packages/framework-espidf")
os.environ["IDF_PATH"] = _IDF_PATH
sys.path.insert(0, os.path.join(_IDF_PATH, "tools", "esp_prov"))
sys.path.insert(0, os.path.join(_IDF_PATH, "components", "protocomm", "python"))

import esp_prov   # noqa: E402 — tools/esp_prov/esp_prov.py
import security   # noqa: E402 — tools/esp_prov/security/
import transport  # noqa: E402 — tools/esp_prov/transport/

# ── Device constants (must match firmware) ────────────────────────────
DEVICE_NAME = "AMBYTE"
POP         = "ambyte123"

# Safe payload bytes per cert-prov send call.
# The 4-byte length header takes 4 bytes on the first call, so the first
# data window is CHUNK_SIZE-4. Subsequent calls carry CHUNK_SIZE bytes.
# 200 is well under the minimum negotiated BLE MTU (247 bytes) after
# AES-CTR encryption overhead.
CHUNK_SIZE = 200


# ── Transport helpers ─────────────────────────────────────────────────

def _to_latin1(data: bytes) -> str:
    """bytes -> latin-1 str expected by Transport_BLE.send_data."""
    return data.decode("latin-1")


def _from_latin1(data: str) -> bytes:
    """latin-1 str returned by Transport_BLE.send_data -> bytes."""
    return bytes(data, "latin-1")


async def _send_raw(tp, sec, endpoint: str, payload: bytes) -> bytes:
    """
    Encrypt `payload`, send to `endpoint` inside the established Security1
    session, decrypt and return the raw response bytes.

    One call = one protocomm request/response exchange = one handler invocation
    on the device side.
    """
    encrypted    = sec.encrypt_data(payload)            # bytes -> bytes (AES-CTR)
    response_str = await tp.send_data(endpoint, _to_latin1(encrypted))
    return sec.decrypt_data(_from_latin1(response_str)) # bytes


# ── Custom endpoint: MQTT device config ──────────────────────────────

async def send_dev_cfg(tp, sec,
                       mqtt_uri: str, client_id: str,
                       topic_root: str, device_id: str,
                       protocol_id: str, device_name: str,
                       device_version: str, device_firmware: str,
                       firmware_version: str) -> None:
    """
    Send MQTT runtime config to the 'dev-cfg' endpoint.
    Payload is a plain JSON object — always fits in a single BLE send.
    Device handler: dev_cfg_prov_handler in app_main.c.
    """
    payload = json.dumps({
        "mqtt_uri":         mqtt_uri,
        "mqtt_client_id":   client_id,
        "mqtt_topic_root":  topic_root,
        "device_id":        device_id,
        "protocol_id":      protocol_id,
        "device_name":      device_name,
        "device_version":   device_version,
        "device_firmware":  device_firmware,
        "firmware_version": firmware_version,
    }).encode()

    response = await _send_raw(tp, sec, "dev-cfg", payload)
    result   = json.loads(response)
    if not result.get("ok"):
        raise RuntimeError(f"dev-cfg rejected by device: {result}")
    print("  dev-cfg: OK")


# ── Custom endpoint: TLS certificates ────────────────────────────────

async def send_cert_prov(tp, sec,
                         ca_cert: str, dev_cert: str, dev_key: str) -> None:
    """
    Send TLS certs to the 'cert-prov' endpoint using the chunked wire protocol
    implemented in cert_prov_handler (app_main.c):

      First call  : 4-byte big-endian total_len  +  first chunk of JSON payload
      Next calls  : raw JSON payload chunks
      Final call  : device responds {"ok": true}

    Single-chunk path: if the whole payload fits in CHUNK_SIZE-4 bytes the
    loop is skipped and the device processes everything in the first call.
    """
    payload = json.dumps({
        "ca_cert":  ca_cert,
        "dev_cert": dev_cert,
        "dev_key":  dev_key,
    }).encode()

    total  = len(payload)
    offset = 0
    result = {}

    # First chunk — prepend 4-byte big-endian total length
    first_data_len = min(CHUNK_SIZE - 4, total)
    first_chunk    = struct.pack(">I", total) + payload[:first_data_len]
    offset        += first_data_len

    response = await _send_raw(tp, sec, "cert-prov", first_chunk)
    result   = json.loads(response)
    if result.get("ok") is False:
        raise RuntimeError(f"cert-prov: device rejected first chunk: {result}")
    print(f"  cert-prov: {offset}/{total} bytes sent...")

    # Remaining chunks (skipped when payload fit in the first send)
    while offset < total:
        chunk    = payload[offset : offset + CHUNK_SIZE]
        offset  += len(chunk)
        response = await _send_raw(tp, sec, "cert-prov", chunk)
        result   = json.loads(response)
        if result.get("ok") is False:
            raise RuntimeError(
                f"cert-prov: device rejected chunk at byte {offset}: {result}")
        print(f"  cert-prov: {offset}/{total} bytes sent...")

    if not result.get("ok"):
        raise RuntimeError(f"cert-prov: device did not confirm completion: {result}")
    print("  cert-prov: OK")


# ── Main ──────────────────────────────────────────────────────────────

async def run(args) -> None:
    # 1. BLE connect + SRP6a key exchange
    print(f"Scanning for '{DEVICE_NAME}'...")
    tp = await esp_prov.get_transport("ble", DEVICE_NAME)
    if tp is None:
        raise RuntimeError(f"Could not connect to '{DEVICE_NAME}'")

    sec = security.Security1(pop=POP, verbose=False)
    print("Establishing secure session (SRP6a + AES-CTR)...")
    if not await esp_prov.establish_session(tp, sec):
        await tp.disconnect()
        raise RuntimeError("Security session handshake failed")
    print("Session established\n")

    try:
        # 2. Wi-Fi credentials (standard endpoint)
        if args.skip_wifi:
            print("[1/3] Skipping Wi-Fi credentials (--skip-wifi)")
        elif args.ssid is not None:
            print("[1/3] Wi-Fi credentials...")
            if not await esp_prov.send_wifi_config(tp, sec, args.ssid, args.password):
                raise RuntimeError("Wi-Fi config rejected by device")
            if not await esp_prov.apply_wifi_config(tp, sec):
                raise RuntimeError(
                    "Wi-Fi apply failed — the device likely already has Wi-Fi\n"
                    "  configured from a previous run. To re-provision, either:\n"
                    "    • erase flash and reflash:  pio run -e esp32-s3-devkitm-1 -t erase_flash -t upload\n"
                    "    • or skip Wi-Fi and update only MQTT/certs:  --skip-wifi")
            print("  prov-config: OK")
        else:
            print("[1/3] Skipping Wi-Fi credentials (none provided)")

        # 3. MQTT device config (custom endpoint)
        print("[2/3] MQTT device config...")
        await send_dev_cfg(tp, sec,
                           mqtt_uri         = args.mqtt_uri,
                           client_id        = args.client_id,
                           topic_root       = args.topic_root,
                           device_id        = args.device_id,
                           protocol_id      = args.protocol_id,
                           device_name      = args.device_name,
                           device_version   = args.device_version,
                           device_firmware  = args.device_firmware,
                           firmware_version = args.firmware_version)

        # 4. TLS certificates (custom endpoint, chunked)
        print("[3/3] TLS certificates...")
        await send_cert_prov(tp, sec,
                             ca_cert  = open(args.ca_cert).read(),
                             dev_cert = open(args.dev_cert).read(),
                             dev_key  = open(args.dev_key).read())

    finally:
        await tp.disconnect()
        print("")

    print("All done. The device will reboot and connect to AWS IoT Core with TLS.")


# ── Config resolution ────────────────────────────────────────────────
#
# Each field resolves in this precedence order:
#   1. CLI flag                  (--ssid foo)
#   2. Environment variable      (AMBYTE_SSID=foo)
#   3. Cached config file        (~/.ambyte_prov.json)
#   4. Interactive prompt        (input() / getpass for secrets)
#
# After all fields are resolved, non-secret values are written back to the
# config file so the next run offers them as defaults.

CONFIG_PATH = Path.home() / ".ambyte_prov.json"

# (cli_name, prompt_label, is_secret, is_path)
FIELDS = [
    ("ssid",             "Wi-Fi SSID",                                    False, False),
    ("password",         "Wi-Fi password",                                True,  False),
    ("mqtt_uri",         "MQTT broker URI (mqtts://...:8883)",            False, False),
    ("client_id",        "MQTT client ID",                                False, False),
    ("topic_root",       "MQTT topic root prefix",                        False, False),
    ("device_id",        "Device ID (embedded in MQTT topics)",           False, False),
    ("protocol_id",      "MultispeQ protocol ID",                         False, False),
    ("device_name",      "Device name (e.g. AmbyteOnAir)",                False, False),
    ("device_version",   "Device hardware version",                       False, False),
    ("device_firmware",  "Device firmware version",                       False, False),
    ("firmware_version", "Firmware version",                              False, False),
    ("ca_cert",          "Path to AWS root CA PEM",                       False, True),
    ("dev_cert",         "Path to device certificate PEM",                False, True),
    ("dev_key",          "Path to device private key PEM",                False, True),
]


def _load_cached() -> dict:
    try:
        with open(CONFIG_PATH, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _save_cached(values: dict) -> None:
    safe = {k: v for k, v in values.items()
            if v is not None and not any(n == k and secret for n, _, secret, _ in FIELDS)}
    try:
        with open(CONFIG_PATH, "w") as f:
            json.dump(safe, f, indent=2)
        os.chmod(CONFIG_PATH, 0o600)
    except OSError as e:
        print(f"(warning: could not save {CONFIG_PATH}: {e})", file=sys.stderr)


def _normalize_path(value: str) -> str:
    # Accept Windows-style backslash paths too (from PowerShell one-liners).
    return os.path.normpath(value.replace("\\", "/"))


def _prompt(label: str, default: str | None, secret: bool) -> str:
    if not sys.stdin.isatty():
        raise SystemExit(f"missing required value for '{label}' and no TTY for prompting")
    if secret:
        shown = " [press enter to keep saved]" if default else ""
        val = getpass.getpass(f"{label}{shown}: ")
        return val or (default or "")
    suffix = f" [{default}]" if default else ""
    val = input(f"{label}{suffix}: ").strip()
    return val or (default or "")


def resolve_config(cli: argparse.Namespace) -> argparse.Namespace:
    cached  = _load_cached()
    final   = {}
    changed = False

    for name, label, secret, is_path in FIELDS:
        value = getattr(cli, name, None)
        if value is None:
            value = os.environ.get(f"AMBYTE_{name.upper()}")
        if value is None:
            value = cached.get(name)
        if value is None or value == "":
            value = _prompt(label, cached.get(name), secret)
            changed = True
        if not value:
            raise SystemExit(f"'{label}' is required")
        if is_path:
            value = _normalize_path(value)
        final[name] = value

    # Fail fast on bad cert paths so we don't burn a 120s BLE session.
    for name, _, _, is_path in FIELDS:
        if is_path and not os.path.isfile(final[name]):
            raise SystemExit(
                f"{name}: file not found: {final[name]}\n"
                f"Check your .env / {CONFIG_PATH} / CLI flag.")

    if changed:
        _save_cached(final)

    # Preserve non-FIELD args (e.g. --skip-wifi) passed through untouched.
    for k, v in vars(cli).items():
        final.setdefault(k, v)

    return argparse.Namespace(**final)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Ambyte full BLE provisioning: WiFi + MQTT config + TLS certs.\n"
                    "Any flag omitted falls back to $AMBYTE_<FIELD>, then .env in the\n"
                    f"repo root, then {CONFIG_PATH}, then an interactive prompt.",
        formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--ssid",             help="Wi-Fi SSID")
    p.add_argument("--password",         help="Wi-Fi password")
    p.add_argument("--mqtt_uri",         help="MQTT broker URI (mqtts://...:8883)")
    p.add_argument("--client_id",        help="MQTT client ID")
    p.add_argument("--topic_root",       help="MQTT topic root prefix")
    p.add_argument("--device_id",        help="Device ID embedded in MQTT topics")
    p.add_argument("--protocol_id",      help="MultispeQ protocol ID")
    p.add_argument("--device_name",      help="Device name reported in payload")
    p.add_argument("--device_version",   help="Device hardware version")
    p.add_argument("--device_firmware",  help="Device firmware version")
    p.add_argument("--firmware_version", help="Firmware version")
    p.add_argument("--ca_cert",          help="Path to AWS root CA PEM")
    p.add_argument("--dev_cert",         help="Path to device certificate PEM")
    p.add_argument("--dev_key",          help="Path to device private key PEM")
    p.add_argument("--skip-wifi", action="store_true",
                   default=os.environ.get("AMBYTE_SKIP_WIFI", "").lower() in ("1", "true", "yes"),
                   help="Skip step 1 (Wi-Fi apply). Use when the device already has\n"
                        "Wi-Fi from a previous run and you only want to update MQTT/certs.")
    return p.parse_args()


if __name__ == "__main__":
    asyncio.run(run(resolve_config(parse_args())))
