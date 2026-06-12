-- crashtest-main.lua — measurement-cadence benchmark (docs/measurement-flow-plan.md).
--
--   PURPOSE: prove whether the Lua measurement loop holds its schedule once
--   `lua_runner` is pinned to core 1 (APP_CPU), away from Wi-Fi@23 / lwIP@18 on
--   core 0. This is the deliberately HARSH schedule (MPF every 1 min, not 5)
--   where the pre-pin firmware lost cadence ("MPF can't keep its minute").
--
--   NOT a production schedule. Copy to /sdcard/main.lua only for the benchmark,
--   then restore the real exampleMain.lua afterwards.
--
--   REQUIRES ≥1 AMBIT connected and responding. With no AMBIT, run_trace only
--   pings (≈0 ms) and MPF trivially "holds" cadence — the test is meaningless.
--
--   WHAT TO WATCH (serial): lines prefixed "BENCH". The MPF line is the verdict —
--     BENCH MPF #N  gap=NNNNNms (target 60000, slip +-NNms)  wall=NNNNms
--   gap = ms since the previous MPF start. PASS: gap stays ≈60000 (slip within a
--   few hundred ms) for every cycle, no minute skipped (a skip shows gap≈120000).
--   wall = how long MPF actually took; if wall approaches 60000 the minute is at
--   risk regardless of preemption (that's the axis-2 serialization, not the pin).
--   SS gap jitter (it skips its :10/:20 marks while MPF runs) is EXPECTED — the
--   single Lua task serializes SS behind MPF; the core-pin does not change that.

local NUM_CHANNELS = 4                              -- AMBIT channels to scan (0..N-1)

-- ── Protocols (identical to exampleMain so the workload is realistic) ───────
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

-- ── Parallel run tuning (identical to exampleMain) ──────────────────────────
local POLL_INTERVAL_MS    = 500     -- gap between poll sweeps
local POLL_START_FRAC     = 0.9     -- don't poll a channel until 90% of its estimate elapsed
local DEADLINE_MARGIN_MS  = 15000   -- est + this without a result ⇒ ambit considered broken
local SEG_OVERHEAD_MS     = 300     -- per-segment config/light-sleep slack in the estimate

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
                end
            end
        end
    end

    device.measurement_window(false)
end

-- ── Cadence instrumentation ─────────────────────────────────────────────────
-- Each job logs the gap since its own previous start (the cadence the scheduler
-- actually delivered) and its wall-time. "slip" = gap - target; near 0 is good,
-- ≈+target means a whole interval was skipped.
local function timed(tag, target_ms, body)
    local last, n = nil, 0
    return function()
        local t0  = device.uptime_ms()
        local gap = last and (t0 - last) or 0
        last = t0
        n = n + 1
        body()
        local wall = device.uptime_ms() - t0
        device.log(string.format(
            "BENCH %-3s #%d  gap=%dms (target %d, slip %+dms)  wall=%dms",
            tag, n, gap, target_ms, (n > 1) and (gap - target_ms) or 0, wall))
    end
end

local ss_round   = timed("SS",  10000, function() run_trace("SS",   SS)  end)
local mpf_round  = timed("MPF", 60000, function() run_trace("MPF",  MPF) end)
local edge_round = function() run_trace("edge", MPF) end

-- ── Boot banner ───────────────────────────────────────────────────────────
do
    local sr, ss = sync.sun_today()
    device.log(string.format("CRASHTEST schedule started; sunrise=%s sunset=%s", sr, ss))
    device.log("CRASHTEST: SS@10s + MPF@1min (no day gate) — watch 'BENCH MPF' gap vs 60000")
end

-- ── Schedule ──────────────────────────────────────────────────────────────
-- Day gate REMOVED vs exampleMain so MPF runs at any hour (the preemption
-- behaviour under test is day/night-independent; this makes the bench runnable
-- whenever). Edge traces kept for parity but they only fire at the sun events,
-- so they won't appear during a short bench window.
sched.every(10,   ss_round)                          -- steady-state probe, every 10 s
sched.every("1m", mpf_round)                         -- saturating flash, EVERY MINUTE (the stress)
sched.sun("sunset",   30 * 60, edge_round)           -- dark-edge trace, 30 min after sunset
sched.sun("sunrise", -30 * 60, edge_round)           -- dark-edge trace, 30 min before sunrise

sched.run()                                          -- blocking merge loop
