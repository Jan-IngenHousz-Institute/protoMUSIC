# MQTT payload reference

What the firmware actually publishes, as implemented. The single envelope
builder is `cmd_mqtt_publish_next_event()` in
[components/device_commands/device_commands.c](../components/device_commands/device_commands.c)
— if this document and the code disagree, the code wins.

> **This is the v1 schema.** A redesign (schema v2: firmware-filled
> provenance, `tag`/`channel`/`cmd_raw`, measurement-time envelope timestamp)
> is planned in [payload-v2-plan.md](payload-v2-plan.md); this document gets
> rewritten when it lands.

## Model: store, then publish

Lua scripts never talk to MQTT. They *measure and store* events
(`db.store_event`, `ambit.run{store=true}`) into the append-log event store;
a background task (`sync_runner`) is the sole publisher and drains the store
when **all** of these hold:

- MQTT is connected,
- no UART measurement is in progress (measurement-activity gate),
- the publish power gate is open (external power present — on battery,
  events queue and flush when power returns).

One stored event (one `measure_id`) becomes exactly one MQTT message.
Events publish in store order (FIFO).

## Topic

```
<topic_root>/1234
```

- `topic_root` comes from NVS (`AMBYTE_TOPIC_ROOT`, e.g.
  `experiment/data_ingest/v1/<experiment-uuid>/multispeq/v1.0/AMBYTE{MAC}`;
  the `{MAC}` token is expanded at boot to the board's STA MAC).
- The `/1234` suffix is a hardcoded literal (`device_commands.c`,
  `cmd_mqtt_publish_next_event`).
- The AWS IoT policy must permit publishing to the resulting topic; policies
  typically pin topics to strings containing the thing name.

## Envelope

Built as a direct `snprintf` string splice — the stored `data`/`metadata`
JSON is inserted verbatim, no cJSON round-trip (heap reasons). Annotated
example:

```jsonc
{
  "sample": [{                          // always exactly ONE element
    "measure_id": 1000042,              // int64, unique per device (see below)
    "startTicks": 1765459200123,        // epoch ms, measurement start
    "endTicks":   1765459210456,        // epoch ms, measurement end
    "device":  "ambit",                 // string, or null when stored without one
    "sensor":  "AMBIT_1",               // required at store time
    "metadata": { "segments": [ ... ] },// object, or null when absent
    "data":     { ... }                 // object — the measurement quantities
  }],
  "device_firmware":  "1",              // AMBYTE_DEVICE_FIRMWARE (NVS)
  "device_id":        "10:00:3B:72:22:44", // STA MAC — NOT AMBYTE_DEVICE_ID
  "device_name":      "AmbyteOnAir",    // AMBYTE_DEVICE_NAME (NVS)
  "device_version":   "1",              // AMBYTE_DEVICE_VERSION (NVS)
  "firmware_version": "1",              // AMBYTE_FIRMWARE_VERSION (NVS)
  "timestamp": "2026-06-11T09:30:00Z"   // UTC ISO-8601 at PUBLISH time
}
```

Field notes:

- `sample` is an array purely for compatibility with the cloud's
  `sample:[…]` ingestion contract; the firmware always sends one element.
- `startTicks`/`endTicks` are captured at **measurement** time
  (`db.store_event` defaults them to "now" when the event is stored);
  the top-level `timestamp` is stamped at **publish** time. On battery
  these can differ by hours or days — use the ticks for measurement time.
- `measure_id` is a plain monotonic `int64` from the event log (starts at 1,
  reseeded above the highest id found on the SD card at boot). It is unique
  **per device only** — combine with `device_id` for a global key.
- The identity strings are provisioned via `.env` → NVS (see
  [.env.example](../.env.example)); empty strings appear when unprovisioned.

## Event types (what fills `sensor` / `device` / `metadata` / `data`)

`data` is whatever the Lua script stores; the shapes below are what the
current firmware paths and the shipped schedule script produce.

| Event | `sensor` | `device` | `metadata` | `data` |
|---|---|---|---|---|
| AMBIT fluorescence run (`ambit.run`, `ambit.trigger`+`fetch`) | `AMBIT_<ch+1>` | `ambit` | `{"segments":[{"pulses":N,"freq":N,"actinic":N},…]}` | `{"env":[…],"s_fluo":[…],"r_fluo":[…],"sun":[…],"leaf":[…],"s_730":[…],"r_730":[…],"timing":[…]}` |
| Spectrum + PAR (`device.ambit_get_spec`) | `AMBIT` | `AMBIT<ch+1>` | `null` | `{"spec":[…],"par":N}` |
| Status heartbeat (`device.status_report`) | `status` | `null` | `null` | `{"wifi":bool,"provisioned":bool,"db_online":bool,"publish_gate":bool,"battery_v":num,"input_v":num,"system_v":num,"input_ma":int,"charge_ma":int,"input_present":bool,"charge_status":int}` |
| BME280 (`device.bme280` stored via Lua) | script-defined | `null` | `null` | `{"temperature_c":num,"humidity_pct":num,"pressure_pa":num}` |
| UART text query (`save=true`) | `uart_ch<N>` | `null` | `null` | `{"response":"…"}` — empty string means "queried, no reply" |

### AMBIT `data` keys

Array index → JSON key, fixed in `ambit_array_tag()`
([components/lua_runner/lua_runner.c](../components/lua_runner/lua_runner.c)),
matching the AMBIT firmware's send order:

| idx | key | content |
|---|---|---|
| 0 | `env` | leaf temperature, °C (decoded, 2 decimals) |
| 1 | `s_fluo` | fluorescence signal (raw uint32) |
| 2 | `r_fluo` | fluorescence reference |
| 3 | `sun` | sun sensor |
| 4 | `leaf` | leaf sensor |
| 5 | `s_730` | 730 nm signal |
| 6 | `r_730` | 730 nm reference |
| 7 | `timing` | `[tick_begin, tick_end]` µs |

Unknown indices fall back to `"arr<idx>"`. All values except `env` are raw
uint32 counts.

## Delivery semantics

- **QoS 1, retain 0**, one message in flight at a time.
- PUBACK marks the event `SYNCED`; a publish failure or MQTT disconnect
  re-marks it `PENDING` for retry. Delivery is therefore **at-least-once**
  — the cloud must dedupe on (`device_id`, `measure_id`).
- Payloads larger than 128 KiB (the AWS IoT maximum) are still attempted
  but logged as likely to be rejected. The AMBIT run payload buffer is
  capped at 8 KiB (`AMBIT_RUN_PAYLOAD_CAP`); larger runs truncate with a
  warning and are not stored.

## Inbound messages

The device subscribes to `AMBYTE_COMMAND_TOPIC` and replies on
`AMBYTE_STATUS_TOPIC` (see [.env.example](../.env.example)). The command
payload format (inline Lua script + SHA-256 checksum, optional gzip+base64)
is documented in [device-script-delivery.md](../device-script-delivery.md).

## Caveat: host test client

The dummy payload sent by
[docs/mqtt_tls_test_client.py](mqtt_tls_test_client.py) `--publish`
(`{"sample":[{"protocol_id":…,"set":[]}]}`) is a connectivity smoke test
only — it does **not** match the firmware's envelope above.
