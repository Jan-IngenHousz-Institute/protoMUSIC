#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "device_commands.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "lua_runner.h"

#define LUA_RUNNER_TAG "lua_runner"
#define LUA_RUNNER_TASK_NAME "lua_runner"
#define LUA_RUNNER_TASK_STACK 8192
#define LUA_RUNNER_TASK_PRIORITY 5
#define LUA_QUERY_MAX_RECORDS 64

extern const uint8_t g_lua_script[];
extern const size_t g_lua_script_len;

static TaskHandle_t s_lua_task_handle = NULL;
static const uint8_t *const s_lua_script_start = g_lua_script;

/* ── helpers ─────────────────────────────────────────────────────────── */

static int lua_push_nil_reason(lua_State *L, const char *reason)
{
    lua_pushnil(L);
    lua_pushstring(L, (reason != NULL) ? reason : "unknown error");
    return 2;
}

/* ── device.* bindings ───────────────────────────────────────────────── */

static int l_device_set_rgb(lua_State *L)
{
    lua_Integer r = luaL_checkinteger(L, 1);
    lua_Integer g = luaL_checkinteger(L, 2);
    lua_Integer b = luaL_checkinteger(L, 3);

    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        return luaL_error(L, "RGB values must be in [0, 255]");
    }

    cmd_result_t res = cmd_set_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    if (res.status != ESP_OK) {
        return luaL_error(L, "%s", res.message);
    }
    return 0;
}

static int l_device_read_rtc(lua_State *L)
{
    time_t t = 0;
    cmd_result_t res = cmd_read_rtc(&t);
    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }
    lua_pushinteger(L, (lua_Integer)t);
    return 1;
}

static int l_device_status(lua_State *L)
{
    bool bme_ready = false;
    bool rtc_ready = false;
    time_t rtc_time = 0;
    cmd_result_t res = cmd_device_status(&bme_ready, &rtc_ready, &rtc_time);

    lua_newtable(L);
    lua_pushboolean(L, bme_ready);
    lua_setfield(L, -2, "bme280");
    lua_pushboolean(L, rtc_ready);
    lua_setfield(L, -2, "rtc");
    if (rtc_ready) {
        lua_pushinteger(L, (lua_Integer)rtc_time);
        lua_setfield(L, -2, "rtc_time");
    }
    lua_pushstring(L, res.message);
    lua_setfield(L, -2, "summary");
    return 1;
}

static int l_device_read_env(lua_State *L)
{
    float temp = 0, hum = 0, pres = 0;
    cmd_result_t res = cmd_read_env(&temp, &hum, &pres);
    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }

    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)temp);
    lua_setfield(L, -2, "temperature_c");
    lua_pushnumber(L, (lua_Number)hum);
    lua_setfield(L, -2, "humidity_pct");
    lua_pushnumber(L, (lua_Number)pres);
    lua_setfield(L, -2, "pressure_pa");
    return 1;
}

static int l_device_sleep_ms(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0 || ms > INT_MAX) {
        return luaL_error(L, "ms must be in [0, INT_MAX]");
    }
    cmd_sleep_ms((uint32_t)ms);
    return 0;
}

static int l_device_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    cmd_log(msg);
    return 0;
}

/* ── db.* bindings ───────────────────────────────────────────────────── */

static int l_db_store(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int n = (int)luaL_len(L, 1);
    if (n <= 0) {
        return lua_push_nil_reason(L, "records array is empty");
    }

    measurement_record_t *records = malloc((size_t)n * sizeof(measurement_record_t));
    if (records == NULL) {
        return lua_push_nil_reason(L, "out of memory");
    }

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        if (!lua_istable(L, -1)) {
            free(records);
            return lua_push_nil_reason(L, "each record must be a table");
        }
        measurement_record_t *r = &records[i - 1];
        memset(r, 0, sizeof(*r));

        lua_getfield(L, -1, "sensor_id");
        r->sensor_id = (int64_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "measure_id");
        r->measure_id = (int64_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "measure_type");
        const char *mt = luaL_checkstring(L, -1);
        strncpy(r->measure_type, mt, sizeof(r->measure_type) - 1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "timestamp");
        r->timestamp = (time_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "data_type");
        const char *dt = luaL_checkstring(L, -1);
        strncpy(r->data_type, dt, sizeof(r->data_type) - 1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "value");
        r->value = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);

        r->sync_state = MEASUREMENT_SYNC_PENDING;
        lua_pop(L, 1); /* pop the record table */
    }

    cmd_result_t res = cmd_store_measurement(records, (size_t)n);
    free(records);

    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static void lua_push_record_table(lua_State *L, const measurement_record_t *r)
{
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)r->sensor_id);
    lua_setfield(L, -2, "sensor_id");
    lua_pushinteger(L, (lua_Integer)r->measure_id);
    lua_setfield(L, -2, "measure_id");
    lua_pushstring(L, r->measure_type);
    lua_setfield(L, -2, "measure_type");
    lua_pushinteger(L, (lua_Integer)r->timestamp);
    lua_setfield(L, -2, "timestamp");
    lua_pushstring(L, r->data_type);
    lua_setfield(L, -2, "data_type");
    lua_pushnumber(L, (lua_Number)r->value);
    lua_setfield(L, -2, "value");
    lua_pushinteger(L, (lua_Integer)r->sync_state);
    lua_setfield(L, -2, "sync_state");
}

static int l_db_query(lua_State *L)
{
    const char *type = luaL_checkstring(L, 1);
    lua_Integer from = luaL_checkinteger(L, 2);
    lua_Integer to = luaL_checkinteger(L, 3);

    measurement_record_t *out = malloc(LUA_QUERY_MAX_RECORDS * sizeof(measurement_record_t));
    if (out == NULL) {
        return lua_push_nil_reason(L, "out of memory");
    }

    size_t count = 0;
    cmd_result_t res = cmd_query_measurements(type, (time_t)from, (time_t)to,
                                              out, LUA_QUERY_MAX_RECORDS, &count);
    if (res.status != ESP_OK) {
        free(out);
        return lua_push_nil_reason(L, res.message);
    }

    lua_newtable(L);
    for (size_t i = 0; i < count; i++) {
        lua_push_record_table(L, &out[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }

    free(out);
    return 1;
}

static int l_db_count(lua_State *L)
{
    const char *type = luaL_checkstring(L, 1);
    size_t count = 0;
    cmd_result_t res = cmd_measurement_count(type, &count);
    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }
    lua_pushinteger(L, (lua_Integer)count);
    return 1;
}

static int l_db_next_id(lua_State *L)
{
    int64_t id = 0;
    cmd_result_t res = cmd_next_measure_id(&id);
    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

static int l_db_unsynced(lua_State *L)
{
    const char *type = luaL_checkstring(L, 1);

    measurement_record_t *out = malloc(LUA_QUERY_MAX_RECORDS * sizeof(measurement_record_t));
    if (out == NULL) {
        return lua_push_nil_reason(L, "out of memory");
    }

    size_t count = 0;
    cmd_result_t res = cmd_query_unsynced(type, out, LUA_QUERY_MAX_RECORDS, &count);
    if (res.status != ESP_OK) {
        free(out);
        return lua_push_nil_reason(L, res.message);
    }

    lua_newtable(L);
    for (size_t i = 0; i < count; i++) {
        lua_push_record_table(L, &out[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }

    free(out);
    return 1;
}

/* ── mqtt.* bindings ─────────────────────────────────────────────────── */

static int l_mqtt_status(lua_State *L)
{
    cmd_result_t res = cmd_mqtt_status();
    /* ESP_ERR_NOT_SUPPORTED → not wired; "MQTT: connected" / "MQTT: disconnected" otherwise */
    lua_pushboolean(L, res.status == ESP_OK &&
                       strstr(res.message, "disconnected") == NULL);
    return 1;
}

static int l_mqtt_publish_measurement(lua_State *L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    cmd_result_t res = cmd_mqtt_publish_measurement((int64_t)id);
    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_mqtt_publish_unsynced(lua_State *L)
{
    const char *type = luaL_checkstring(L, 1);
    cmd_result_t res = cmd_mqtt_publish_unsynced(type);
    if (res.status == ESP_ERR_NOT_FOUND) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "no pending");
        return 2;
    }
    if (res.status != ESP_OK) {
        return lua_push_nil_reason(L, res.message);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* ── module registration ─────────────────────────────────────────────── */

static void lua_register_device_module(lua_State *L)
{
    static const luaL_Reg device_api[] = {
        {"set_rgb",   l_device_set_rgb},
        {"read_rtc",  l_device_read_rtc},
        {"status",    l_device_status},
        {"read_env",  l_device_read_env},
        {"sleep_ms",  l_device_sleep_ms},
        {"log",       l_device_log},
        {NULL, NULL},
    };

    luaL_newlib(L, device_api);
    lua_setglobal(L, "device");
}

static void lua_register_db_module(lua_State *L)
{
    static const luaL_Reg db_api[] = {
        {"store",    l_db_store},
        {"query",    l_db_query},
        {"count",    l_db_count},
        {"next_id",  l_db_next_id},
        {"unsynced", l_db_unsynced},
        {NULL, NULL},
    };

    luaL_newlib(L, db_api);
    lua_setglobal(L, "db");
}

static void lua_register_mqtt_module(lua_State *L)
{
    static const luaL_Reg mqtt_api[] = {
        {"status",              l_mqtt_status},
        {"publish_measurement", l_mqtt_publish_measurement},
        {"publish_unsynced",    l_mqtt_publish_unsynced},
        {NULL, NULL},
    };

    luaL_newlib(L, mqtt_api);
    lua_setglobal(L, "mqtt");
}

/* ── task ────────────────────────────────────────────────────────────── */

static void log_lua_error(lua_State *L, const char *phase)
{
    const char *msg = lua_tostring(L, -1);
    ESP_LOGE(LUA_RUNNER_TAG, "%s: %s", phase, msg ? msg : "unknown Lua error");
    lua_pop(L, 1);
}

static void lua_runner_task(void *arg)
{
    (void)arg;

    lua_State *L = luaL_newstate();
    if (L == NULL) {
        ESP_LOGE(LUA_RUNNER_TAG, "Failed to create Lua state");
        s_lua_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    luaL_openlibs(L);
    lua_register_device_module(L);
    lua_register_db_module(L);
    lua_register_mqtt_module(L);

    const char *script = (const char *)s_lua_script_start;
    size_t script_len = g_lua_script_len;
    if (script_len == 0 || luaL_loadbuffer(L, script, script_len, "lua_script.lua") != LUA_OK) {
        log_lua_error(L, "failed to load embedded script");
        lua_close(L);
        s_lua_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        log_lua_error(L, "lua_pcall failed");
    }

    ESP_LOGI(LUA_RUNNER_TAG, "Script finished, stack high water: %lu",
             (unsigned long)uxTaskGetStackHighWaterMark(NULL));

    lua_close(L);
    s_lua_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t lua_runner_start(void)
{
    if (s_lua_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t created = xTaskCreate(
        lua_runner_task,
        LUA_RUNNER_TASK_NAME,
        LUA_RUNNER_TASK_STACK,
        NULL,
        LUA_RUNNER_TASK_PRIORITY,
        &s_lua_task_handle
    );
    if (created != pdPASS) {
        s_lua_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
