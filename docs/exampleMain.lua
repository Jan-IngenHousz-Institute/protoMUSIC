-- main.lua — measurement schedule (Lua, sched-driven).
-- actinic convention: negative = raw DAC |value|; positive = PAR µmol
-- (converted via the AMBIT calibration); 0 = no actinic light.

local NUM_CHANNELS = 4

-- ── Protocols (segment tables) ───────────────────────────────────────────
local SS = {                                      -- steady-state probe, 1 line
    { pulses = 4, freq = 4, actinic = 0 },
}

local MPF = {                                     -- multi-phase saturating flash
    { pulses = 40, freq = 10,  actinic = 0    },  -- dark baseline
    { pulses = 70, freq = 100, actinic = -250 },  -- saturating phases (raw DAC)
    { pulses = 10, freq = 100, actinic = -200 },
    { pulses = 10, freq = 100, actinic = -160 },
    { pulses = 10, freq = 100, actinic = -250 },
    { pulses = 40, freq = 10,  actinic = 0    },  -- relaxation
}

-- ── Helpers ──────────────────────────────────────────────────────────────
local function heartbeat()
    device.set_rgb(0, 100, 0); device.sleep_ms(100); device.set_rgb(0, 0, 0)
end

local function record_spectra()                   -- spectrum + PAR per AMBIT
    local stored = 0
    for ch = 0, NUM_CHANNELS - 1 do
        if device.uart_ping(ch) then
            local s, err = device.ambit_get_spec(ch)
            if s then
                local id = db.store_event{
                    sensor = "AMBIT", device = string.format("AMBIT%d", ch + 1),
                    data = { spec = s.spec, par = s.par },
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

local function run_trace(tag, trace)              -- a trace on every connected AMBIT
    local ran = 0
    for ch = 0, NUM_CHANNELS - 1 do
        if device.uart_ping(ch) then
            -- store=true persists the run; the sync_runner publishes it when the
            -- power gate is open. Lua never publishes directly.
            local r, err = ambit.run(ch, trace, { store = true, timeout_ms = 30000 })
            if r then
                ran = ran + 1
                device.log(string.format("%s ch%d: %d points, stored %d",
                           tag, ch, r.points, r.stored or 0))
            else
                device.log(string.format("%s ch%d: run failed: %s", tag, ch, err or "?"))
            end
        end
    end
    if ran == 0 then device.log(tag .. ": no AMBIT responded") end
end

-- ── Job bodies ───────────────────────────────────────────────────────────
local function ss_round()   run_trace("SS",   SS)  end
local function mpf_round()  run_trace("MPF",  MPF) end
local function edge_round() run_trace("edge", MPF) end

-- Status heartbeat: store the current device state (Wi-Fi, DB, power, source/
-- charge/publish-gate) as a sensor="status" event. db.store_event stamps it with
-- the capture time in startTicks, and the sync_runner publishes it when the
-- power gate is open — i.e. on external power, alongside the measurement backlog.
local function status_round()
    local s = device.status_report()
    if s then
        db.store_event{ sensor = "status", data = s }
        device.log(string.format("status: src=%s gate=%s Vbat=%.2f Iin=%d",
                   s.input_present and "external" or "battery",
                   s.publish_gate and "OPEN" or "CLOSED",
                   s.battery_v or 0, s.input_ma or 0))
    end
end

do
    local sr, ss = sync.sun_today()
    device.log(string.format("schedule started; sunrise=%s sunset=%s", sr, ss))
end

-- ── Schedule ─────────────────────────────────────────────────────────────
sched.every(10,   ss_round)                         -- SS every 10 s
sched.every("1m", record_spectra, { when = "day" }) -- spectrum, daytime
sched.every("5m", mpf_round,      { when = "day" }) -- MPF, daytime
sched.sun("sunset",   30 * 60, edge_round)          -- dark-edge trace
sched.sun("sunrise", -30 * 60, edge_round)
sched.every("5m", status_round)                     -- status heartbeat (log + MQTT)
sched.every(3, heartbeat)                           -- liveness blink

sched.run()
