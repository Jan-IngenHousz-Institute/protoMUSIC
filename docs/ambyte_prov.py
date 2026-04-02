#!/usr/bin/env python3
"""
Ambyte BLE Full Provisioning Script
=====================================
Sends over one BLE session (120-second window):
  1. Wi-Fi credentials        -> standard "prov-config" endpoint
  2. MQTT device config       -> custom "dev-cfg" endpoint
  3. TLS certificates         -> custom "cert-prov" endpoint (chunked)

After all three succeed the device reboots automatically (prov_ok || any_write).

Usage (PowerShell):
  python docs\\ambyte_prov.py `
    --ssid       YOUR_WIFI `
    --password   YOUR_WIFI_PASS `
    --mqtt_uri   "mqtts://XXXX.iot.REGION.amazonaws.com:8883" `
    --client_id  "thing-001" `
    --topic_root "ambyte/prod" `
    --device_id  "thing-001" `
    --ca_cert    ca.pem `
    --dev_cert   device.crt `
    --dev_key    device.key

Prerequisites (one-time):
  pip install bleak protobuf cryptography

Note: on Windows, run PowerShell as Administrator if BLE access is denied.
"""

import argparse
import asyncio
import json
import os
import struct
import sys

# ── Locate esp_prov inside PlatformIO's bundled ESP-IDF ──────────────
_IDF_PATH = os.path.expanduser("~/.platformio/packages/framework-espidf")
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
        if args.ssid is not None:
            print("[1/3] Wi-Fi credentials...")
            if not await esp_prov.send_wifi_config(tp, sec, args.ssid, args.password):
                raise RuntimeError("Wi-Fi config rejected by device")
            if not await esp_prov.apply_wifi_config(tp, sec):
                raise RuntimeError("Wi-Fi apply failed")
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


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Ambyte full BLE provisioning: WiFi + MQTT config + TLS certs",
        formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--ssid",        required=True,
                   help="Wi-Fi SSID")
    p.add_argument("--password",    required=True,
                   help="Wi-Fi password")
    p.add_argument("--mqtt_uri",    required=True,
                   help="MQTT broker URI\n"
                        "e.g. mqtts://XXXX.iot.REGION.amazonaws.com:8883")
    p.add_argument("--client_id",   required=True,
                   help="MQTT client ID (e.g. thing-001)")
    p.add_argument("--topic_root",  required=True,
                   help="MQTT topic root prefix (e.g. ambyte/prod)")
    p.add_argument("--device_id",   required=True,
                   help="Device ID embedded in MQTT topics (e.g. thing-001)")
    p.add_argument("--protocol_id", required=True,
                   help="MultispeQ protocol ID (e.g. 3517)")
    p.add_argument("--device_name", required=True,
                   help="Device name reported in payload (e.g. AmbyteOnAir)")
    p.add_argument("--device_version", required=True,
                   help="Device hardware version string (e.g. 1)")
    p.add_argument("--device_firmware", required=True,
                   help="Device firmware version string (e.g. 1)")
    p.add_argument("--firmware_version", required=True,
                   help="Firmware version string (e.g. 1)")
    p.add_argument("--ca_cert",     required=True,
                   help="Path to AWS root CA certificate PEM file")
    p.add_argument("--dev_cert",    required=True,
                   help="Path to device certificate PEM file")
    p.add_argument("--dev_key",     required=True,
                   help="Path to device private key PEM file")
    return p.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
