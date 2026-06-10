# OTA firmware update — staged plan

Host-triggered, MQTT-commanded OTA for the **ambyte** (ESP32-S3 gateway), and
later the **ambit** sensors (ESP32-C3) over UART. Companion to
[device-script-delivery.md](../device-script-delivery.md), which describes the
Lua-script push that shares the same inbound channel.

## Decisions already taken
- **Trigger transport:** MQTT command on a single inbound topic, `type`-dispatched
  (same channel serves `ota_update`, `script_update`, future commands).
- **Firmware source:** the trigger payload carries the **full download URL**
  (any GitHub release asset or `raw.githubusercontent.com/<repo>/<branch>/…`),
  so we can point at an arbitrary branch or tag without reflashing.
- **OTA mechanism:** application-level `esp_https_ota` into a dual-OTA partition
  layout, TLS validated by the Mozilla cert bundle, with rollback.

## Corrections folded in from the critical review
1. **The OTA must run in its own task**, not in the MQTT `MQTT_EVENT_DATA`
   callback (a multi-minute blocking download there stalls the MQTT event loop /
   keepalive). The callback only parses → validates → dedupes → signals.
2. **Retain stays ON; the handler is made idempotent** (not retain-off). Retain-off
   would mean an offline device never gets the trigger (AWS IoT does not reliably
   queue QoS-1 for disconnected sessions). Safety comes from a **version-aware,
   NVS-persisted dedupe**: ignore a trigger whose `id` was already applied OR whose
   target version already equals the running version. This is what stops a retained
   trigger from re-flashing on every reboot.
3. **`mark_app_valid` is gated on connectivity:** the new image only cancels
   rollback after it has reconnected to MQTT, so a connectivity-breaking update
   auto-rolls-back instead of stranding the device.
4. **`sha256` + `size` in the payload are integrity, not authenticity.** The real
   trust boundary is "who can publish to the device's AWS IoT topic." For branch
   builds the hash is a moving target — accept that friction or restrict OTA to
   tagged release assets. (True authenticity would need Secure Boot v2 + signed
   images — out of scope for now.)

---

## Stage 0 — TLS/heap feasibility spike  ⟵ *implemented, awaiting a run*

**Question it answers (make-or-break):** does an HTTPS firmware download from
GitHub work on this board, given `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=8192` and a
~17 KiB largest free block (no PSRAM)? AWS IoT's handshake fits in 8 KiB; GitHub
(Fastly CDN, different cert chain) is the unknown.

**What landed:**
- `components/ota_spike/` — runs the real `esp_https_ota` advanced API against a
  GitHub URL, logs the handshake result + the internal-RAM largest-block low-water
  through the whole download, then **aborts before finish** (never sets boot;
  there is no otadata partition yet anyway).
- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` + `_DEFAULT_FULL` added to `sdkconfig.defaults`.
- `-DSPIKE_OTA` / `-DSPIKE_OTA_URL` build flags in `platformio.ini` (commented).
- Spike hook in `app_main.c`, placed before MQTT/sensors start (maximal heap →
  a failure is a pure TLS-buffer verdict, not heap exhaustion).

**How to run:**
1. Uncomment the `SPIKE_OTA` `build_flags` block in `platformio.ini` and set
   `SPIKE_OTA_URL` to a real esp32s3 `firmware.bin` (release asset or raw branch path).
2. Delete `sdkconfig.esp32-s3-devkitm-1` so the cert-bundle change regenerates.
3. Build + flash, watch the serial log for `OTA SPIKE RESULT` / `VERDICT`.

**Gate / branches:**
- **Handshake OK + healthy heap floor** → proceed to Stage 1, GitHub is viable.
- **Handshake fails with heap still high** → 8 KiB record buffer too small for
  GitHub. Either raise `MBEDTLS_SSL_IN_CONTENT_LEN` (and re-confront the heap
  ceiling) or host the image behind the AWS channel we already trust.
- **Handshake OK but heap floor dangerously low** → OTA must quiesce Lua + MQTT
  publishing during download (Stage 3 already plans to).

---

## Stage 1 — Partition migration + rollback  *(prerequisite, forces one hand-reflash)*

Today's `partitions.csv` has `factory + ota_0` with **no `otadata` / `ota_1`** —
not an OTA-capable layout. Rework to `ota_0 + ota_1 + otadata` (drop `factory`).

- Keep `littlefs` and `storage` at their current offsets so a reflash that writes
  only bootloader + table + app (no `erase_flash`) **preserves field data**.
- Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (+ `…ROLLBACK_REASON` optional).
- **Bootstrap caveat:** you cannot OTA into a new layout — every already-fielded
  ambyte needs this one-time physical reflash. OTA only helps units flashed after
  the migration. (Also re-seeds NVS per the known PlatformIO reflash behaviour.)

**Deliverable:** new partition table + rollback config, validated by a manual
`esp_ota` round-trip (write the other slot, switch, mark valid).

---

## Stage 2 — Inbound MQTT command channel  ("publishing to a device")

The current `mqtt_client.c` is **publish-only** — no subscribe, no
`MQTT_EVENT_DATA` handler, no dispatcher. This stage builds that foundation, which
is shared by OTA (Stage 3) and script push (Stage 4). **Build and prove it with a
trivial command first, independent of OTA.**

**Scope:**
- Subscribe to `device/cmd/v1/{sensorType}/{sensorVersion}/{thingName}` on
  `MQTT_EVENT_CONNECTED` (QoS 1), retain ON.
- `MQTT_EVENT_DATA` handler that **reassembles multi-part payloads**
  (`current_data_offset` / `total_data_len`) before parsing — mandatory because
  esp-mqtt chunks payloads > ~1 KB (the inline-Lua script can be 128 KB).
- A small command router that parses JSON and dispatches on `type`.
- A first `type: "ping"` (or `"status"`) command that just publishes a reply on a
  status topic — proves the round trip end-to-end.
- **AWS IoT policy update** granting `iot:Subscribe` + `iot:Receive` on the new
  topic (an unauthorized subscribe can make AWS *drop the connection* → reconnect
  loop, so this is load-bearing, not cosmetic).
- Shared idempotency helper: persisted "last applied command `id`" in NVS.

**Deliverable:** the device receives a published command and acknowledges it on a
status topic. No OTA, no script logic yet.

---

## Stage 3 — ambyte self-OTA handler  *(depends on Stage 1 + 2, gated by Stage 0)*

Wire `type: "ota_update"`, `target: "ambyte"` to a dedicated OTA **task**:

```json
{ "type":"ota_update", "id":"ota-2026-06-10-1", "target":"ambyte",
  "url":"https://raw.githubusercontent.com/Ludo-lab/ambyte-iot-ludo/<branch>/firmware.bin",
  "sha256":"<hex>", "size":1356608 }
```

Flow: dedupe (id + running-version) → publish "accepted" status → quiesce Lua +
publishing → `esp_https_ota` from `url` via cert bundle (follows the 302) → verify
`sha256`/`size` → set boot → store id in NVS → reboot → **after MQTT reconnects**,
`esp_ota_mark_app_valid_cancel_rollback()` → publish "success" status (a download
or boot failure rolls back / reports "failed").

**Deliverable:** publish a trigger → device self-updates from GitHub and reports
the result; a bad/connectivity-breaking image rolls back automatically.

---

## Stage 4 — Lua `script_update` over the same channel  *(parallel to Stage 3)*

Reuse Stage 2's channel for `type: "script_update"` per
[device-script-delivery.md](../device-script-delivery.md): verify checksum, write
to SD/littlefs, restart `lua_runner`. Note the **firmware ↔ script compatibility
contract** — an OTA and a script push can each break the other if their API
versions drift.

---

## Stage 5 — ambit OTA over UART  *(later; separate workstream)*

Out of scope here. Download `.bin` to the ambyte SD → framed/CRC'd/ACK'd UART
chunk protocol → Arduino `Update` on the C3 (which already has dual-OTA partitions
and is USB-JTAG recoverable). A *different* handler from Stage 3; the `target`
field reserves the slot. See the OTA-plan memory and ambit-firmware-interop notes.
