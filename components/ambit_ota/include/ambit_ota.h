#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "messaging_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-driven AMBIT (ESP32-C3) firmware update over UART.
 *
 * The ambyte downloads a C3 firmware image from a public HTTPS URL to the SD
 * card, then streams it to one AMBIT sensor over the existing binary UART link
 * (cmds 25-28: OTA_BEGIN / OTA_DATA / OTA_END / OTA_ABORT — see run_esp.cpp on
 * the ambit side and cmd_ambit_ota_* in device_commands). The AMBIT writes the
 * image into its spare OTA slot via Arduino's Update, verifies it, and reboots
 * into it. The ambyte itself does NOT reboot.
 *
 * Quiescing: the worker stops the Lua measurement task (so it can't contend for
 * the shared UART) and MQTT (the board can't hold two TLS sessions during the
 * download) for the duration, then resumes both. The image is staged on SD
 * first — not streamed straight from HTTP — because the C3 sleeps after UART
 * silence and the heap can't buffer a whole image; SD decouples the two halves.
 *
 * v1 is CLI-triggered (`ambit_ota <ch> <url>`); an MQTT trigger is a follow-up.
 */

typedef struct {
    void (*workload_suspend)(void);   /* stop the Lua task during the update; NULL = skip */
    void (*workload_resume)(void);    /* restart it afterward; NULL = skip */
    void (*comms_suspend)(void);      /* mqtt_client_stop — free TLS heap for the HTTPS download */
    void (*comms_resume)(void);       /* mqtt_client_start — after the update */

    /* Optional best-effort status reporting (used once MQTT is back; NULL = log-only). */
    message_publish_fn      publish;
    message_is_connected_fn is_connected;
    const char             *status_topic;
    const char             *device_id;
} ambit_ota_config_t;

/* Start the AMBIT-OTA worker task (idempotent). */
esp_err_t ambit_ota_init(const ambit_ota_config_t *cfg);

/* Queue an AMBIT firmware update on `channel` (0-3) from `url` (a direct .bin —
 * raw.githubusercontent.com/… or a release-asset link, NOT a /blob//tree/ page).
 * Non-blocking: the worker quiesces, downloads, streams, and the AMBIT reboots.
 * ESP_ERR_INVALID_STATE before init; ESP_ERR_INVALID_ARG on a bad channel/url;
 * ESP_ERR_NO_MEM if an update is already queued. */
esp_err_t ambit_ota_request(uint8_t channel, const char *url);

#ifdef __cplusplus
}
#endif
