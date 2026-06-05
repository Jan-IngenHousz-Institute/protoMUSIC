-- sync.lua — RTC-based measurement scheduling for the ambyte (pure Lua, self-contained).
--
-- Copy to the SD card next to main.lua and load it with:
--     local sync = dofile("/sdcard/sync.lua")
--
-- Time model
-- ----------
-- Every function works in "RTC time" = whatever your PCF2131 RTC holds, which
-- is your LOCAL wall clock (e.g. CEST). Clock/interval/weekday schedules use the
-- RTC value directly — no timezone math. The ONLY timezone-aware part is
-- sunrise/sunset: those are computed in UTC from lat/lon and shifted by sync.TZ
-- (hours the RTC is AHEAD of UTC) so they line up with the RTC.
--
-- Needs only: device.read_rtc() (accurate hardware RTC, in seconds) and
-- device.sleep_ms(). No firmware changes.

local sync = {}

-- ── configuration (override after loading, e.g. sync.LAT = 51.0) ─────────
sync.LAT = 52.173     -- degrees north
sync.LON = 5.819      -- degrees east (positive = east)
sync.TZ  = 2          -- hours the RTC/local clock is AHEAD of UTC.
                      --   CEST (NL summer) = 2, CET (NL winter) = 1, UTC = 0.
                      --   Used ONLY to localise sunrise/sunset.

local floor = math.floor
local sin, cos, asin, acos = math.sin, math.cos, math.asin, math.acos
local DEG = math.pi / 180

-- ── calendar helpers (Hinnant civil algorithms; no os.date/TZ dependency) ─
local function days_from_civil(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = floor((y >= 0 and y or y - 399) / 400)
  local yoe = y - era * 400
  local mp  = (m > 2) and (m - 3) or (m + 9)
  local doy = floor((153 * mp + 2) / 5) + d - 1
  local doe = yoe * 365 + floor(yoe / 4) - floor(yoe / 100) + doy
  return era * 146097 + doe - 719468           -- days since 1970-01-01
end

local function civil_from_days(z)
  z = z + 719468
  local era = floor((z >= 0 and z or z - 146096) / 146097)
  local doe = z - era * 146097
  local yoe = floor((doe - floor(doe/1460) + floor(doe/36524) - floor(doe/146096)) / 365)
  local y   = yoe + era * 400
  local doy = doe - (365 * yoe + floor(yoe/4) - floor(yoe/100))
  local mp  = floor((5 * doy + 2) / 153)
  local d   = doy - floor((153 * mp + 2) / 5) + 1
  local m   = (mp < 10) and (mp + 3) or (mp - 9)
  if m <= 2 then y = y + 1 end
  return y, m, d
end

-- break an RTC unix time into local calendar parts
local function parts(unix)
  local days = floor(unix / 86400)
  local rem  = unix - days * 86400
  local y, m, d = civil_from_days(days)
  return {
    year = y, month = m, day = d,
    hour = floor(rem / 3600),
    min  = floor((rem % 3600) / 60),
    sec  = rem % 60,
    wday = (days % 7 + 4) % 7 + 1,             -- 1=Sun .. 7=Sat
  }
end

local function unix_of(y, m, d, hour, min, sec)
  return days_from_civil(y, m, d) * 86400
       + (hour or 0) * 3600 + (min or 0) * 60 + (sec or 0)
end

-- ── block until `seconds` from now, polling the hardware RTC (drift-free) ─
function sync.wait(seconds)
  local target = device.read_rtc() + seconds
  while true do
    local remaining = target - device.read_rtc()
    if remaining <= 0 then return end
    device.sleep_ms((remaining < 30 and remaining or 30) * 1000)  -- chunked + interruptible
  end
end

-- ── 1) clock-aligned interval: 600 = every 10 min on :00/:10/…, 1800 = :00/:30 ─
--    `phase` (optional) shifts the grid, e.g. until_interval(3600, 300) = HH:05.
function sync.until_interval(period, phase)
  phase = phase or 0
  local now = device.read_rtc()
  local n = floor((now - phase) / period) + 1
  return (n * period + phase) - now
end

-- ── 2) next HH:MM[:SS] in RTC (local) time ───────────────────────────────
function sync.until_clock(hour, min, sec)
  local now = device.read_rtc()
  local t = parts(now)
  local target = unix_of(t.year, t.month, t.day, hour, min, sec or 0)
  if target <= now then target = target + 86400 end
  return target - now
end

-- ── 3) next matching weekday at HH:MM. days: list of 1=Sun..7=Sat or names ─
--    e.g. {"mon","tue"} or {2,3}.
local WNAME = { sun=1, mon=2, tue=3, wed=4, thu=5, fri=6, sat=7 }
function sync.until_weekly(days, hour, min)
  local want = {}
  for _, d in ipairs(days) do
    want[type(d) == "string" and WNAME[d:sub(1,3):lower()] or d] = true
  end
  local now = device.read_rtc()
  for ahead = 0, 7 do
    local t = parts(now + ahead * 86400)
    if want[t.wday] then
      local target = unix_of(t.year, t.month, t.day, hour, min, 0)
      if target > now then return target - now end
    end
  end
end

-- ── solar: sunrise/sunset for an RTC-local date, returned in RTC time ─────
-- NOAA sunrise equation. Returns unix (RTC/local) of the event, or nil for
-- polar day/night.
local function sun_on(y, m, d, event)  -- event: "sunrise" | "sunset"
  local lw  = -sync.LON                       -- west longitude positive
  local phi = sync.LAT
  local Jd0 = days_from_civil(y, m, d) + 2440587.5
  local n   = floor(Jd0 - 2451545.0 - 0.0009 - lw / 360 + 0.5)
  local J   = 2451545.0 + 0.0009 + lw / 360 + n
  local M   = (357.5291 + 0.98560028 * (J - 2451545.0)) % 360
  local C   = 1.9148 * sin(M*DEG) + 0.0200 * sin(2*M*DEG) + 0.0003 * sin(3*M*DEG)
  local lam = (M + C + 180 + 102.9372) % 360
  local Jtr = J + 0.0053 * sin(M*DEG) - 0.0069 * sin(2*lam*DEG)
  local dec = asin(sin(lam*DEG) * sin(23.44*DEG)) / DEG
  local cosw = (sin(-0.833*DEG) - sin(phi*DEG) * sin(dec*DEG)) / (cos(phi*DEG) * cos(dec*DEG))
  if cosw < -1 or cosw > 1 then return nil end           -- polar day / night
  local w0  = acos(cosw) / DEG
  local Jev = (event == "sunset") and (Jtr + w0 / 360) or (Jtr - w0 / 360)
  local unix_utc = (Jev - 2440587.5) * 86400
  return floor(unix_utc + sync.TZ * 3600 + 0.5)          -- → RTC (local) time
end

-- ── 4) x seconds before/after sunrise|sunset. offset signed: -1800 = 30 min before ─
function sync.until_sun(event, offset)
  offset = offset or 0
  local now = device.read_rtc()
  for ahead = 0, 2 do
    local t = parts(now + ahead * 86400)
    local ev = sun_on(t.year, t.month, t.day, event)
    if ev then
      local target = ev + offset
      if target > now then return target - now end
    end
  end
end

-- ── debugging: today's sunrise/sunset as "HH:MM" in RTC time ─────────────
function sync.sun_today()
  local t = parts(device.read_rtc())
  local function hm(u)
    if not u then return "--:--" end
    local p = parts(u); return string.format("%02d:%02d", p.hour, p.min)
  end
  return hm(sun_on(t.year, t.month, t.day, "sunrise")),
         hm(sun_on(t.year, t.month, t.day, "sunset"))
end

return sync
