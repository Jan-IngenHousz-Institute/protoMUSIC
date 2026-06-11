# Measurement-flow protection — plan

Goal: the Lua measurement schedule (the `sched.run()` loop driving AMBIT
runs over UART) should run **as uninterrupted as possible** by background
firmware tasks, so timing-sensitive UART transactions don't stall and the
schedule keeps its cadence.

## Benchmark — the "crash test" protocol

A demanding `main.lua` is the optimization target / regression test:

```lua
sched.every(10,   ss_round)                          -- SS probe every 10 s
sched.every("1m", mpf_round, { when = "day" })       -- multi-phase flash every 1 min
sched.sun("sunset",   30*60, edge_round)             -- dark-edge trace
sched.sun("sunrise", -30*60, edge_round)
```

Observed (2026-06-11): the **`mpf_round` cannot complete on its 1-minute
cadence** — the other tasks (the 10 s SS rounds + background work) eat enough
time that the minute slips. Use this protocol to measure and to validate every
change below.

## Two independent axes (don't conflate them)

1. **Background-task preemption (firmware).** The Lua task is timing-sensitive
   during a measurement (FSM UART handshake, 500 ms reads). Today `lua_runner`
   is priority 10 but **unpinned**, so it can land on core 0 where Wi-Fi
   (prio 23, pinned core 0) and lwIP (18) preempt it — and MQTT/TLS bursts
   there are CPU-heavy. This is the axis the firmware can fix.
2. **Lua single-task serialization (script).** `sched` runs every job in **one**
   Lua task, cooperatively. A ~15–20 s `mpf_round` blocks the SS rounds that
   come due meanwhile; they queue and push the next `mpf` late. Core-pinning
   does **not** fix this — it's the measurement *duration* + the catch-up
   policy of `sched`. Levers: longer `mpf` interval, skip-if-busy, trimming the
   poll latency, or a `sched` "coalesce/drop overruns" policy.

## Firmware levers (axis 1), highest-leverage first

1. **Pin `lua_runner` to core 1 (APP_CPU); keep comms on core 0.**
   `xTaskCreatePinnedToCore(..., 1)` for the Lua task. Single change, directly
   targets the preemption. *Caveat:* verify the UART driver/ISR and SD access
   behave (different buses from the radio, so no direct contention; the win is
   CPU isolation).

   **Affinity ≠ core reservation.** Pinning the task to core 1 doesn't idle
   core 1 when the measurement blocks (which is most of the time — UART waits,
   poll sleeps): the scheduler runs any *other task allowed on core 1* in those
   gaps. So core 1 slack is NOT wasted — but *who* may use it decides whether
   isolation holds:
   - Low-prio tasks (blinker/sd_logger/sd-monitor, ≤ prio 3) using the slack is
     harmless — the measurement (prio 10) preempts them instantly when it
     unblocks, negligible jitter.
   - High-prio comms (lwIP prio 18, MQTT/TLS) using the slack REINTRODUCES the
     problem: when the measurement unblocks it waits behind them. So the airtight
     form is to **confine lwIP/MQTT to core 0** (Wi-Fi already is), e.g. via
     `CONFIG_LWIP_TCPIP_TASK_AFFINITY=CPU0` + the esp-mqtt task core selection.
   - Pinning `lua_runner` to core 1 alone is the high-leverage first cut (it
     stops the scheduler from ever placing the measurement on core 0 with the
     radio); add the lwIP/MQTT core-0 pin only if jitter persists.
2. **Defer the STATUS heartbeat store while `measurement_window` is held.**
   sync_runner already defers *publishing* during a measurement; the heartbeat
   *store* currently runs regardless. With core-pinning it's on core 0 so it
   won't preempt core-1 measurement, but deferring it is still tidy (avoids the
   event_log mutex contending with a fetch's store).
3. **Reconsider the LED dark-window.** We chose "blink through measurements"
   earlier; for the dark-adapted sunset/sunrise edge traces the green/blue
   flash is optical contamination. A *measurement-quality* (not CPU) item:
   suppress the blink while `measurement_window` is held, at least for dark
   traces. Revisit now that flow/quality is the focus.

## Script levers (axis 2)

- The shipped example runs SS every 10 s **and** MPF every 1 min in one task.
  Realistic options: lengthen the MPF interval to fit its true duration; make
  `sched` drop/coalesce a job whose previous run is still pending instead of
  queuing it; reduce `POLL_INTERVAL_MS`/`POLL_START_FRAC` so fetches start
  sooner; trim per-segment `SEG_OVERHEAD_MS` once measured.
- Measure actual `mpf_round` wall-time on HW (instrument with `uptime_ms`
  around `run_trace`) before tuning — don't guess.

## Sequencing

Start with firmware lever 1 (core-pin) — smallest change, directly addresses
"uninterrupted by background tasks", and immediately testable with the crash
protocol already on the bench. Then re-measure: if cadence still slips, it's
axis 2 (script/`sched`), addressed separately. Lever 2 and the dark-window are
quick follow-ons.
