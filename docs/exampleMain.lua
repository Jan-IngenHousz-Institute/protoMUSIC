-- main.lua — measurement schedule for the Ambyte (runs as /sdcard/main.lua).
--
-- EXECUTION MODEL
--   The firmware bundles a scheduler (`sched`) and exposes four globals:
--     device.*  on-board I/O      (LED, BME280, RTC, MP2731 power, AMBIT helpers)
--     ambit.*   AMBIT sensor runs (binary protocol over UART)
--     db.*      the event log     (db.store_event, db.next_id)
--     sync.*    RTC-based timing   (sunrise/sunset, intervals, wait)
--   This file registers jobs with `sched`, then calls sched.run() (never returns).
--
--   STORE, DON'T PUBLISH. Lua only ever *measures and stores* (db.store_event /
--   ambit.run{store=true}). It never talks to MQTT. A background task
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

-- One spectrum + PAR per connected AMBIT, each stored as its own event.
local function record_spectra()
    local stored = 0
    for ch = 0, NUM_CHANNELS - 1 do
        if device.uart_ping(ch) then
            local s, err = device.ambit_get_spec(ch)
            if s then
                local id = db.store_event{
                    sensor = "AMBIT",
                    device = string.format("AMBIT%d", ch + 1),
                    data   = { spec = s.spec, par = s.par },
                }
                stored = stored + (id and 1 or 0)
                device.log(string.format("spectra ch%d: PAR=%.2f%s",
                           ch, s.par, id and (" id=" .. id) or " store failed"))
            else
                device.log(string.format("spectra ch%d: read failed: %s", ch, err or "?"))
            end
        end
    end
    if stored == 0 then device.log("spectra: no AMBIT responded") end
end

-- Run a trace on every connected AMBIT. store=true persists each run as an event;
-- sync_runner publishes it later when the power gate is open. Lua never publishes.
local function run_trace(tag, trace)
    local ran = 0
    for ch = 0, NUM_CHANNELS - 1 do
        if device.uart_ping(ch) then
            local r, err = ambit.run(ch, trace, { store = true, timeout_ms = 30000 })
            if r then
                ran = ran + 1
                device.log(string.format("%s ch%d: %d points, %.1fC, stored %d",
                           tag, ch, r.points, r.leaf_temp or 0, r.stored or 0))
            else
                device.log(string.format("%s ch%d: run failed: %s", tag, ch, err or "?"))
            end
        end
    end
    if ran == 0 then device.log(tag .. ": no AMBIT responded") end
end

-- Periodic device status, stored as a sensor="status" event. db.store_event
-- stamps it with the capture time in startTicks; sync_runner publishes it under
-- the same power gate as measurements (so on battery these queue and flush once
-- external power returns). Also logged for live visibility on the console.
local function status_heartbeat()
    local s = device.status_report()
    if s then
        db.store_event{ sensor = "status", data = s }
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
