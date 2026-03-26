-- Embedded default script (runs from firmware image data)
status.log("Lua runner started")

while true do
    status.set_rgb(255, 0, 0)
    status.sleep_ms(250)
    status.set_rgb(0, 255, 0)
    status.sleep_ms(250)
    status.set_rgb(0, 0, 255)
    status.sleep_ms(250)
    status.set_rgb(0, 0, 0)
    status.sleep_ms(250)
end

