#include <stddef.h>
#include <stdint.h>

const uint8_t g_lua_script[] =
"-- Embedded default script (runs from firmware image data)\n"
"status.log(\"Lua runner started\")\n"
"\n"
"local log_path, log_err = sd.create_log_file()\n"
"if log_path then\n"
"    status.log(\"SD log file ready: \" .. log_path)\n"
"else\n"
"    status.log(\"WARNING: SD log file was not created: \" .. (log_err or \"unknown error\"))\n"
"end\n"
"\n"
"for i=1,5 do\n"
"    status.set_rgb(50, 0, 0)\n"
"    status.sleep_ms(10)\n"
"    status.set_rgb(0, 50, 0)\n"
"    status.sleep_ms(10)\n"
"    status.set_rgb(0, 0, 50)\n"
"    status.sleep_ms(10)\n"
"    status.set_rgb(0, 0, 0)\n"
"    local mystring = status.hello()\n"
"    status.log(\"Lua iteration \" .. i .. \", status.hello() returned: \" .. mystring)\n"
"    if log_path then\n"
"        local ok, append_err = sd.append(log_path, \"Lua iteration \" .. i .. \", status.hello() returned: \" .. mystring)\n"
"        if ok then\n"
"            status.log(\"sd.append succeeded for iteration \" .. i)\n"
"        else\n"
"            status.log(\"sd.append failed: \" .. append_err)\n"
"        end\n"
"    end\n"
"    status.sleep_ms(2000)\n"
"end\n";

const size_t g_lua_script_len = sizeof(g_lua_script) - 1;
