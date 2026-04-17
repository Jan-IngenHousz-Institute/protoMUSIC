

#!/usr/bin/env python3
"""
MQTT TLS test client (AWS IoT Core / mutual TLS friendly)

Examples:

# Subscribe (MQTT 3.1.1) on port 8883
python mqtt_test.py \
  --host a1234567890-ats.iot.eu-west-1.amazonaws.com \
  --port 8883 \
  --client-id esp32-test \
  --ca AmazonRootCA1.pem \
  --cert device-cert.pem \
  --key device-private.key \
  --subscribe test/topic

# Publish once
python mqtt_test.py \
  --host a1234567890-ats.iot.eu-west-1.amazonaws.com \
  --port 8883 \
  --client-id esp32-test \
  --ca AmazonRootCA1.pem \
  --cert device-cert.pem \
  --key device-private.key \
  --publish test/topic --message "hello from python"

# MQTT v5
python mqtt_test.py ... --mqtt5

use as:

////// old ///// python '..\MQTT TLS test client.py' --ca .\AmazonRootCA1.pem --cert .\5981af399524454b9af0055c6f48a6d69e98c1088d4c29cc42177961a1f9ac8c-certificate.pem.crt --key .\5981af399524454b9af0055c6f48a6d69e98c1088d4c29cc42177961a1f9ac8c-private.pem.key --host a2s5vvyojsnl53-ats.iot.eu-central-1.amazonaws.com --mqtt5 --publish experiment/data_ingest/v1/8d044362-8719-4823-847a-61073f8c8f97/multispeq/v1.0/dom_ludo_prototype_ambyte_thing/test_server  --client-id dom_ludo_prototype_ambyte_thing
python 'docs\MQTT TLS test client.py' --message "hello again from python" --ca device_certs\AmazonRootCA1.pem --cert device_certs\5981af399524454b9af0055c6f48a6d69e98c1088d4c29cc42177961a1f9ac8c-certificate.pem.crt --key device_certs\5981af399524454b9af0055c6f48a6d69e98c1088d4c29cc42177961a1f9ac8c-private.pem.key --host a2s5vvyojsnl53-ats.iot.eu-central-1.amazonaws.com --mqtt5 --publish experiment/data_ingest/v1/8d044362-8719-4823-847a-61073f8c8f97/multispeq/v1.0/dom_ludo_prototype_ambyte_thing/test_server  --client-id dom_ludo_prototype_ambyte_thing
"""

import argparse
import ssl
import sys
import time
from xmlrpc import client

import paho.mqtt.client as mqtt


def build_client(args) -> mqtt.Client:
    protocol = mqtt.MQTTv5 if args.mqtt5 else mqtt.MQTTv311
    client = mqtt.Client(client_id=args.client_id, protocol=protocol)

    # Optional username/password (NOT used for AWS IoT mTLS by default)
    if args.username is not None:
        client.username_pw_set(args.username, args.password)

    # TLS setup (mutual TLS: CA + client cert + client key)
    # For AWS IoT Core on 8883, this is the common configuration.
    client.tls_set(
        ca_certs=args.ca,
        certfile=args.cert,
        keyfile=args.key,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLS_CLIENT,
    )

    # Ensure verification is ON (recommended). Only disable for local debugging.
    client.tls_insecure_set(args.insecure)

    # Logging / debug
    if args.debug:
        client.enable_logger()

    # Callbacks
    def on_connect(c, userdata, flags, reason_code, properties=None):
        # paho uses "reason_code" for v5 and v3.1.1 (it’s 0 on success)
        print(f"[on_connect] reason_code={reason_code} flags={flags}")
        if reason_code == 0:
            if args.subscribe:
                c.subscribe(args.subscribe, qos=args.qos)
                print(f"[subscribe] topic={args.subscribe} qos={args.qos}")
        else:
            print("[on_connect] Connection refused / failed (non-zero reason_code).")

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

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_subscribe = on_subscribe
    client.on_message = on_message
    client.on_publish = on_publish

    return client


def main():
    parser = argparse.ArgumentParser(description="MQTT TLS test client")
    parser.add_argument("--host", required=True, help="MQTT broker hostname (AWS IoT endpoint)")
    parser.add_argument("--port", type=int, default=8883, help="MQTT TLS port (default: 8883)")
    parser.add_argument("--client-id", default="dom_ludo_prototype_ambyte_thing", help="MQTT client ID")
    parser.add_argument("--ca", required=True, help="Path to Root CA PEM (e.g., AmazonRootCA1.pem)")
    parser.add_argument("--cert", required=True, help="Path to device/client certificate PEM")
    parser.add_argument("--key", required=True, help="Path to device/client private key PEM")
    parser.add_argument("--subscribe", help="Topic to subscribe to")
    parser.add_argument("--publish", help="Topic to publish to")
    parser.add_argument("--message", default="test", help="Message payload for publish")
    parser.add_argument("--qos", type=int, default=0, choices=[0, 1, 2], help="QoS (default: 0)")
    parser.add_argument("--keepalive", type=int, default=30, help="Keepalive seconds (default: 30)")
    parser.add_argument("--mqtt5", action="store_true", help="Use MQTT v5 (default: v3.1.1)")
    parser.add_argument("--username", help="Optional username (not needed for AWS IoT mTLS)")
    parser.add_argument("--password", help="Optional password")
    parser.add_argument("--insecure", action="store_true",
                        help="Disable TLS cert verification (debug only; NOT recommended)")
    parser.add_argument("--debug", action="store_true", help="Enable paho logger output")
    parser.add_argument("--timeout", type=int, default=10, help="Seconds to wait before exiting (publish-only)")
    args = parser.parse_args()

    if not args.subscribe and not args.publish:
        print("Error: provide at least --subscribe or --publish", file=sys.stderr)
        sys.exit(2)

    client = build_client(args)

    print(f"[connect] host={args.host} port={args.port} client_id={args.client_id} mqtt5={args.mqtt5}")
    try:
        client.connect(args.host, args.port, keepalive=args.keepalive)
    except ssl.SSLError as e:
        print(f"[TLS ERROR] {e}")
        print("This usually means CA/cert/key mismatch, wrong endpoint hostname, or cert verification failed.")
        sys.exit(1)
    except Exception as e:
        print(f"[CONNECT ERROR] {e}")
        sys.exit(1)

    # Start network loop
    client.loop_start()

    # Publish once if requested
    import json
    if args.publish:
        # Give connect a moment
        time.sleep(1)
        payload =  json.dumps({"sample":[{"protocol_id":"3517","set":[]}],"device_firmware":"1","device_id":"03:25:07:04","device_name":"AmbyteOnAir","device_version":"1","firmware_version":"1","timestamp":"2025-09-16T10:45:21.861Z"})
        # try:
        #     # If it's JSON text, normalize it (and ensure it's valid JSON)
        #     payload = json.dumps(args.message)
        #     print("all good, payload is:",payload)
        # except Exception as e:       
        #     # Not JSON; send raw string as-is
        #     print(e)
        #     payload = args.message

        info = client.publish(args.publish, payload=payload, qos=args.qos)
        # info = client.publish(args.publish, payload=args.message, qos=args.qos)
        info.wait_for_publish(timeout=5)
        print(f"[publish] topic={args.publish} qos={args.qos} rc={info.rc}")

        # If publish-only, wait a bit for callbacks then exit
        if not args.subscribe:
            time.sleep(args.timeout)
            client.loop_stop()
            client.disconnect()
            return

    # If subscribing, just run until Ctrl+C
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
    # Requires: pip install paho-mqtt
    main()