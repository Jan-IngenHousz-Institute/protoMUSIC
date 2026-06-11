# OTA firmware update — staged plan

Host-triggered OTA for the **ambyte** (ESP32-S3 gateway), and later the
**ambit** sensors (ESP32-C3) over UART. Companion to
[device-script-delivery.md](../device-script-delivery.md), which describes the
Lua-script push that shares the same inbound channel.

> **Revised 2026-06-11:** the trigger/orchestration layer moves from a custom
> MQTT command topic to **AWS IoT Jobs** (the mechanism underneath AWS's OTA
> update tasks). The download path is unchanged (Stage-0-proven
> `esp_https_ota`). Stages 2–4 are rewritten; Stage 0 (passed) and Stage 1
> are untouched. The custom-channel code already built for Stage 2
> (subscribe + reassembly in `mqtt_client.c`, JSON router + NVS dedupe in
> `command_router/`) is repointed, not discarded.

## Decisions already taken

- **Trigger transport: AWS IoT Jobs** on the reserved
  `$aws/things/<thingName>/jobs/*` topics, over the existing esp-mqtt
  connection. A job's document carries the same payload the custom trigger
  would have (`type`-dispatched: `ota_update`, `script_update`, future
  commands).
- **Firmware source:** the job document carries the **full download URL**
  (any GitHub release asset or `raw.githubusercontent.com/<repo>/<branch>/…`),
  so we can point at an arbitrary branch or tag without reflashing. If the
  platform team prefers S3 hosting, job documents support
  `${aws:iot:s3-presigned-url:…}` placeholders — a drop-in swap later.
- **OTA mechanism:** application-level `esp_https_ota` into a dual-OTA
  partition layout, TLS validated by the Mozilla cert bundle, with rollback.

## Why Jobs instead of the custom trigger

1. **It dissolves the Stage-2 policy blocker.** The custom command topic is
   dead in the water because openJII's subscribe policy is scoped by a Cognito
   identity variable that is *empty* for cert-authenticated devices (the
   SUBACK 135 failure). Jobs topics are granted with the standard
   `${iot:Connection.Thing.ThingName}` thing-policy variable — the documented
   pattern for cert-auth fleets. Precondition (already true in our setup,
   enforced by their policy anyway): the device connects with **client ID =
   thing name** and the cert attached to the thing.
2. **The retain/idempotency hack becomes unnecessary.** Jobs have a real
   per-device execution lifecycle (QUEUED → IN_PROGRESS → SUCCEEDED/FAILED/
   TIMED_OUT). An offline device picks its QUEUED job up on reconnect — the
   problem retain-ON was solving — and an applied job never re-fires, the
   problem the NVS last-id dedupe was solving. The NVS latch survives in a
   smaller role (reboot correlation, below).
3. **Fleet visibility for free:** per-device status, statusDetails, timeouts
   (`inProgressTimeoutInMinutes` → auto-TIMED_OUT watchdog), thing-group
   targeting and staged rollout/abort configs in the AWS console — all things
   the custom status topic would have reimplemented poorly.

## Why NOT the full AWS OTA Update service (CreateOTAUpdate + OTA agent)

Assessed and deferred, not rejected on principle:

- The official OTA agent (`ota-for-aws-iot-embedded-sdk` via `esp-aws-iot`)
  is built on **coreMQTT/coreHTTP**, not esp-mqtt. Adopting it means either a
  full MQTT-stack migration or a second TLS session — the latter is
  heap-prohibitive on this board (~17 KB steady-state largest block), the
  former a rework far larger than OTA itself. AWS's own current guidance has
  shifted toward exactly the Jobs-based custom flow we're adopting.
- `CreateOTAUpdate` adds AWS Signer **code signing** — real authenticity, and
  the honest answer to correction #4 below. It layers cleanly on top of a
  Jobs-based device flow later (verify the signature file referenced by the
  job document before `esp_ota_set_boot_partition`). Park it with Secure Boot
  v2 as the hardening path.

## Corrections carried over from the original critical review

1. **The OTA must run in its own task**, not in the MQTT event callback (a
   multi-minute blocking download there stalls the MQTT event loop /
   keepalive). The callback only parses → validates → signals.
2. ~~Retain + NVS-dedupe idempotency~~ — superseded by the job execution
   lifecycle (see above). The NVS jobId latch remains for **reboot
   correlation only**: it tells the freshly-booted image which execution to
   report on.
3. **`mark_app_valid` is gated on connectivity:** the new image only cancels
   rollback after it has reconnected to MQTT — at which point it also reports
   the job SUCCEEDED. A connectivity-breaking update rolls back instead of
   stranding the device, and the rolled-back image reports the job FAILED.
4. **`sha256` + `size` in the job document are integrity, not authenticity.**
   The trust boundary becomes "who can create jobs in the openJII AWS
   account" (IAM) — strictly better than "who can publish to a topic", but
   still not signed images. True authenticity = AWS Signer and/or Secure
   Boot v2, out of scope for now.

---

## Stage 0 — TLS/heap feasibility spike  ⟵ *PASSED 2026-06-10 on hardware*

**Question it answered (make-or-break):** does an HTTPS firmware download from
GitHub work on this board? **YES.** A full 1.36 MB esp32s3 image downloaded from a
public GitHub release (`protoMUSIC/releases/ota-spike-test`) end-to-end:
- TLS handshake validated against **both** github.com AND the Fastly CDN across the
  302 redirect (cert bundle), `complete_image = YES`, ~53 KiB/s.
- **Heap floor 131 KB largest-internal, flat** the entire download (measured in the
  quiesced pre-MQTT state Stage 3 will reproduce) — heap is a non-issue.

**Proven production settings (carry into the Stage-3 handler / config):**
1. `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` + `_DEFAULT_FULL=y` — validates github.com
   + the CDN host.
2. `esp_http_client_config`: **`buffer_size=4096`, `buffer_size_tx=4096`** — the 512 B
   defaults overflow on GitHub's long signed-redirect URL + verbose CDN headers
   (`HTTP_CLIENT: Out of buffer`).
3. **`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`** (raised back from 8192) — S3/Fastly
   stream the body in full 16 KiB TLS records; with `DYNAMIC_BUFFER` on, a record
   over the cap fails on the first data read with `-0x7100` (BAD_INPUT_DATA). Safe
   to raise because `DYNAMIC_BUFFER` makes it a *transient per-record ceiling*: MQTT's
   small records still alloc small. **Caveat to verify:** this is now in
   `sdkconfig.defaults` so it affects NORMAL builds too — confirm steady-state MQTT is
   unaffected (watch the `sync_runner` "heap:" line) after reflashing normal firmware.

**What landed:** `components/ota_spike/` (re-runnable test harness),
cert-bundle config in `sdkconfig.defaults`, `-DSPIKE_OTA` build flags in
`platformio.ini` (commented), spike hook in `app_main.c`.

**Gate: PASSED → GitHub-hosted OTA is viable; proceed to Stage 1.**

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

## Stage 2 — Jobs channel  *(rework of the built custom channel)*

Repoint the existing inbound infrastructure (connect-time subscribe +
multi-part reassembly in `mqtt_client.c`; JSON router in `command_router/`)
from the custom command topic to the Jobs reserved topics. Hand-rolled on
esp-mqtt + cJSON — the AWS Jobs "library" is mostly topic-string helpers; we
don't need it.

**Device flow:**
- On `MQTT_EVENT_CONNECTED`: subscribe (QoS 1) to
  `$aws/things/<tn>/jobs/notify-next`,
  `$aws/things/<tn>/jobs/start-next/accepted` + `/rejected`,
  `$aws/things/<tn>/jobs/+/update/accepted` + `/rejected`;
  then publish `{}` to `$aws/things/<tn>/jobs/start-next` to claim any QUEUED
  execution (this is how offline-queued jobs are picked up — no retain).
- `start-next/accepted` carries the **execution + job document**; the router
  dispatches on the document's `type` exactly as designed for the custom
  channel. Claiming via start-next marks it IN_PROGRESS server-side.
- Replies become `UpdateJobExecution` publishes
  (`$aws/things/<tn>/jobs/<jobId>/update` with `status` + `statusDetails`)
  instead of the custom status topic.
- Reassembly stays (esp-mqtt chunks inbound > ~1 KB; job documents are small
  but multi-KB). **Job documents are capped at 32 KB** — see Stage 4 for the
  consequence on script delivery.
- `handle_ping` survives as a no-op job type (`{"type":"ping"}` → report
  SUCCEEDED with uptime/fw in `statusDetails`) — the Stage-2 end-to-end proof.

**Policy ask to the platform team** (replaces the Cognito-policy fix):
`iot:Subscribe`/`iot:Receive` on
`$aws/things/${iot:Connection.Thing.ThingName}/jobs/*` topic filters and
`iot:Publish` on the matching topics. Standard AWS pattern, resolves for
cert auth because client ID = thing name. Plus: whoever triggers updates
needs IAM `iot:CreateJob` (+ console or a small boto3 script — replaces
`docs/stage2_command_test.py`, which becomes a job-creation/monitor tool).

**Cleanup:** `AMBYTE_COMMAND_TOPIC` / `AMBYTE_STATUS_TOPIC` env keys and their
NVS plumbing go away (jobs topics derive from the thing name = client ID).

**Deliverable:** create a `ping` job in the console → device claims it,
reports SUCCEEDED with statusDetails visible in the console. No OTA yet.

---

## Stage 3 — ambyte self-OTA handler  *(depends on Stage 1 + 2, gated by Stage 0)*

Wire `type: "ota_update"`, `target: "ambyte"` to a dedicated OTA **task**.
Job document (jobId replaces the old `id` field):

```json
{ "type":"ota_update", "target":"ambyte", "version":"1.4.0",
  "url":"https://raw.githubusercontent.com/Ludo-lab/ambyte-iot-ludo/<branch>/firmware.bin",
  "sha256":"<hex>", "size":1356608 }
```

Flow: claim via start-next → skip-if-current (`version` == running → report
SUCCEEDED with `statusDetails: {"skipped":"already at version"}`) → report
IN_PROGRESS detail "downloading" → quiesce Lua + publishing →
`esp_https_ota` from `url` via cert bundle (follows the 302) → verify
`sha256`/`size` → set boot → **latch jobId + target version in NVS** →
reboot → new image boots, MQTT reconnects →
`esp_ota_mark_app_valid_cancel_rollback()` → report the latched jobId
SUCCEEDED → clear latch. Failure paths: download/verify error → report
FAILED with reason, no reboot; connectivity-breaking image → bootloader
rolls back → old image finds the latch with version ≠ running → reports
FAILED `{"rolled_back":true}`. Set `inProgressTimeoutInMinutes` (~30) on the
job as the server-side watchdog for bricked-silent outcomes.

**Deliverable:** create an OTA job → device self-updates from GitHub and the
job shows SUCCEEDED in the console; a bad/connectivity-breaking image rolls
back and the job shows FAILED with the rollback detail.

---

## Stage 3b — image authenticity via AWS Signer  *(hardening; layers on Stage 3)*

This is the part of "the dedicated AWS IoT OTA feature" worth adopting without
the full OTA-agent stack migration. Stage 3 gives integrity (`sha256`/`size`)
but not **authenticity** — the trust boundary is only "who can create jobs in
the AWS account" (IAM). AWS Signer closes that with a real signature over the
image, and it grafts onto the **existing Jobs flow** rather than requiring
coreMQTT/coreHTTP or a second TLS session (the reason the full managed OTA
service is deferred — see the "Why NOT" note above).

**Why it fits the Jobs flow:** `CreateOTAUpdate` already produces a code-signed
artifact + a signature, and the job document can carry the signature (or a URL
to it) alongside the image URL. The device just adds one verify step; the
transport stays esp_https_ota over the existing connection.

**Scope:**
- Sign the firmware with **AWS Signer** (or an offline ECDSA key) at release
  time; publish the signature next to the `firmware.bin`.
- Provision the **public key** on the device (NVS or embedded in the app) —
  one-time, like the cert bundle.
- Job document gains `sig` (+ `sig_alg`). The Stage-3 handler verifies the
  downloaded image against the public key **before** `esp_ota_set_boot_partition`
  — using `esp_https_ota`'s post-download hook or a streaming hash + an mbedTLS
  ECDSA verify (mbedTLS is already linked for TLS, so no new heap stack).
- A bad/unsigned/altered image is rejected and the job reports FAILED
  `{"reason":"signature"}` — never boots.

**Relation to Secure Boot v2:** Signer verifies *who built it* (app-level,
software trust). Secure Boot v2 verifies *what the ROM will boot* (hardware
root of trust, efuse-burned). They're complementary; Signer is the cheaper
first step and needs no efuse changes. Full hardware authenticity = Secure
Boot v2, still a separate, irreversible (efuse) decision.

**Deliverable:** an image signed by the release pipeline updates normally; a
tampered or unsigned image is refused before boot and the job shows FAILED with
a signature reason. No coreMQTT migration, no second TLS session.

---

## Stage 4 — Lua `script_update` as a job  *(parallel to Stage 3)*

`type: "script_update"` per
[device-script-delivery.md](../device-script-delivery.md), with one
structural change: the 32 KB job-document cap means the **inline-script
variant dies** (scripts can be 128 KB). The document carries a `url` +
`checksum` instead, and the device fetches the script over HTTPS exactly like
firmware — which also deletes the only consumer of >32 KB MQTT reassembly.
Verify checksum → write to SD/littlefs → restart `lua_runner` → report
SUCCEEDED. `device-script-delivery.md` needs a matching revision when this
lands. Note the **firmware ↔ script compatibility contract** — an OTA and a
script push can each break the other if their API versions drift.

---

## Stage 5 — ambit OTA over UART  *(later; separate workstream)*

Out of scope here. Download `.bin` to the ambyte SD → framed/CRC'd/ACK'd UART
chunk protocol → Arduino `Update` on the C3 (which already has dual-OTA partitions
and is USB-JTAG recoverable). Same jobs channel, `target: "ambit"` in the job
document reserves the slot; long-running executions suit jobs well
(IN_PROGRESS statusDetails per chunk phase, generous timeout). See the
OTA-plan memory and ambit-firmware-interop notes.
