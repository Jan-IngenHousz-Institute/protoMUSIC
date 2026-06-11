#!/usr/bin/env python3
"""
Stage-2 hardware test — inbound MQTT command channel (docs/ota-update-plan.md).

Publishes a `ping` command to the device's command topic and verifies the device
replies with a matching `pong` on its status topic. Exits 0 on PASS, 1 on FAIL, so
it doubles as an automatable check.

What it exercises end-to-end:
  cloud --publish {"type":"ping","id":..} --> <topic_root>/cmd
  device (command_router) --> {"type":"pong","id":..,..} --> <topic_root>/status

Connection params come from `.env` (same file ambyte_prov.py / mqtt_tls_test_client.py
use); any CLI flag overrides. The command/reply topics are the SAME authorized,
full topics the device is provisioned with:
  * AMBYTE_COMMAND_TOPIC — device subscribes here; this tester PUBLISHES the ping.
  * AMBYTE_STATUS_TOPIC  — device publishes here; this tester SUBSCRIBES for the pong.
Set both in .env (the firmware reads them from NVS), or override with
--command-topic / --status-topic.

The device must be ONLINE for this test. Because AWS IoT permits only one connection
per client id, this tester connects with a DISTINCT client id ("<client_id>-test" by
default). If your AWS IoT policy pins iot:Connect to the thing-name client id, give
the tester its own cert/policy or pass a permitted --client-id.

Usage:
  uv run docs/stage2_command_test.py
  uv run docs/stage2_command_test.py --command-topic '<cmd topic>' --status-topic '<status topic>'
"""

import argparse
import json
import os
import ssl
import sys
import threading
import time
from urllib.parse import urlparse

# ── Load .env + resolve cert bundle (canonical impl: tools/_prov_env.py) ──
# Loaded by file path under a unique module name: the docs/_prov_env.py shim
# self-collides on the module name "_prov_env" when docs/ is first on sys.path.
import importlib.util  # noqa: E402

_PROV = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "tools", "_prov_env.py")
_spec = importlib.util.spec_from_file_location("_prov_env_canon", _PROV)
_prov = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_prov)
load_dotenv = _prov.load_dotenv
resolve_cert_bundle = _prov.resolve_cert_bundle

load_dotenv()
resolve_cert_bundle()

import paho.mqtt.client as mqtt  # noqa: E402


def _parse_mqtt_uri(uri: str) -> tuple[str, int]:
    if "://" not in uri:
        uri = "mqtts://" + uri
    u = urlparse(uri)
    if not u.hostname:
        raise SystemExit(f"cannot parse host from AMBYTE_MQTT_URI={uri!r}")
    return u.hostname, u.port or 8883


def main():
    env_host, env_port = (None, 8883)
    if os.environ.get("AMBYTE_MQTT_URI"):
        env_host, env_port = _parse_mqtt_uri(os.environ["AMBYTE_MQTT_URI"])

    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host",       default=env_host)
    p.add_argument("--port",       type=int, default=env_port)
    p.add_argument("--ca",         default=os.environ.get("AMBYTE_CA_CERT"))
    p.add_argument("--cert",       default=os.environ.get("AMBYTE_DEV_CERT"))
    p.add_argument("--key",        default=os.environ.get("AMBYTE_DEV_KEY"))
    p.add_argument("--command-topic", default=os.environ.get("AMBYTE_COMMAND_TOPIC"),
                   help="Topic to PUBLISH the command to (device subscribes). Default: $AMBYTE_COMMAND_TOPIC")
    p.add_argument("--status-topic",  default=os.environ.get("AMBYTE_STATUS_TOPIC"),
                   help="Topic to SUBSCRIBE for the reply (device publishes). Default: $AMBYTE_STATUS_TOPIC")
    p.add_argument("--mac",        help="Board MAC, only to expand {MAC} in the tester client id")
    p.add_argument("--client-id",  help="Tester client id (default: <AMBYTE_CLIENT_ID>-test)")
    p.add_argument("--id",         default=f"ping-{int(time.time())}", help="Command id to send")
    p.add_argument("--timeout",    type=int, default=15, help="Seconds to wait for the pong")
    p.add_argument("--mqtt5",      action="store_true")
    p.add_argument("--insecure",   action="store_true")
    p.add_argument("--debug",      action="store_true")
    args = p.parse_args()

    missing = [n for n, v in [("--host/$AMBYTE_MQTT_URI", args.host),
                              ("--ca/$AMBYTE_CA_CERT", args.ca),
                              ("--cert/$AMBYTE_DEV_CERT", args.cert),
                              ("--key/$AMBYTE_DEV_KEY", args.key)] if not v]
    if missing:
        sys.exit(f"Error: missing required config: {', '.join(missing)}. "
                 f"Populate .env from .env.example or pass the flags.")

    cmd_topic = args.command_topic
    status_topic = args.status_topic
    if not cmd_topic or not status_topic:
        sys.exit("Error: set AMBYTE_COMMAND_TOPIC and AMBYTE_STATUS_TOPIC in .env "
                 "(or pass --command-topic / --status-topic).")

    base_client_id = os.environ.get("AMBYTE_CLIENT_ID", "ambyte")
    if base_client_id and "{MAC}" in base_client_id and args.mac:
        base_client_id = base_client_id.replace("{MAC}", args.mac)
    client_id = args.client_id or f"{base_client_id}-test"

    # ── synchronisation: set when a matching pong arrives ────────────
    done = threading.Event()
    result = {"ok": False, "payload": None}

    protocol = mqtt.MQTTv5 if args.mqtt5 else mqtt.MQTTv311
    client = mqtt.Client(client_id=client_id, protocol=protocol)
    client.tls_set(ca_certs=args.ca, certfile=args.cert, keyfile=args.key,
                   cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.tls_insecure_set(args.insecure)
    if args.debug:
        client.enable_logger()

    def on_connect(c, u, flags, reason_code, properties=None):
        print(f"[connect] reason_code={reason_code}")
        if reason_code == 0:
            c.subscribe(status_topic, qos=1)
            print(f"[subscribe] {status_topic}")

    def on_message(c, u, msg):
        payload = msg.payload.decode("utf-8", errors="replace")
        print(f"[recv] {msg.topic}: {payload}")
        try:
            obj = json.loads(payload)
        except ValueError:
            return
        if obj.get("type") == "pong" and obj.get("id") == args.id:
            result["ok"] = True
            result["payload"] = obj
            done.set()

    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[start] host={args.host} client_id={client_id}")
    print(f"[topics] cmd={cmd_topic}")
    print(f"         status={status_topic}")
    try:
        client.connect(args.host, args.port, keepalive=30)
    except Exception as e:
        sys.exit(f"[CONNECT ERROR] {e}\n"
                 f"If this is a policy/client-id rejection, the tester needs a "
                 f"permitted --client-id or its own cert/policy.")
    client.loop_start()

    # Give the subscription a moment to register before publishing.
    time.sleep(1.5)
    command = json.dumps({"type": "ping", "id": args.id})
    info = client.publish(cmd_topic, payload=command, qos=1)
    info.wait_for_publish(timeout=5)
    print(f"[publish] {cmd_topic}: {command} (rc={info.rc})")

    print(f"[wait] up to {args.timeout}s for pong id={args.id} ...")
    got = done.wait(timeout=args.timeout)
    client.loop_stop()
    client.disconnect()

    if got and result["ok"]:
        print(f"\nPASS — device replied: {json.dumps(result['payload'])}")
        sys.exit(0)
    print("\nFAIL — no matching pong received.\n"
          "Check: device online? AWS policy allows Subscribe/Receive on <root>/cmd "
          "and Publish on <root>/status? topic_root matches the device boot log?")
    sys.exit(1)


if __name__ == "__main__":
    main()
