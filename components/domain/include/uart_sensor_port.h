#ifndef AMBYTE_UART_SENSOR_PORT_H
#define AMBYTE_UART_SENSOR_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_SENSOR_NUM_CHANNELS  4
#define UART_SENSOR_MAX_ARRAYS    8

/* Sentinel value for expect_raw: return immediately after CMD_DONE.
 * Used for configuration commands (set gains, set currents, etc.) that
 * send CMD_DONE but no CMD_END and no response data. */
#define UART_QUERY_ACK_ONLY  ((size_t)-1)

typedef enum {
    UART_SENSOR_DISCONNECTED = 0,
    UART_SENSOR_CONNECTED    = 1,
    UART_SENSOR_BUSY         = 2,
} uart_sensor_state_t;

/* One data array received via FSM handshake (one per fsm_send_esp call on the
 * ambit side).  Caller frees via uart_sensor_response_free(). */
typedef struct {
    uint8_t   index;        /* array index (0–7) reported in the FSM length header */
    uint32_t *data;         /* heap-allocated uint32 array */
    uint16_t  length;       /* number of uint32_t elements */
} uart_data_array_t;

/* Response returned by a UART sensor query. */
typedef struct {
    /* Structured FSM data arrays (populated for run/measurement commands) */
    uart_data_array_t arrays[UART_SENSOR_MAX_ARRAYS];
    uint8_t           array_count;

    /* Raw response bytes (populated for simple immediate-response commands) */
    uint8_t          *raw;
    size_t            raw_len;

    esp_err_t         status;
} uart_sensor_response_t;

/* Free all heap-allocated fields inside a response struct. */
static inline void uart_sensor_response_free(uart_sensor_response_t *resp)
{
    if (resp == NULL) {
        return;
    }
    for (uint8_t i = 0; i < resp->array_count; i++) {
        free(resp->arrays[i].data);
        resp->arrays[i].data   = NULL;
        resp->arrays[i].length = 0;
    }
    resp->array_count = 0;
    free(resp->raw);
    resp->raw     = NULL;
    resp->raw_len = 0;
}

/* ---------------------------------------------------------------------------
 * Port function-pointer types
 * ---------------------------------------------------------------------------
 * uart_sensor_query_fn — send a binary ESP command and collect the response.
 *
 *   channel       : 0–3 (AMBIT1–AMBIT4)
 *   cmd           : 8-byte command array (the payload after the 0xA0 header)
 *   extra/extra_len: optional additional payload bytes (e.g. run-array config)
 *   expect_raw    : expected raw response length for immediate commands.
 *                   0 → FSM data-transfer mode (measurement commands).
 *                   >0 → read exactly this many bytes after CMD_DONE, then
 *                         verify CMD_END follows.
 *   response      : output — caller must call uart_sensor_response_free()
 *   timeout_ms    : overall timeout for the entire exchange
 */
typedef esp_err_t (*uart_sensor_query_fn)(uint8_t channel,
                                          const uint8_t cmd[8],
                                          const uint8_t *extra, size_t extra_len,
                                          size_t expect_raw,
                                          uart_sensor_response_t *response,
                                          uint32_t timeout_ms);

typedef esp_err_t (*uart_sensor_ping_fn)(uint8_t channel, bool *connected);

typedef esp_err_t (*uart_sensor_status_fn)(uint8_t channel,
                                           uart_sensor_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AMBYTE_UART_SENSOR_PORT_H */
