#include "RTC_NXP.h"

#include <cstring>

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bus.h"

namespace {

constexpr TickType_t PCF2131_I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(1000);

esp_err_t pcf2131_i2c_lock_bus(i2c_port_t *out_port)
{
    esp_err_t err = i2c_bus_get_port(out_port);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_bus_lock(PCF2131_I2C_TIMEOUT_TICKS);
}

void pcf2131_i2c_unlock_bus(void)
{
    (void)i2c_bus_unlock();
}

} // namespace

PCF2131_I2C::PCF2131_I2C(uint8_t i2c_address) : i2c_address_(i2c_address)
{
}

PCF2131_I2C::~PCF2131_I2C()
{
}

void PCF2131_I2C::_reg_w(uint8_t reg, const uint8_t *vp, int len)
{
    if ((vp == nullptr) && (len > 0)) {
        set_error(ESP_ERR_INVALID_ARG);
        return;
    }

    i2c_port_t port = I2C_NUM_MAX;
    esp_err_t err = pcf2131_i2c_lock_bus(&port);
    if (err != ESP_OK) {
        set_error(err);
        return;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == nullptr) {
        pcf2131_i2c_unlock_bus();
        set_error(ESP_ERR_NO_MEM);
        return;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, static_cast<uint8_t>((i2c_address_ << 1) | I2C_MASTER_WRITE), true);
    i2c_master_write_byte(cmd, reg, true);
    if (len > 0) {
        i2c_master_write(cmd, const_cast<uint8_t *>(vp), len, true);
    }
    i2c_master_stop(cmd);

    err = i2c_master_cmd_begin(port, cmd, PCF2131_I2C_TIMEOUT_TICKS);
    i2c_cmd_link_delete(cmd);
    pcf2131_i2c_unlock_bus();

    set_error(err);
}

void PCF2131_I2C::_reg_r(uint8_t reg, uint8_t *vp, int len)
{
    if ((vp == nullptr) && (len > 0)) {
        set_error(ESP_ERR_INVALID_ARG);
        return;
    }

    i2c_port_t port = I2C_NUM_MAX;
    esp_err_t err = pcf2131_i2c_lock_bus(&port);
    if (err != ESP_OK) {
        set_error(err);
        return;
    }

    err = i2c_master_write_read_device(
        port,
        i2c_address_,
        &reg,
        1,
        vp,
        len,
        PCF2131_I2C_TIMEOUT_TICKS);

    pcf2131_i2c_unlock_bus();

    if ((err != ESP_OK) && (vp != nullptr) && (len > 0)) {
        memset(vp, 0, static_cast<size_t>(len));
    }

    set_error(err);
}

void PCF2131_I2C::_reg_w(uint8_t reg, uint8_t val)
{
    _reg_w(reg, &val, 1);
}

uint8_t PCF2131_I2C::_reg_r(uint8_t reg)
{
    uint8_t value = 0;
    _reg_r(reg, &value, 1);
    return value;
}

void PCF2131_I2C::_bit_op8(uint8_t reg, uint8_t mask, uint8_t val)
{
    const uint8_t current = _reg_r(reg);
    if (last_error() != ESP_OK) {
        return;
    }

    uint8_t updated = current;
    updated &= mask;
    updated |= val;

    _reg_w(reg, updated);
}
