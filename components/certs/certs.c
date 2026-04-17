#include "certs.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG           "certs"
#define NVS_NS        "certs"
#define KEY_CA        "ca_cert"
#define KEY_CERT      "dev_cert"
#define KEY_KEY       "dev_key"
#define CERT_BUF_SIZE 2048

static char s_ca_cert[CERT_BUF_SIZE];
static char s_dev_cert[CERT_BUF_SIZE];
static char s_dev_key[CERT_BUF_SIZE];

static nvs_handle_t s_handle      = 0;
static bool         s_initialized = false;

esp_err_t certs_init(void)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_initialized = true;

    size_t len = sizeof(s_ca_cert);
    if (nvs_get_str(s_handle, KEY_CA, s_ca_cert, &len) != ESP_OK) {
        s_ca_cert[0] = '\0';
    }
    len = sizeof(s_dev_cert);
    if (nvs_get_str(s_handle, KEY_CERT, s_dev_cert, &len) != ESP_OK) {
        s_dev_cert[0] = '\0';
    }
    len = sizeof(s_dev_key);
    if (nvs_get_str(s_handle, KEY_KEY, s_dev_key, &len) != ESP_OK) {
        s_dev_key[0] = '\0';
    }

    ESP_LOGI(TAG, "certs initialised (provisioned=%s)",
             certs_are_provisioned() ? "yes" : "no");
    return ESP_OK;
}

bool certs_are_provisioned(void)
{
    return s_initialized &&
           s_ca_cert[0]  != '\0' &&
           s_dev_cert[0] != '\0' &&
           s_dev_key[0]  != '\0';
}

const char *certs_get_ca_cert(void)     { return s_ca_cert;  }
const char *certs_get_device_cert(void) { return s_dev_cert; }
const char *certs_get_device_key(void)  { return s_dev_key;  }

static esp_err_t cert_set(const char *key, char *buf, const char *pem)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_str(s_handle, key, pem);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_handle);
    if (err == ESP_OK) {
        strncpy(buf, pem, CERT_BUF_SIZE - 1);
        buf[CERT_BUF_SIZE - 1] = '\0';
    }
    return err;
}

esp_err_t certs_set_ca_cert(const char *pem)
{
    return cert_set(KEY_CA, s_ca_cert, pem);
}

esp_err_t certs_set_device_cert(const char *pem)
{
    return cert_set(KEY_CERT, s_dev_cert, pem);
}

esp_err_t certs_set_device_key(const char *pem)
{
    return cert_set(KEY_KEY, s_dev_key, pem);
}
