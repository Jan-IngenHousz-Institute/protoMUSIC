#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ambyte_status.h"
#include "lauxlib.h"
#include "lua.h"
#include "lua_runner.h"
#include "pcf2131tfy_rtc_api.h"
#include "sd_card.h"

#define LUA_RUNNER_TAG "lua_runner"
#define LUA_RUNNER_TASK_NAME "lua_runner"
#define LUA_RUNNER_TASK_STACK 4096
#define LUA_RUNNER_TASK_PRIORITY 5

extern const uint8_t g_lua_script[];
extern const size_t g_lua_script_len;

static TaskHandle_t s_lua_task_handle = NULL;
static const uint8_t *const s_lua_script_start = g_lua_script;

static int lua_push_nil_reason(lua_State *L, const char *reason)
{
    lua_pushnil(L);
    lua_pushstring(L, (reason != NULL) ? reason : "unknown error");
    return 2;
}

static const char *lua_err_detail(esp_err_t err)
{
    switch (err) {
        case ESP_ERR_INVALID_ARG:
            return "invalid argument or path";
        case ESP_ERR_INVALID_SIZE:
            return "path too long";
        case ESP_ERR_INVALID_STATE:
            return "service not ready";
        case ESP_ERR_TIMEOUT:
            return "operation timed out";
        case ESP_ERR_NOT_FOUND:
            return "not found";
        default:
            return esp_err_to_name(err);
    }
}

static int lua_push_esp_err_reason(lua_State *L, const char *context, esp_err_t err)
{
    char reason[96];
    snprintf(reason, sizeof(reason), "%s: %s", context, lua_err_detail(err));
    return lua_push_nil_reason(L, reason);
}

static bool lua_string_has_embedded_nul(const char *value, size_t value_len)
{
    return (value == NULL) || (strlen(value) != value_len);
}

static esp_err_t lua_sd_make_default_name(char *out_name, size_t out_name_len)
{
    if ((out_name == NULL) || (out_name_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pcf2131tfy_rtc_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t now = 0;
    esp_err_t err = pcf2131tfy_rtc_get_time(&now);
    if (err != ESP_OK) {
        return err;
    }

    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        return ESP_FAIL;
    }

    const size_t written = strftime(out_name, out_name_len, "%Y-%m-%d_%H_%M_%S", &tm_now);
    return (written > 0) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static int l_status_set_rgb(lua_State *L)
{
    if (lua_gettop(L) != 3) {
        return luaL_error(L, "status.set_rgb expects 3 arguments: r, g, b");
    }

    lua_Integer r = luaL_checkinteger(L, 1);
    lua_Integer g = luaL_checkinteger(L, 2);
    lua_Integer b = luaL_checkinteger(L, 3);

    if (r < 0 || r > UINT8_MAX) {
        return luaL_error(L, "r must be in [0, 255]");
    }
    if (g < 0 || g > UINT8_MAX) {
        return luaL_error(L, "g must be in [0, 255]");
    }
    if (b < 0 || b > UINT8_MAX) {
        return luaL_error(L, "b must be in [0, 255]");
    }

    esp_err_t err = ambyte_status_set_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    if (err != ESP_OK) {
        return luaL_error(L, "status.set_rgb failed: %s", esp_err_to_name(err));
    }
    return 0;
}

static int l_status_sleep_ms(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0 || ms > INT_MAX) {
        return luaL_error(L, "ms must be in [0, INT_MAX]");
    }

    vTaskDelay(pdMS_TO_TICKS((TickType_t)ms));
    return 0;
}

static int l_status_log(lua_State *L)
{
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "status.log expects exactly 1 string argument");
    }

    size_t len = 0;
    const char *msg = luaL_checklstring(L, 1, &len);
    if (msg == NULL || len == 0) {
        return luaL_error(L, "status.log expects a non-empty string");
    }

    ESP_LOGI(LUA_RUNNER_TAG, "%s", msg);
    return 0;
}

static int l_status_stop(lua_State *L)
{
    (void)L;
    return luaL_error(L, "status.stop() requested");
}

static int l_status_hello(lua_State *L)
{
    lua_pushstring(L, "hello world");
    return 1;
}

static int l_sd_create_log_file(lua_State *L)
{
    const int argc = lua_gettop(L);
    if (argc > 1) {
        return lua_push_nil_reason(L, "sd.create_log_file expects zero or one string argument");
    }

    char generated_name[32];
    const char *filename = NULL;

    if (argc == 1) {
        if (lua_type(L, 1) != LUA_TSTRING) {
            return lua_push_nil_reason(L, "sd.create_log_file expects a string filename");
        }

        size_t filename_len = 0;
        filename = lua_tolstring(L, 1, &filename_len);
        if ((filename_len == 0) || lua_string_has_embedded_nul(filename, filename_len)) {
            return lua_push_nil_reason(L, "sd.create_log_file filename must be a non-empty text string");
        }
    } else {
        const esp_err_t err = lua_sd_make_default_name(generated_name, sizeof(generated_name));
        if (err == ESP_ERR_INVALID_STATE) {
            return lua_push_nil_reason(L, "rtc not ready");
        }
        if (err != ESP_OK) {
            return lua_push_esp_err_reason(L, "sd.create_log_file failed to build timestamp name", err);
        }
        filename = generated_name;
    }

    char resolved_path[SD_CARD_PATH_MAX];
    const esp_err_t err = sdcard_ensure_file(filename, resolved_path, sizeof(resolved_path));
    if (err != ESP_OK) {
        return lua_push_esp_err_reason(L, "sd.create_log_file failed", err);
    }

    lua_pushstring(L, resolved_path);
    return 1;
}

static int l_sd_append(lua_State *L)
{
    if (lua_gettop(L) != 2) {
        return lua_push_nil_reason(L, "sd.append expects exactly 2 string arguments");
    }

    if (lua_type(L, 1) != LUA_TSTRING) {
        return lua_push_nil_reason(L, "sd.append filename must be a string");
    }
    if (lua_type(L, 2) != LUA_TSTRING) {
        return lua_push_nil_reason(L, "sd.append text must be a string");
    }

    size_t filename_len = 0;
    const char *filename = lua_tolstring(L, 1, &filename_len);
    if ((filename_len == 0) || lua_string_has_embedded_nul(filename, filename_len)) {
        return lua_push_nil_reason(L, "sd.append filename must be a non-empty text string");
    }

    size_t text_len = 0;
    const char *text = lua_tolstring(L, 2, &text_len);
    if (lua_string_has_embedded_nul(text, text_len)) {
        return lua_push_nil_reason(L, "sd.append text must not contain embedded NUL bytes");
    }

    const esp_err_t err = sdcard_append_line_exact(filename, text);
    if (err != ESP_OK) {
        return lua_push_esp_err_reason(L, "sd.append failed", err);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static void lua_register_status_module(lua_State *L)
{
    static const luaL_Reg status_api[] = {
        {"set_rgb", l_status_set_rgb},
        {"sleep_ms", l_status_sleep_ms},
        {"log", l_status_log},
        {"stop", l_status_stop},
        {"hello", l_status_hello},
        {NULL, NULL},
    };

    luaL_newlib(L, status_api);
    lua_setglobal(L, "status");
}

static void lua_register_sd_module(lua_State *L)
{
    static const luaL_Reg sd_api[] = {
        {"create_log_file", l_sd_create_log_file},
        {"append", l_sd_append},
        {NULL, NULL},
    };

    luaL_newlib(L, sd_api);
    lua_setglobal(L, "sd");
}

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

    lua_register_status_module(L);
    lua_register_sd_module(L);

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
