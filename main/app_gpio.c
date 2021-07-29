#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"

#include "driver/i2c.h"
#include "app_adxl345.h"

#define I2C_PORT			0

#define GPIO_ADDR			0x40
#define GPIO_IO_DIRECTION_REGISTER	0x00
#define GPIO_IO_POLARITY_REGISTER	0x01
#define GPIO_IO_CONFIG_REGISTER	        0x05
#define GPIO_SET_REGISTER 		0x09
#define GPIO_OUTPUT_LATCH_REGISTER	0x0a

#define GPIO_READ_BIT			0x1
#define GPIO_WRITE_BIT			0x0

#define ACK_CHECK_EN			0x1
#define ACK_VAL				0x0
#define NACK_VAL			0x1

static const char *TAG = "app_gpio";

esp_err_t gpio_read_reg(uint8_t reg, uint8_t *data){
	
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, GPIO_ADDR | GPIO_WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
	ESP_LOGE(TAG, "Write to GPIO expander incorrect: %d", ret);
        return ret;
    }
    vTaskDelay(10 / portTICK_RATE_MS);
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, GPIO_ADDR | GPIO_READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t gpio_write_reg(uint8_t reg, uint8_t comd){
	
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, GPIO_ADDR | GPIO_WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, comd, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t gpio_set_pin(uint8_t comd){

    int ret;
    ret = gpio_write_reg(GPIO_SET_REGISTER, comd);
    if(ret != ESP_OK)
	return ret;

    //ret = gpio_write_reg(GPIO_OUTPUT_LATCH_REGISTER, comd);
    return ret;
}

esp_err_t gpio_read_pin(uint8_t *gpio_val){

    int ret;
    ret = gpio_read_reg(GPIO_SET_REGISTER, gpio_val);
    return ret;
}

void gpio_expander_init(){

	// Reading from IO direction register for confirmation of device commmunication
	ESP_LOGI(TAG, "Reading from IO direction register on GPIO expander for confirmation of device communication....");
	uint8_t io_dir;
	if(ESP_OK == gpio_read_reg(GPIO_IO_DIRECTION_REGISTER, &io_dir)){
		ESP_LOGI(TAG, "Read complete! Value should be 0xff or 0xfc");
		if(io_dir == 0xff || io_dir == 0xfc){
			ESP_LOGI(TAG, "Correct value for IO direction register! Register holds: 0x%02x", io_dir);
		}else{
			ESP_LOGE(TAG, "Error: Incorrect value for IO direction register! Register holds: 0x%02x", io_dir);
		}
	}else{
		ESP_LOGE(TAG, "Error: Read of IO direction register incomplete");
	}

	// Setting IO direction register
	ESP_LOGI(TAG, "Setting IO direction register on GPIO expander to 0xfc....");
	if(ESP_OK == gpio_write_reg(GPIO_IO_DIRECTION_REGISTER, 0xfc)){
		ESP_LOGI(TAG, "Write complete!");
		gpio_read_reg(GPIO_IO_DIRECTION_REGISTER, &io_dir);
		if(io_dir == 0xfc){
			ESP_LOGI(TAG, "Correct value for IO direction register! Register holds: 0x%02x", io_dir);
		}else{
			ESP_LOGE(TAG, "Error: Incorrect value for IO direction register! Register holds: 0x%02x", io_dir);
		}
	}else{
		ESP_LOGE(TAG, "Error: Write of IO direction register incomplete");
	}

	// Setting IO polarity register
	ESP_LOGI(TAG, "Setting IO polarity register on GPIO expander to 0x00....");
	if(ESP_OK == gpio_write_reg(GPIO_IO_POLARITY_REGISTER, 0x00)){
		ESP_LOGI(TAG, "Write complete!");
		uint8_t io_pol;
		gpio_read_reg(GPIO_IO_POLARITY_REGISTER, &io_pol);
		if(io_pol == 0x00){
			ESP_LOGI(TAG, "Correct value for IO polarity register! Register holds: 0x%02x", io_pol);
		}else{
			ESP_LOGE(TAG, "Error: Incorrect value for IO polarity register! Register holds: 0x%02x", io_pol);
		}
	}else{
		ESP_LOGE(TAG, "Error: Write of IO polarity register incomplete");
	}

	// Setting IO configuration register for ADXL345
	ESP_LOGI(TAG, "Setting IO configuration register to 0x30...");
	if(ESP_OK == gpio_write_reg(GPIO_IO_CONFIG_REGISTER, 0x30)){
		ESP_LOGI(TAG, "Finished write of IO configuration register! Checking IO configuration register...");
		uint8_t io_config;
		gpio_read_reg(GPIO_IO_CONFIG_REGISTER, &io_config);
		if(io_config == 0x30){
			ESP_LOGI(TAG, "Correct value for IO config register! Register holds: 0x%02x", io_config);
		}else{
			ESP_LOGE(TAG, "Error: Incorrect value for IO config register! Register holds: 0x%02x", io_config);
		}
	}else{
		ESP_LOGE(TAG, "Error: Could not complete write of IO configuration register\n");
	}
}
