-- main.lua — measurement schedule for the Ambyte (runs as /sdcard/main.lua).
--
-- EXECUTION MODEL
--   The firmware bundles a scheduler (`sched`) and exposes five globals:
--     device.*  on-board I/O      (LED, BME280, RTC, MP2731 power)
--     ambit.*   everything AMBIT  (ping/spec/leaf_temp/run/trigger/poll/fetch
--                                  + config/diagnostics; channel is always arg 1)
--     uart.*    raw serial        (uart.query for not-yet-drivered sensors)
--     db.*      the event log     (db.store_event for CUSTOM events, db.next_id)
--     sync.*    RTC-based timing   (sunrise/sunset, intervals, wait)
--   This file registers jobs with `sched`, then calls sched.run() (never returns).
--
--   MEASUREMENT COMMANDS STORE THEMSELVES. ambit.spec / ambit.leaf_temp /
--   ambit.run / ambit.fetch / device.bme280 persist their result as one event
--   by default (pass {store=false} to probe without storing); the firmware
--   stamps the provenance (channel, device, cmd_raw, tag, ticks).
--   db.store_event{ data=, metadata=, channel= } is for derived/custom events
--   only. Raw transport (uart.query, ambit.query) never stores.
--
--   STORE, DON'T PUBLISH. Lua never talks to MQTT. A background task
--   (sync_runner) is the sole publisher: it drains stored events to the cloud
--   when, and only when, the device is on external power (the "publish power
--   gate" — VIN present). On battery, events stay in the log and drain the next
--   time the panel/USB brings power back. Nothing is lost, only delayed; the
--   measurement time is preserved in each event's startTicks.
--
--   GC is handled by sched.run() between cycles — jobs may build large transient
--   tables (AMBIT arrays) freely.
--
-- ACTINIC CONVENTION (segment .actinic field):
--   negative = raw DAC |value|   positive = PAR µmol (via AMBIT calibration)   0 = off

local NUM_CHANNELS = 4                              -- AMBIT channels to scan (0..N-1)

-- ── Protocols (segment tables passed to ambit.run) ────────────────────────
local SS = {                                        -- steady-state probe (1 line)
    { pulses = 4, freq = 4, actinic = 0 },
}

local MPF = {                                       -- multi-phase saturating flash
    { pulses = 40, freq = 10,  actinic = 0    },    -- dark baseline
    { pulses = 70, freq = 100, actinic = -250 },    -- saturating phases (raw DAC)
    { pulses = 10, freq = 100, actinic = -200 },
    { pulses = 10, freq = 100, actinic = -160 },
    { pulses = 10, freq = 100, actinic = -250 },
    { pulses = 40, freq = 10,  actinic = 0    },    -- relaxation
}

-- ── Helpers ───────────────────────────────────────────────────────────────

-- Brief green blink: visible "still alive" sign.
local function blink()
    device.set_rgb(0, 100, 0); device.sleep_ms(100); device.set_rgb(0, 0, 0)
end

-- One spectrum + PAR per connected AMBIT. ambit.spec stores its own event
-- (channel/device/cmd_raw="get_par" stamped by the firmware).
local function record_spectra()
    local stored = 0
    for ch = 0, NUM_CHANNELS - 1 do
        if ambit.ping(ch) then
            local s, err = ambit.spec(ch)
            if s then
                stored = stored + (s.id and 1 or 0)
                device.log(string.format("spectra ch%d: PAR=%.2f%s",
                           ch, s.par, s.id and (" id=" .. s.id) or " store failed"))
            else
                device.log(string.format("spectra ch%d: read failed: %s", ch, err or "?"))
            end
        end
    end
    if stored == 0 then device.log("spectra: no AMBIT responded") end
end

-- ── Parallel run tuning ─────────────────────────────────────────────────────
local POLL_INTERVAL_MS    = 500     -- gap between poll sweeps
local POLL_START_FRAC     = 0.9     -- don't poll a channel until 90% of its estimate elapsed
local DEADLINE_MARGIN_MS  = 15000   -- est + this without a result ⇒ ambit considered broken
local SEG_OVERHEAD_MS     = 300     -- per-segment config/light-sleep slack in the estimate

-- Approximate a trace's run time (ms): each segment ≈ pulses/freq seconds, plus a
-- fixed per-segment overhead. Used to schedule polling and bound a broken ambit.
local function estimate_ms(trace)
    local total = 0
    for _, seg in ipairs(trace) do
        local pulses = seg.pulses or seg[1] or 0
        local freq   = seg.freq   or seg[2] or 1
        if freq < 1 then freq = 1 end
        total = total + (pulses / freq) * 1000 + SEG_OVERHEAD_MS
    end
    return math.floor(total)
end

-- Trigger the trace on every connected AMBIT back-to-back, then poll and fetch
-- each as it finishes. The 4 ambits measure concurrently, so wall-time ≈ the
-- slowest run plus the (serialized) result fetches, instead of the sum of four.
-- store=true persists each run as an event; sync_runner publishes it later when
-- the power gate is open. Lua never publishes. The measurement window is held
-- across the whole cycle so the publisher can't race a fetch on the tight heap.
local function run_trace(tag, trace)
    device.measurement_window(true)

    local est     = estimate_ms(trace)
    local pending = {}                 -- ch -> t0 (uptime_ms at trigger)
    local count   = 0
    for ch = 0, NUM_CHANNELS - 1 do
        if ambit.ping(ch) then
            local ok, err = ambit.trigger(ch, trace, { interrupt = false })
            if ok then
                pending[ch] = device.uptime_ms()
                count = count + 1
            else
                device.log(string.format("%s ch%d: trigger failed: %s", tag, ch, err or "?"))
            end
        end
    end

    -- Poll + fetch loop. Stay off a channel's wire until ~90% of its estimate has
    -- elapsed (a poll's wake bytes reach a measuring sensor). Only "done"/"error"
    -- are terminal; a silent channel past the deadline is treated as broken.
    while count > 0 do
        device.sleep_ms(POLL_INTERVAL_MS)
        local now = device.uptime_ms()
        for ch = 0, NUM_CHANNELS - 1 do
            local t0 = pending[ch]
            if t0 then
                local elapsed = now - t0
                if elapsed >= est * POLL_START_FRAC then
                    local st = ambit.poll(ch)
                    if st == "done" then
                        local r, err = ambit.fetch(ch, { store = true })
                        if r then
                            device.log(string.format("%s ch%d: %d points, %.1fC, stored %d",
                                       tag, ch, r.points, r.leaf_temp or 0, r.stored or 0))
                        else
                            device.log(string.format("%s ch%d: fetch failed: %s", tag, ch, err or "?"))
                        end
                        pending[ch] = nil; count = count - 1
                    elseif st == "error" then
                        device.log(string.format("%s ch%d: ambit reported run error", tag, ch))
                        pending[ch] = nil; count = count - 1
                    elseif elapsed > est + DEADLINE_MARGIN_MS then
                        device.log(string.format("%s ch%d: no result after %dms — ambit broken?",
                                   tag, ch, elapsed))
                        pending[ch] = nil; count = count - 1
                    end
                    -- else "busy"/"idle": keep waiting
                end
            end
        end
    end

    device.measurement_window(false)
end

-- Periodic device status, stored as a custom event (identified downstream by
-- its data keys until the Phase-4 firmware heartbeat takes over with
-- tag=STATUS). db.store_event stamps the capture time in startTicks;
-- sync_runner publishes it under the same power gate as measurements (so on
-- battery these queue and flush once external power returns).
local function status_heartbeat()
    local s = device.status_report()
    if s then
        db.store_event{ data = s }
        device.log(string.format("status: src=%s gate=%s Vbat=%.2fV Iin=%dmA",
                   s.input_present and "external" or "battery",
                   s.publish_gate and "OPEN" or "CLOSED",
                   s.battery_v or 0, s.input_ma or 0))
    end
end

-- ── Job bodies ────────────────────────────────────────────────────────────
local function ss_round()   run_trace("SS",   SS)  end
local function mpf_round()  run_trace("MPF",  MPF) end
local function edge_round() run_trace("edge", MPF) end

-- ── Boot banner ───────────────────────────────────────────────────────────
do
    local sr, ss = sync.sun_today()
    device.log(string.format("schedule started; sunrise=%s sunset=%s", sr, ss))
end

-- ── Schedule ──────────────────────────────────────────────────────────────
-- sched.every(period, fn [, {when="day"|"night"}])  -- clock-aligned interval
-- sched.cron({minutes}, fn [, opts])                -- at minutes-of-hour
-- sched.sun("sunrise"|"sunset", offset_s, fn)       -- once/day relative to sun
sched.every(10,   ss_round)                          -- steady-state probe, every 10 s
sched.every("1m", record_spectra, { when = "day" })  -- spectrum + PAR, daytime
sched.every("5m", mpf_round,      { when = "day" })  -- saturating flash, daytime
sched.sun("sunset",   30 * 60, edge_round)           -- dark-edge trace, 30 min after sunset
sched.sun("sunrise", -30 * 60, edge_round)           -- dark-edge trace, 30 min before sunrise
sched.every("5m", status_heartbeat)                  -- status event (day & night)
sched.every(3,    blink)                             -- liveness blink

sched.run()                                          -- blocking merge loop
