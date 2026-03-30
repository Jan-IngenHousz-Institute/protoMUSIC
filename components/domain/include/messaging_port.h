#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* out_msg_id receives the broker-assigned message ID (QoS 1); may be NULL for fire-and-forget */
typedef esp_err_t (*message_publish_fn)(const char *topic, const char *payload, size_t len,
                                        int *out_msg_id);
typedef bool      (*message_is_connected_fn)(void);

/* Callback delivered when a QoS-1 publish is acknowledged (or fails) */
typedef void      (*message_publish_ack_fn)(int msg_id, esp_err_t status, void *ctx);
typedef esp_err_t (*message_set_publish_ack_handler_fn)(message_publish_ack_fn handler,
                                                         void *ctx);

/* Phase 6B: inbound subscribe support */
typedef void      (*message_handler_fn)(const char *topic, const char *payload, size_t len);
typedef esp_err_t (*message_subscribe_fn)(const char *topic, message_handler_fn handler);

#ifdef __cplusplus
}
#endif
