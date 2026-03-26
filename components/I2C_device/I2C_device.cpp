#include "I2C_device.h"

#include "i2c_bus.h"

namespace {

constexpr TickType_t I2C_DEVICE_TIMEOUT_TICKS = pdMS_TO_TICKS(1000);

int i2c_device_err_to_result(esp_err_t err)
{
    switch (err) {
        case ESP_OK:
            return 0;
        case ESP_ERR_TIMEOUT:
            return -5;
        default:
            return -4;
    }
}

esp_err_t i2c_device_lock_bus(i2c_port_t *out_port)
{
    esp_err_t err = i2c_bus_get_port(out_port);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_bus_lock(I2C_DEVICE_TIMEOUT_TICKS);
}

void i2c_device_unlock_bus(void)
{
    (void)i2c_bus_unlock();
}

esp_err_t i2c_device_ping_locked(i2c_port_t port, uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, static_cast<uint8_t>((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);

    const esp_err_t err = i2c_master_cmd_begin(port, cmd, I2C_DEVICE_TIMEOUT_TICKS);
    i2c_cmd_link_delete(cmd);
    return err;
}

} // namespace

I2C_device::I2C_device( uint8_t i2c_address, bool repeated_start_enable ) : i2c_addr( i2c_address ), i2c( Wire ), rs_dis( !repeated_start_enable )
{
}

I2C_device::I2C_device( TwoWire& wire, uint8_t i2c_address, bool repeated_start_enable ) : i2c_addr( i2c_address ), i2c( wire ), rs_dis( !repeated_start_enable )
{
}

I2C_device::~I2C_device()
{
}

void I2C_device::repeated_start_enable( bool en )
{
	rs_dis	= !en;
}

bool I2C_device::ping( void )
{
    i2c_port_t port = I2C_NUM_MAX;
    const esp_err_t err = i2c_device_lock_bus(&port);
    if (err != ESP_OK) {
        return false;
    }

    const esp_err_t ping_err = i2c_device_ping_locked(port, i2c_addr);
    i2c_device_unlock_bus();
    return ping_err == ESP_OK;
}

bool I2C_device::ping( uint8_t addr )
{
    i2c_port_t port = I2C_NUM_MAX;
    const esp_err_t err = i2c_device_lock_bus(&port);
    if (err != ESP_OK) {
        return false;
    }

    const esp_err_t ping_err = i2c_device_ping_locked(port, addr);
    i2c_device_unlock_bus();
    return ping_err == ESP_OK;
}

void I2C_device::scan( TwoWire& target_i2c, uint8_t stop )
{
	(void)target_i2c;
	bool  result[ 128 ];

	for ( uint8_t i = 0; i < stop; i++ ) {
		result[ i ] = ping( i );
	}

	for ( uint8_t i = stop; i < 128; i++ ) {
		result[i] = false;
	}

	Serial.print( "\nI2C scan result\n   " );
	for ( uint8_t x = 0; x < 16; x++ ) {
		Serial.print( " x" );
		Serial.print( x, HEX );
	}
	
	for ( uint8_t i = 0; i < 128; i++ ) {
		if ( !( i % 16) ) {
			Serial.print( "\n" );
			Serial.print( i / 16, HEX );
			Serial.print( "x:" );
		}
		
		if ( result[ i ] ) {
			Serial.print( " " );
			Serial.print( i, HEX );
		}
		else {
			Serial.print( " --" );
		}
	}
	Serial.print( "\n" );			
}

int I2C_device::tx( const uint8_t *data, uint16_t size, bool stop )
{
    i2c_port_t port = I2C_NUM_MAX;
    esp_err_t err = i2c_device_lock_bus(&port);
    if (err != ESP_OK) {
        return i2c_device_err_to_result(err);
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == nullptr) {
        i2c_device_unlock_bus();
        return -4;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, static_cast<uint8_t>((i2c_addr << 1) | I2C_MASTER_WRITE), true);
    if ((data != nullptr) && size) {
        i2c_master_write(cmd, const_cast<uint8_t *>(data), size, true);
    }
    if (stop) {
        i2c_master_stop(cmd);
    }

    err = i2c_master_cmd_begin(port, cmd, I2C_DEVICE_TIMEOUT_TICKS);
    i2c_cmd_link_delete(cmd);
    i2c_device_unlock_bus();

    if (err != ESP_OK) {
        return i2c_device_err_to_result(err);
    }

    return size;
}

int I2C_device::rx( uint8_t *data, uint16_t size )
{
    if ((data == nullptr) && size) {
        return -4;
    }

    i2c_port_t port = I2C_NUM_MAX;
    esp_err_t err = i2c_device_lock_bus(&port);
    if (err != ESP_OK) {
        return i2c_device_err_to_result(err);
    }

    err = i2c_master_read_from_device(port, i2c_addr, data, size, I2C_DEVICE_TIMEOUT_TICKS);
    i2c_device_unlock_bus();

    if (err != ESP_OK) {
        return i2c_device_err_to_result(err);
    }

    return size;
}

int I2C_device::reg_w( uint8_t reg_adr, const uint8_t *data, uint16_t size )
{
	uint8_t buffer[ size + 1 ];
	
	buffer[ 0 ]	= reg_adr;
	for ( uint16_t i = 0; i < size; i++)
		buffer[ i + 1 ]	= data[ i ];
	
	return tx( buffer, sizeof( buffer ) );
}

int I2C_device::reg_w( uint8_t reg_adr, uint8_t data )
{
	uint8_t buffer[ 2 ];
	
	buffer[ 0 ]	= reg_adr;
	buffer[ 1 ]	= data;
	
	return tx( buffer, sizeof( buffer ) );
}

int I2C_device::reg_r( uint8_t reg_adr, uint8_t *data, uint16_t size )
{
    if ((data == nullptr) && size) {
        return -4;
    }

    i2c_port_t port = I2C_NUM_MAX;
    esp_err_t err = i2c_device_lock_bus(&port);
    if (err != ESP_OK) {
        return i2c_device_err_to_result(err);
    }

    if (rs_dis) {
        err = i2c_master_write_to_device(port, i2c_addr, &reg_adr, 1, I2C_DEVICE_TIMEOUT_TICKS);
        if (err == ESP_OK) {
            err = i2c_master_read_from_device(port, i2c_addr, data, size, I2C_DEVICE_TIMEOUT_TICKS);
        }
    } else {
        err = i2c_master_write_read_device(port, i2c_addr, &reg_adr, 1, data, size, I2C_DEVICE_TIMEOUT_TICKS);
    }

    i2c_device_unlock_bus();

    if (err != ESP_OK) {
        return i2c_device_err_to_result(err);
    }

    return size;
}

uint8_t I2C_device::reg_r( uint8_t reg_adr )
{
	uint8_t	buffer	= 0;

	reg_r( reg_adr, &buffer, 1 );
	return buffer;
} 

void I2C_device::write_r8( uint8_t reg, uint8_t val )
{
	reg_w( reg, val );
}

void I2C_device::write_r16( uint8_t reg, uint16_t val )
{
	uint8_t	buff[ 2 ];
	
	buff[ 0 ]	= val >> 8;
	buff[ 1 ]	= val & 0xFF;
	
	reg_w( reg, buff, sizeof( buff ) );
}

uint8_t I2C_device::read_r8( uint8_t reg )
{
	return reg_r( reg );	
}

uint16_t I2C_device::read_r16( uint8_t reg )
{
	uint8_t	buff[ 2 ];

	reg_r( reg, buff, sizeof( buff ) );
	
	return (buff[ 0 ] << 8) | buff[ 1 ];
}

void I2C_device::bit_op8( uint8_t reg, uint8_t mask, uint8_t value )
{
	uint8_t	v	= read_r8( reg );

	v	&= mask;
	v	|= value;
	
	write_r8( reg, v );
}

void I2C_device::bit_op16( uint8_t reg, uint16_t mask, uint16_t value )
{
	uint16_t v	= read_r16( reg );

	v	&= mask;
	v	|= value;

	write_r16( reg, v );
}

#include	<SPI.h>

void I2C_device::txrx( const uint8_t *w_data, uint8_t *r_data, uint16_t size )
{
	memcpy( r_data, w_data, size );
	
	SPI.beginTransaction( spi_setting );
	
	digitalWrite( SS, LOW );
	SPI.transfer( r_data, size );
	digitalWrite( SS, HIGH );
	
	SPI.endTransaction();
}
