#include <stddef.h>
#include <stdint.h>

const uint8_t g_lua_script[] =
"-- GPIO4 PWM demo: cycle duty 50 -> 100 -> 0 -> 50, five times\n"
"device.log(\"Lua runner started\")\n"
"\n"
"for i = 1, 5 do\n"
"    device.GPIO4_PWM(10000, 50, true)\n"
"    device.sleep_ms(2000)\n"
"    device.GPIO4_PWM(10000, 100, true)\n"
"    device.sleep_ms(2000)\n"
"    device.GPIO4_PWM(10000, 0, false)\n"
"    device.sleep_ms(2000)\n"
"    device.GPIO4_PWM(10000, 50, true)\n"
"    device.sleep_ms(2000)\n"
"    device.log(\"Cycle done\")\n"
"end\n";

const size_t g_lua_script_len = sizeof(g_lua_script) - 1;
