# AMBIT-over-UART OTA — hardware test plan

Covers the four follow-ups added 2026-06-12: **MQTT trigger**, **C3 rollback**,
**per-channel rollout (`all`)**, and the **fleet version report**. Builds on the
already-passed end-to-end OTA (v0.0.4 → v0.0.5).

Repos: ambyte = `ambyte-iot-ludo` (this repo); C3 = `ambit-IoT/fw_new` (Arduino).
A bad C3 image is always recoverable over **USB-Serial-JTAG** — keep a USB cable handy.

---

## Prerequisites

1. **Flash the new ambyte build** (has the MQTT dispatch, rollback-confirm, `all`
   sweep, and version commands):
   ```powershell
   pio run -e esp32-s3-devkitm-1 -t upload
   pio device monitor -b 115200
   ```
2. **A hosting spot for C3 images** — commit each `firmware.bin` to a repo and use its
   **raw** URL (`https://raw.githubusercontent.com/<owner>/<repo>/<branch>/<path>`), the
   same way the ambyte image is hosted. Not a `/blob/` or `/tree/` page.
3. At least one AMBIT on a channel (tests assume **ch0**; the `all` test wants ≥2).

> **Rollback only arms for C3 images built from the current code** (they contain
> `verifyRollbackLater()` + cmd 29). The AMBIT is currently on an older image, so do
> **Test 0** first to get a rollback-capable image onto it.

### Build a C3 image (repeat per step, bumping the version so it's distinguishable)

In `ambit-IoT/fw_new/src/nvs1.h` bump `BATCH_VERSION`, then:
```powershell
pio run -d "C:\Users\LudovicoCaracciolo\Documents\Git-repo\ambit-IoT\fw_new" -e ambyte
```
The image is `%LOCALAPPDATA%\pio_build\ambit_fw_new\ambyte\firmware.bin`. Copy it to your
hosting repo path and push; that's the URL you pass to `ambit_ota`.

---

## Test 0 — baseline: rollback-capable receiver on the C3 (USB, one time)

Get a current-code image onto the AMBIT so later OTAs can roll back.

1. `BATCH_VERSION = 6`, build, and **USB-flash the C3 directly**:
   ```powershell
   pio run -d "C:\Users\LudovicoCaracciolo\Documents\Git-repo\ambit-IoT\fw_new" -e ambyte -t upload
   ```
2. On the ambyte console: `ambit_versions`
   - **PASS:** `AMBIT1: v0.0.6  (built …)`.

---

## Test 1 — MQTT trigger (+ dedupe)

1. Host a `BATCH_VERSION = 7` image; note its raw URL.
2. Publish to your **command topic** (the one `ota_update` uses) with your MQTT client:
   ```json
   {"type":"ambit_ota","id":"ambit-7-a","channel":0,"url":"https://raw.githubusercontent.com/.../firmware.bin"}
   ```
3. Watch the ambyte log.
   - **PASS:** `AMBIT OTA requested: ch=0 id=ambit-7-a …` → `streaming … 100%` →
     `OTA_END ok` → `fw after : v0.0.7` → `image confirmed — rollback cancelled` →
     `AMBIT OTA SUCCESS`. If MQTT is connected, an `ambit_ota_status` `success` message
     is published.
4. **Dedupe:** publish the **same** payload again (same `id`).
   - **PASS:** log shows `ambit_ota id=ambit-7-a already applied — ignoring` (no re-flash).
5. Re-publish with a **new** `id` (`ambit-7-b`, same url) → runs again (re-flashes v0.0.7).
   - **PASS:** runs to `SUCCESS` (proves a new id is not deduped).

---

## Test 2 — C3 rollback

### 2a — confirm + persistence (happy path)

After any successful OTA (e.g. Test 1), the new image was confirmed.
1. Power-cycle (or `reboot` over the C3's USB console) the AMBIT.
2. `ambit_versions` on the ambyte.
   - **PASS:** still `v0.0.7` — a confirmed image survives a reboot (no rollback).

### 2b — actual revert (the safety net)

Push a deliberately **unhealthy** image and confirm the C3 reverts.
1. Make a C3 image that boots but never services UART: in `ambit-1.ino` `setup()`, right
   after `Serial.begin(115200);` add a hang:
   ```cpp
   Serial.begin(115200);
   while (1) { delay(1000); }   // TEST 2b ONLY — boots but never answers commands
   ```
   Set `BATCH_VERSION = 8`, build, host it.
2. `ambit_ota 0 <url-to-broken-v0.0.8>` (or via MQTT).
   - Expect: `OTA_END ok — AMBIT1 rebooting` → after the wait + 3 retries,
     `AMBIT1 not answering after OTA — NOT confirming; it will roll back …` →
     `AMBIT OTA FAILED`. (The broken image is now running, **PENDING_VERIFY**, unconfirmed.)
3. **Power-cycle the AMBIT.** The bootloader sees the unconfirmed image and rolls back.
4. `ambit_versions`.
   - **PASS:** back to **v0.0.7** (the previous, confirmed image) — rollback worked.
5. Remove the `while(1)` line before any real build.

> A C3 image that *crashes* on boot rolls back automatically on the crash-reboot (no
> manual power-cycle needed); the `while(1)` variant hangs instead, so it needs the
> power-cycle in step 3. The C3 has no hardware watchdog.

---

## Test 3 — per-channel rollout (`all`)

Needs ≥2 AMBITs connected (e.g. ch0 + ch1).
1. Host a `BATCH_VERSION = 9` image.
2. `ambit_ota all <url>`  (CLI)  — or MQTT `{"type":"ambit_ota","id":"all-9","channel":"all","url":"…"}`.
3. Watch the log.
   - **PASS:** `AMBIT2: not present — skipping` for empty channels; each present channel
     runs `streaming…OTA_END…confirmed`; ends with `AMBIT OTA all: N/N present channels updated`.
4. `ambit_versions`.
   - **PASS:** every present channel reads `v0.0.9`.

---

## Test 4 — fleet version report

1. **CLI:** `ambit_versions`
   - **PASS:** one line per channel, e.g. `AMBIT1: v0.0.9  (built …)` / `AMBIT2: absent`.
2. **MQTT:** publish `{"type":"ambit_versions","id":"v1"}` to the command topic.
   - **PASS (log):** `AMBIT1: v0.0.9`, `AMBIT2: absent`, … (runs on the worker, not the
     MQTT task).
   - **PASS (MQTT):** an `ambit_versions` report is published to the status topic:
     `{"type":"ambit_versions","device_id":"…","id":"v1","channels":[{"ch":0,"present":true,"version":"0.0.9"},{"ch":1,"present":false},…]}`.

---

## Notes

- Each OTA suspends Lua + MQTT for the duration and resumes after; a normal measurement
  cycle should run on the channel afterward (regression check: `ambit_spec 0` / a Lua round).
- `ambit_ota_status` / `ambit_versions` MQTT messages only publish if the status topic is
  authorized (watch for `PUBACK rc=135`); the **console log is always authoritative**.
- Status reports are best-effort right after `comms_resume` — MQTT may not have reconnected
  yet when `success`/`failed` is sent, so rely on the log for the final verdict.
