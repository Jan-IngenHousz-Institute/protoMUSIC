#include "command_router.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "ota_update.h"

#define TAG "cmd_router"

#define NVS_NS          "cmd_router"
#define NVS_KEY_LAST_ID "last_id"

static command_router_config_t s_cfg;   /* pointers reference app_main's static buffers */

/* ── idempotency: persisted last-applied command id ─────────────────────────
 * Survives the reboot an OTA causes, so a retained trigger is applied exactly
 * once. ping is NOT deduped (a reply is harmless and wanted every time). */

static bool already_applied(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char prev[64] = {0};
    size_t len = sizeof(prev);
    esp_err_t err = nvs_get_str(h, NVS_KEY_LAST_ID, prev, &len);
    nvs_close(h);
    return (err == ESP_OK && strcmp(prev, id) == 0);
}

/* Recorded when an ota_update is dispatched, so a retained/duplicate trigger
 * can't re-apply it (at-least-once idempotency). */
static void mark_applied(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_str(h, NVS_KEY_LAST_ID, id) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

static void publish_reply(const char *json)
{
    if (s_cfg.publish == NULL || s_cfg.status_topic == NULL || s_cfg.status_topic[0] == '\0') {
        return;
    }
    int msg_id = 0;
    s_cfg.publish(s_cfg.status_topic, json, strlen(json), &msg_id);
}

static void handle_ping(const char *id)
{
    char reply[256];
    const long long up_ms = (long long)(esp_timer_get_time() / 1000);
    snprintf(reply, sizeof(reply),
             "{\"type\":\"pong\",\"id\":\"%s\",\"device_id\":\"%s\",\"fw\":\"%s\",\"uptime_ms\":%lld}",
             id ? id : "",
             s_cfg.device_id ? s_cfg.device_id : "",
             s_cfg.firmware_version ? s_cfg.firmware_version : "",
             up_ms);
    ESP_LOGI(TAG, "ping -> pong (id=%s)", id ? id : "");
    publish_reply(reply);
}

/* message_received_fn — runs in the mqtt task. Keep light; hand long work (OTA) to
 * a separate task in Stage 3. */
static void on_message(const char *topic, const char *payload, size_t len, void *ctx)
{
    (void)topic;
    (void)len;
    (void)ctx;

    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) {
        ESP_LOGW(TAG, "command JSON parse failed");
        return;
    }

    const cJSON *jtype = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *jid   = cJSON_GetObjectItemCaseSensitive(root, "id");
    const char *type = cJSON_IsString(jtype) ? jtype->valuestring : NULL;
    const char *id   = cJSON_IsString(jid)   ? jid->valuestring   : NULL;

    if (type == NULL) {
        ESP_LOGW(TAG, "command missing 'type'");
        cJSON_Delete(root);
        return;
    }
    ESP_LOGI(TAG, "command type=%s id=%s", type, id ? id : "(none)");

    if (strcmp(type, "ping") == 0) {
        handle_ping(id);
    } else if (strcmp(type, "ota_update") == 0) {
        const cJSON *jurl = cJSON_GetObjectItemCaseSensitive(root, "url");
        const char *url = cJSON_IsString(jurl) ? jurl->valuestring : NULL;
        if (already_applied(id)) {
            ESP_LOGI(TAG, "ota_update id=%s already applied — ignoring", id ? id : "");
        } else if (url == NULL) {
            ESP_LOGW(TAG, "ota_update id=%s missing 'url' — ignoring", id ? id : "");
        } else {
            /* Mark applied up front so a retained/duplicate trigger can't re-OTA
             * (at-least-once). The OTA worker downloads with comms suspended and
             * reboots; status is reported on the status topic. A failed update
             * needs a fresh id to retry. */
            mark_applied(id);
            esp_err_t err = ota_update_request(url, id);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "ota_update id=%s dispatch failed: %s",
                         id ? id : "", esp_err_to_name(err));
            } else {
                ESP_LOGW(TAG, "ota_update id=%s dispatched (url=%s)", id ? id : "", url);
            }
        }
    } else if (strcmp(type, "script_update") == 0) {
        ESP_LOGW(TAG, "script_update received (id=%s) — Stage-4 handler not wired yet",
                 id ? id : "");
    } else {
        ESP_LOGW(TAG, "unknown command type '%s'", type);
    }

    cJSON_Delete(root);
}

esp_err_t command_router_init(const command_router_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    ESP_LOGI(TAG, "command router ready (status_topic=%s)",
             s_cfg.status_topic ? s_cfg.status_topic : "(none)");
    return ESP_OK;
}

message_received_fn command_router_get_received_fn(void)
{
    return on_message;
}
