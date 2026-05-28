#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "../pin_config.h"

#include "lib/ADPD6000/adi_adpd6000.h"






uint8_t check_adpd(spi_device_handle_t handle){
    /// @brief  Check ADPD connected
    /// @param ADPD number 
    /// @return 
    esp_err_t err = 1;
    uint16_t ret = 1;

    spi_transaction_t check_adpd = {
    .addr = 0x0010,
    .length = 16,
    .rxlength = 16,
    .rx_buffer = &ret,
    };

    err = spi_device_polling_transmit(handle, &check_adpd);
        
    if (err != ESP_OK) {Serial.print("SPI communication failed: "); Serial.println(esp_err_to_name(err));return 1;}
    ret = SPI_SWAP_DATA_RX(ret, 16);
    Serial.println(ret);

    return 1;

}


int32_t spi_read(void* user_data, uint8_t *rd_buf, uint32_t rd_len, uint8_t *wr_buf, uint32_t wr_len){

    spi_device_handle_t handle = * ((spi_device_handle_t*) user_data);

    esp_err_t err;
    uint16_t addr = 0x0000;
    addr |= wr_buf[0] << 8;
    addr |= wr_buf[1];
 
    spi_transaction_t spi_read_and_write = {
        .addr = addr,
        .length = rd_len * 8,
        .rxlength = rd_len * 8,
        .rx_buffer = rd_buf
    };
    err = spi_device_polling_transmit(handle, &spi_read_and_write);
    if (err != ESP_OK) Serial.println(esp_err_to_name(err));
    return 0;

}
int32_t spi_write(void* user_data, uint8_t *wr_buf, uint32_t len){

    spi_device_handle_t handle = * ((spi_device_handle_t*) user_data);

    esp_err_t err;
    uint16_t addr = 0x0000;
    addr |= wr_buf[0] << 8; 
    addr |= wr_buf[1];
 
    spi_transaction_t spi_read_and_write = {
        .addr = addr,
        .length = (len - 2)<<3,
        .rxlength = (len - 2)<<3,
        .tx_buffer = &wr_buf[2],
        .rx_buffer = &wr_buf[2]
    };
    err = spi_device_polling_transmit(handle, &spi_read_and_write);
    if (err != ESP_OK) Serial.println(esp_err_to_name(err));
    return 0;
}

int32_t log_print(void* user_data, char *string){
  Serial.println(string);
  return 0;
}

