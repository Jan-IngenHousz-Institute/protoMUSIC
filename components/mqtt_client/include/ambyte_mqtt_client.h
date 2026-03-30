#pragma once

#include "esp_err.h"
#include "messaging_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *broker_uri;
    const char *client_id;
} mqtt_client_config_t;

esp_err_t mqtt_client_init(const mqtt_client_config_t *cfg);
void      mqtt_client_start(void);
void      mqtt_client_stop(void);

message_publish_fn                  mqtt_client_get_publish_fn(void);
message_is_connected_fn             mqtt_client_get_is_connected_fn(void);
message_set_publish_ack_handler_fn  mqtt_client_get_set_ack_handler_fn(void);
message_subscribe_fn                mqtt_client_get_subscribe_fn(void);

#ifdef __cplusplus
}
#endif
