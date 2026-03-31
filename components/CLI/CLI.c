#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "CLI.h"
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "device_commands.h"
#include "i2c_bus.h"

static const uint8_t CLI_I2C_SCAN_FIRST_ADDR = 0x08;
static const uint8_t CLI_I2C_SCAN_LAST_ADDR = 0x77;
static const uint8_t CLI_RTC_I2C_ADDR = (uint8_t)(0xA6 >> 1);
static const uint8_t CLI_BME280_ADDR_PRIMARY = 0x77;
static const uint8_t CLI_BME280_ADDR_SECONDARY = 0x76;
static const TickType_t CLI_I2C_SCAN_TIMEOUT_TICKS = pdMS_TO_TICKS(50);
static const TickType_t CLI_I2C_SCAN_LOCK_TIMEOUT_TICKS = pdMS_TO_TICKS(1000);
static esp_console_repl_t *s_cli_repl = NULL;
static bool s_cli_commands_registered = false;

static esp_err_t cli_i2c_probe_locked(i2c_port_t port, uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);

    const esp_err_t err = i2c_master_cmd_begin(port, cmd, CLI_I2C_SCAN_TIMEOUT_TICKS);
    i2c_cmd_link_delete(cmd);
    return err;
}

static const char *cli_i2c_label_for_addr(uint8_t addr)
{
    switch (addr) {
        case CLI_RTC_I2C_ADDR:
            return "PCF2131 RTC";
        case CLI_BME280_ADDR_PRIMARY:
            return "BME280 candidate (primary)";
        case CLI_BME280_ADDR_SECONDARY:
            return "BME280 candidate (secondary)";
        default:
            return NULL;
    }
}

static int cli_cmd_status(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: status\r\n");
        return 1;
    }

    bool bme_ready = false;
    bool rtc_ready = false;
    time_t rtc_time = 0;
    cmd_result_t res = cmd_device_status(&bme_ready, &rtc_ready, &rtc_time);
    printf("CLI status:\r\n");
    printf(" - BME280 ready: %s\r\n", bme_ready ? "yes" : "no");
    printf(" - RTC ready: %s\r\n", rtc_ready ? "yes" : "no");

    if (rtc_ready) {
        struct tm tm_now;
        char now_s[32] = {0};
        localtime_r(&rtc_time, &tm_now);
        strftime(now_s, sizeof(now_s), "%Y-%m-%d %H:%M:%S", &tm_now);
        printf(" - RTC now: %s (%lld)\r\n", now_s, (long long)rtc_time);
    }
    (void)res;
    return 0;
}

static int cli_cmd_rtc(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: rtc\r\n");
        return 1;
    }

    time_t now = 0;
    cmd_result_t res = cmd_read_rtc(&now);
    if (res.status != ESP_OK) {
        printf("RTC read error: %s\r\n", res.message);
        return 1;
    }

    struct tm tm_now;
    char now_s[32] = {0};
    localtime_r(&now, &tm_now);
    strftime(now_s, sizeof(now_s), "%Y-%m-%d %H:%M:%S", &tm_now);
    printf("RTC: %s (%lld)\r\n", now_s, (long long)now);
    return 0;
}

static int cli_cmd_red(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: red <0|1>\r\n");
        return 1;
    }

    cmd_result_t res;
    if (strcmp(argv[1], "0") == 0) {
        res = cmd_set_rgb(0, 0, 0);
        if (res.status == ESP_OK) {
            printf("Red LED off\r\n");
            return 0;
        }
    } else if (strcmp(argv[1], "1") == 0) {
        res = cmd_set_rgb(5, 0, 0);
        if (res.status == ESP_OK) {
            printf("Red LED on\r\n");
            return 0;
        }
    } else {
        printf("Usage: red <0|1>\r\n");
        return 1;
    }

    printf("Status LED error: %s\r\n", res.message);
    return 1;
}

static int cli_cmd_read_env(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: read_env\r\n");
        return 1;
    }

    float temp = 0, hum = 0, pres = 0;
    cmd_result_t res = cmd_read_env(&temp, &hum, &pres);
    if (res.status != ESP_OK) {
        printf("read_env failed: %s\r\n", res.message);
        return 1;
    }
    printf("T=%.2fC  H=%.1f%%  P=%.0fPa\r\n", temp, hum, pres);
    return 0;
}

static int cli_cmd_cert_status(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: cert_status\r\n");
        return 1;
    }

    cmd_result_t res = cmd_cert_status();
    printf("%s\r\n", res.message);
    return (res.status == ESP_OK) ? 0 : 1;
}

static int cli_cmd_mqtt_pub(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: mqtt_pub <payload>\r\n");
        return 1;
    }

    /* Rejoin all tokens — allows JSON with spaces without requiring quotes */
    static char s_payload[480];
    int pos = 0;
    for (int i = 1; i < argc && pos < (int)sizeof(s_payload) - 1; i++) {
        if (i > 1 && pos < (int)sizeof(s_payload) - 2) {
            s_payload[pos++] = ' ';
        }
        int n = snprintf(s_payload + pos, sizeof(s_payload) - (size_t)pos, "%s", argv[i]);
        if (n > 0) pos += n;
    }
    s_payload[pos] = '\0';

    cmd_result_t res = cmd_mqtt_publish_raw(s_payload);
    printf("%s\r\n", res.message);
    return (res.status == ESP_OK) ? 0 : 1;
}

static int cli_cmd_i2cscan(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        printf("Usage: i2cscan\r\n");
        return 1;
    }

    i2c_port_t port = I2C_NUM_MAX;
    esp_err_t err = i2c_bus_get_port(&port);
    if (err != ESP_OK) {
        printf("I2C bus unavailable: %s\r\n", esp_err_to_name(err));
        return 1;
    }

    err = i2c_bus_lock(CLI_I2C_SCAN_LOCK_TIMEOUT_TICKS);
    if (err != ESP_OK) {
        printf("I2C bus lock failed: %s\r\n", esp_err_to_name(err));
        return 1;
    }

    int found_count = 0;
    bool scan_error = false;
    bool found_rtc = false;
    bool found_bme_primary = false;
    bool found_bme_secondary = false;

    printf("I2C scan results on port %d (7-bit addresses 0x%02X-0x%02X):\r\n",
           (int)port,
           CLI_I2C_SCAN_FIRST_ADDR,
           CLI_I2C_SCAN_LAST_ADDR);

    for (uint8_t addr = CLI_I2C_SCAN_FIRST_ADDR; addr <= CLI_I2C_SCAN_LAST_ADDR; ++addr) {
        const esp_err_t probe_err = cli_i2c_probe_locked(port, addr);
        if (probe_err == ESP_OK) {
            const char *label = cli_i2c_label_for_addr(addr);
            printf(" - 0x%02X", addr);
            if (label != NULL) {
                printf("  %s", label);
            }
            printf("\r\n");

            ++found_count;
            found_rtc = found_rtc || (addr == CLI_RTC_I2C_ADDR);
            found_bme_primary = found_bme_primary || (addr == CLI_BME280_ADDR_PRIMARY);
            found_bme_secondary = found_bme_secondary || (addr == CLI_BME280_ADDR_SECONDARY);
            continue;
        }

        if (probe_err != ESP_FAIL) {
            printf("Scan aborted at 0x%02X: %s\r\n", addr, esp_err_to_name(probe_err));
            scan_error = true;
            break;
        }
    }

    const esp_err_t unlock_err = i2c_bus_unlock();
    if (unlock_err != ESP_OK) {
        printf("I2C bus unlock failed: %s\r\n", esp_err_to_name(unlock_err));
        return 1;
    }

    if (scan_error) {
        return 1;
    }

    if (found_count == 0) {
        printf("No I2C devices acknowledged in the scanned range.\r\n");
    } else {
        printf("Found %d device(s).\r\n", found_count);
    }

    printf(
        "Summary: RTC=%s  0x%02X=%s  0x%02X=%s\r\n",
        found_rtc ? "yes" : "no",
        CLI_BME280_ADDR_SECONDARY,
        found_bme_secondary ? "yes" : "no",
        CLI_BME280_ADDR_PRIMARY,
        found_bme_primary ? "yes" : "no");
    return 0;
}

static esp_err_t cli_register_commands(void)
{
    if (s_cli_commands_registered) {
        return ESP_OK;
    }

    static const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "print peripheral readiness and current RTC time",
        .func = cli_cmd_status,
    };
    static const esp_console_cmd_t rtc_cmd = {
        .command = "rtc",
        .help = "print current RTC timestamp",
        .func = cli_cmd_rtc,
    };
    static const esp_console_cmd_t red_cmd = {
        .command = "red",
        .help = "red <0|1>  turn red LED off/on",
        .func = cli_cmd_red,
    };
    static const esp_console_cmd_t read_env_cmd = {
        .command = "read_env",
        .help = "read BME280 temperature, humidity, pressure",
        .func = cli_cmd_read_env,
    };
    static const esp_console_cmd_t i2cscan_cmd = {
        .command = "i2cscan",
        .help = "scan the shared I2C bus for responding 7-bit addresses",
        .func = cli_cmd_i2cscan,
    };
    static const esp_console_cmd_t cert_status_cmd = {
        .command = "cert_status",
        .help = "print whether TLS certificates have been provisioned",
        .func = cli_cmd_cert_status,
    };
    static const esp_console_cmd_t mqtt_pub_cmd = {
        .command = "mqtt_pub",
        .help = "mqtt_pub <payload>  publish string to <topic_root>/<device_id>/cli",
        .func = cli_cmd_mqtt_pub,
    };

    esp_err_t err = esp_console_cmd_register(&status_cmd);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_cmd_register(&rtc_cmd);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_cmd_register(&red_cmd);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_cmd_register(&read_env_cmd);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_cmd_register(&i2cscan_cmd);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_cmd_register(&cert_status_cmd);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_cmd_register(&mqtt_pub_cmd);
    if (err != ESP_OK) {
        return err;
    }

    s_cli_commands_registered = true;
    return ESP_OK;
}

static void cli_reset_after_failure(void)
{
    (void)esp_console_deinit();
    s_cli_commands_registered = false;
    s_cli_repl = NULL;
}

static esp_err_t cli_create_repl(const esp_console_repl_config_t *repl_config)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usb_serial_jtag_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    return esp_console_new_repl_usb_serial_jtag(&usb_serial_jtag_config, repl_config, &s_cli_repl);
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t usb_cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    return esp_console_new_repl_usb_cdc(&usb_cdc_config, repl_config, &s_cli_repl);
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    return esp_console_new_repl_uart(&uart_config, repl_config, &s_cli_repl);
#else
    (void)repl_config;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t cli_start(void)
{
    if (s_cli_repl != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "ambyte> ";
    repl_config.max_cmdline_length = 512;

    esp_err_t err = cli_create_repl(&repl_config);
    if (err != ESP_OK) {
        s_cli_repl = NULL;
        return err;
    }

    err = cli_register_commands();
    if (err != ESP_OK) {
        cli_reset_after_failure();
        return err;
    }

    err = esp_console_start_repl(s_cli_repl);
    if (err != ESP_OK) {
        cli_reset_after_failure();
        return err;
    }

    return ESP_OK;
}
