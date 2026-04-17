#!/usr/bin/env python3
"""
MQTT TLS test client (AWS IoT Core / mutual TLS friendly).

Reads all connection parameters from `.env` in the repo root (same file
`ambyte_prov.py` uses), so the usual invocation is just:

  uv run docs/mqtt_tls_test_client.py --subscribe '<topic>/#'
  uv run docs/mqtt_tls_test_client.py --publish '<topic>' --message 'hello'

Any CLI flag overrides the `.env` / shell-env equivalent.

`.env` keys honoured (from ambyte_prov):
  AMBYTE_MQTT_URI   -> host + port      (e.g. mqtts://XXX-ats.iot.REGION.amazonaws.com:8883)
  AMBYTE_CLIENT_ID  -> client id
  AMBYTE_CA_CERT    -> root CA PEM path
  AMBYTE_DEV_CERT   -> device cert PEM path
  AMBYTE_DEV_KEY    -> device key PEM path
  AMBYTE_TOPIC_ROOT -> default topic (subscribe '<root>/#' / publish '<root>')
"""

import argparse
import json
import os
import ssl
import sys
import time
from urllib.parse import urlparse


# ── Load .env + resolve cert bundle (shared helper) ──────────────────
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _prov_env import load_dotenv, resolve_cert_bundle  # noqa: E402

load_dotenv()
resolve_cert_bundle()


def _parse_mqtt_uri(uri: str) -> tuple[str, int]:
    """Accept mqtts://host[:port] or host[:port] and return (host, port)."""
    if "://" not in uri:
        uri = "mqtts://" + uri
    u = urlparse(uri)
    if not u.hostname:
        raise SystemExit(f"cannot parse host from AMBYTE_MQTT_URI={uri!r}")
    return u.hostname, u.port or 8883


import paho.mqtt.client as mqtt  # noqa: E402 — after _load_dotenv so nothing depends on env yet


def build_client(args) -> mqtt.Client:
    protocol = mqtt.MQTTv5 if args.mqtt5 else mqtt.MQTTv311
    client = mqtt.Client(client_id=args.client_id, protocol=protocol)

    if args.username is not None:
        client.username_pw_set(args.username, args.password)

    client.tls_set(
        ca_certs=args.ca,
        certfile=args.cert,
        keyfile=args.key,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLS_CLIENT,
    )
    client.tls_insecure_set(args.insecure)

    if args.debug:
        client.enable_logger()

    def on_connect(c, userdata, flags, reason_code, properties=None):
        print(f"[on_connect] reason_code={reason_code} flags={flags}")
        if reason_code == 0 and args.subscribe:
            c.subscribe(args.subscribe, qos=args.qos)
            print(f"[subscribe] topic={args.subscribe} qos={args.qos}")

    def on_disconnect(c, userdata, reason_code, properties=None):
        print(f"[on_disconnect] reason_code={reason_code}")

    def on_subscribe(c, userdata, mid, granted_qos, properties=None):
        print(f"[on_subscribe] mid={mid} granted_qos={granted_qos}")

    def on_message(c, userdata, msg):
        try:
            payload = msg.payload.decode("utf-8", errors="replace")
        except Exception:
            payload = repr(msg.payload)
        print(f"[message] {msg.topic} qos={msg.qos} retain={msg.retain} payload={payload}")

    def on_publish(c, userdata, mid):
        print(f"[on_publish] mid={mid}")

    client.on_connect    = on_connect
    client.on_disconnect = on_disconnect
    client.on_subscribe  = on_subscribe
    client.on_message    = on_message
    client.on_publish    = on_publish
    return client


def _default_payload(args) -> str:
    """Dummy measurement payload; overridden by --message if provided."""
    if args.message is not None:
        return args.message
    return json.dumps({
        "sample": [{"protocol_id": os.environ.get("AMBYTE_PROTOCOL_ID", "3517"), "set": []}],
        "device_firmware":  os.environ.get("AMBYTE_DEVICE_FIRMWARE",  "1"),
        "device_id":        os.environ.get("AMBYTE_DEVICE_ID",        "00:00:00:00"),
        "device_name":      os.environ.get("AMBYTE_DEVICE_NAME",      "AmbyteOnAir"),
        "device_version":   os.environ.get("AMBYTE_DEVICE_VERSION",   "1"),
        "firmware_version": os.environ.get("AMBYTE_FIRMWARE_VERSION", "1"),
        "timestamp":        time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime()),
    })


def main():
    env_host, env_port = (None, 8883)
    if os.environ.get("AMBYTE_MQTT_URI"):
        env_host, env_port = _parse_mqtt_uri(os.environ["AMBYTE_MQTT_URI"])

    parser = argparse.ArgumentParser(
        description="MQTT TLS test client. Defaults pulled from .env / AMBYTE_* env vars.",
        formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--host",      default=env_host,                              help="MQTT broker host (default: $AMBYTE_MQTT_URI)")
    parser.add_argument("--port",      type=int, default=env_port,                    help="MQTT TLS port (default: from URI or 8883)")
    parser.add_argument("--client-id", default=os.environ.get("AMBYTE_CLIENT_ID"),    help="MQTT client ID (default: $AMBYTE_CLIENT_ID)")
    parser.add_argument("--ca",        default=os.environ.get("AMBYTE_CA_CERT"),      help="Root CA PEM (default: $AMBYTE_CA_CERT)")
    parser.add_argument("--cert",      default=os.environ.get("AMBYTE_DEV_CERT"),     help="Device cert PEM (default: $AMBYTE_DEV_CERT)")
    parser.add_argument("--key",       default=os.environ.get("AMBYTE_DEV_KEY"),      help="Device key PEM (default: $AMBYTE_DEV_KEY)")
    parser.add_argument("--subscribe", nargs="?", const=f"{os.environ.get('AMBYTE_TOPIC_ROOT', '')}/#".lstrip("/"),
                        help="Topic to subscribe to. With no argument, defaults to '$AMBYTE_TOPIC_ROOT/#'.")
    parser.add_argument("--publish",   nargs="?", const=os.environ.get("AMBYTE_TOPIC_ROOT"),
                        help="Topic to publish to. With no argument, defaults to $AMBYTE_TOPIC_ROOT.")
    parser.add_argument("--message",   help="Payload. If omitted on --publish, a dummy AMBYTE_* measurement JSON is sent.")
    parser.add_argument("--qos",       type=int, default=0, choices=[0, 1, 2])
    parser.add_argument("--keepalive", type=int, default=30)
    parser.add_argument("--mqtt5",     action="store_true", help="Use MQTT v5 (default: v3.1.1)")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--insecure",  action="store_true", help="Disable TLS cert verification (debug only)")
    parser.add_argument("--debug",     action="store_true", help="Enable paho logger output")
    parser.add_argument("--timeout",   type=int, default=10, help="Seconds to linger after publish before exit")
    args = parser.parse_args()

    # Validate mandatory connection params
    missing = [name for name, val in
               [("--host/$AMBYTE_MQTT_URI", args.host),
                ("--ca/$AMBYTE_CA_CERT",    args.ca),
                ("--cert/$AMBYTE_DEV_CERT", args.cert),
                ("--key/$AMBYTE_DEV_KEY",   args.key),
                ("--client-id/$AMBYTE_CLIENT_ID", args.client_id)]
               if not val]
    if missing:
        print(f"Error: missing required config: {', '.join(missing)}", file=sys.stderr)
        print("Populate .env from .env.example, or pass the CLI flags.", file=sys.stderr)
        sys.exit(2)

    if not args.subscribe and not args.publish:
        print("Error: provide at least --subscribe or --publish", file=sys.stderr)
        sys.exit(2)

    client = build_client(args)

    print(f"[connect] host={args.host} port={args.port} client_id={args.client_id} mqtt5={args.mqtt5}")
    try:
        client.connect(args.host, args.port, keepalive=args.keepalive)
    except ssl.SSLError as e:
        print(f"[TLS ERROR] {e}")
        print("Usually CA/cert/key mismatch, wrong endpoint, or verification failed.")
        sys.exit(1)
    except Exception as e:
        print(f"[CONNECT ERROR] {e}")
        sys.exit(1)

    client.loop_start()

    if args.publish:
        time.sleep(1)  # let on_connect settle
        payload = _default_payload(args)
        info = client.publish(args.publish, payload=payload, qos=args.qos)
        info.wait_for_publish(timeout=5)
        print(f"[publish] topic={args.publish} qos={args.qos} rc={info.rc}")

        if not args.subscribe:
            time.sleep(args.timeout)
            client.loop_stop()
            client.disconnect()
            return

    if args.subscribe:
        print("[run] Listening… Ctrl+C to stop.")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            client.loop_stop()
            client.disconnect()


if __name__ == "__main__":
    main()
