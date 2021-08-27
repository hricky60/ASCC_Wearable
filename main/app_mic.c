#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "driver/i2s.h"

#include "tcpip_adapter.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "app_wifi.h"
#include "app_mic.h"

// Pin numbers for ESP-CAM
#define I2S_WS  	2
#define I2S_SD 		15
#define I2S_SCK 	14
#define I2S_PORT 	I2S_NUM_1

//max dma buffer parameters allowed by driver
#define I2S_DMA_BUF_COUNT 22
#define I2S_DMA_BUF_LEN   512

#define I2S_SAMPLE_RATE   41000
#define I2S_SAMPLE_BITS   32
#define I2S_CHANNEL_NUM   1
#define I2S_READ_LEN	  90024

#define TAG "app_rec"

extern int MAX_WIFI_LEN;

const int RECORD_TIME = 5; //Seconds
const int RECORD_SIZE = (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS/ 8 * RECORD_TIME);



esp_err_t mic_send(int sock, uint8_t *buf)
{   
    esp_err_t res = ESP_FAIL;
    uint32_t size = RECORD_SIZE;
    uint32_t bytes_sent = 0;
    char *mic_tag = "app_mic";

    ESP_LOGI(TAG, "\t\t\t\t *** Starting WAV Transmission *** ");
    
    res = send(sock, mic_tag, 7, 0);
    if (res < 0){
			ESP_LOGE(TAG, "\t\t\t\tFailed to send microphone tag");
			return res;
    }
    ESP_LOGI(TAG, "\t\t\t\tSent %d bytes for microphone tag \"%s\"", res, mic_tag);
    
    res = send(sock, &size, sizeof(int), 0);
    if (res < 0){
			ESP_LOGE(TAG, "\t\t\t\tFailed to send size of WAV data");
			return res;
    }
    ESP_LOGI(TAG, "\t\t\t\tSent %d bytes for recording size of %d", res, size);
    
    int count = 0;
    while(bytes_sent < RECORD_SIZE){
		
		if(size < MAX_WIFI_LEN)
			res = send(sock, buf, size, 0);
		else
			res = send(sock, buf, MAX_WIFI_LEN, 0);
			
        if (res <= 0){
			ESP_LOGE(TAG, "\t\t\t\tFailed to send WAV data");
			break;
		}

		ESP_LOGI(TAG, "\t\t\t\tSent %d bytes of audio data to server", res);
	        
        bytes_sent += res;
        size -= res;

		ESP_LOGI(TAG, "\t\t\t\t%d bytes of data remaining", size);
	        
        if(size > 0)
			buf += res;

		if((count % 8) == 0 && count <= 24){
			ESP_LOGI(TAG, "\t\t\t\tResetting interrupt watchdog");
			esp_task_wdt_reset();
		}
		count += 1;
    }
    ESP_LOGI(TAG, "\t\t\t\t *** WAV Transmission Complete *** ");
    
    return res;
}

void mic_i2s_init(void)
{
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,			
        .sample_rate = I2S_SAMPLE_RATE,                 
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,   
        .bits_per_sample = I2S_SAMPLE_BITS,             
        .communication_format = I2S_COMM_FORMAT_I2S,
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .intr_alloc_flags = 0,
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,  
        .ws_io_num = I2S_WS,   
        .data_out_num = -1,
        .data_in_num = I2S_SD  
    };
    
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

void i2s_data_scale(uint8_t *buff, uint32_t len)
{
	uint32_t dac_value = 0;
	
	for(int i=0; i<len; i+=2)
	{
		dac_value = (((uint16_t)(buff[i+1] & 0xf) << 8) | buff[i]);
		buff[i] = 0;
		buff[i+1] = dac_value * 256 / 2048;
	}
}

esp_err_t i2s_record(uint8_t *data, uint8_t *buf)
{
	esp_err_t res = ESP_FAIL;
	size_t data_wr_size = 0;
	size_t bytes_read = 0;
	uint32_t diff = RECORD_SIZE;
	uint32_t read_len = I2S_READ_LEN;
	
	ESP_LOGI(TAG, " *** Recording Start *** ");
	while(data_wr_size < RECORD_SIZE)
	{
		if(mic_flag == 0x01){
			ESP_LOGI(TAG, "Beginning to Reset Mic Task");
			goto RESET;
		}
		
		ESP_LOGI(TAG, "Reading data.....");
		if(diff > I2S_READ_LEN)
		{
			ESP_LOGI(TAG, "I2S transfer of %d bytes into %dB buffer.....", read_len, I2S_READ_LEN * sizeof(char));
			res = i2s_read(I2S_PORT, (void *)buf, read_len, &bytes_read, portMAX_DELAY);
		}
		else
		{
			ESP_LOGI(TAG, "I2S transfer of %d bytes into %dB buffer.....", diff, I2S_READ_LEN * sizeof(char));
			res = i2s_read(I2S_PORT, (void *)buf, diff, &bytes_read, portMAX_DELAY);
		}

		if(mic_flag == 0x01){
			ESP_LOGI(TAG, "Beginning to Reset Mic Task");
			data_wr_size += bytes_read;
			goto RESET;
		}
			
		if(res != ESP_OK)
			break;
		
		ESP_LOGI(TAG, "Scaling data.....");
		i2s_data_scale(buf, (uint32_t)bytes_read);

		if(mic_flag == 0x01){
			ESP_LOGI(TAG, "Beginning to Reset Mic Task");
			data_wr_size += bytes_read;
			goto RESET;
		}
		
		ESP_LOGI(TAG, "Copying data.....");
		memcpy(data, buf, bytes_read);
		
		data += (uint32_t)bytes_read;
		data_wr_size += bytes_read;
		ESP_LOGI(TAG, "---------------Sound recording %u%%---------------", data_wr_size * 100 / RECORD_SIZE);
		
		diff = RECORD_SIZE - data_wr_size;
	}
	ESP_LOGI(TAG, " *** Recording End *** ");

	ESP_LOGI(TAG, "Data pointer before reset: %p", data);
	RESET: data -= data_wr_size;
	ESP_LOGI(TAG, "Data pointer after reset: %p", data);
	
	return res;
}

esp_err_t app_mic(uint8_t *i2s_read_data, size_t *size)
{	
	//uint8_t rx_buffer[128];
	
	ESP_LOGI(TAG, "Initilizing mic for use");
	mic_i2s_init();

	ESP_LOGI(TAG, "Data pointer address before allocation: %p", i2s_read_data);
	
	uint8_t *i2s_read_buf  = (uint8_t *)heap_caps_malloc(I2S_READ_LEN * sizeof(uint8_t), MALLOC_CAP_8BIT);
	//i2s_read_data = (uint8_t *)heap_caps_malloc(RECORD_SIZE * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
	
	if(!i2s_read_buf){
		ESP_LOGE(TAG, "Memory allocation for i2s buffer failed");
		return ESP_FAIL;
	}
	if(!i2s_read_data){
		ESP_LOGE(TAG, "Memory allocation for i2s data failed");
		return ESP_FAIL;
	}

	if(mic_flag == 0x01){
		ESP_LOGI(TAG, "Beginning to Reset Mic Task");
		goto FREE;
	}

	ESP_LOGI(TAG, "Data pointer address after allocation: %p", i2s_read_data);
	
	ESP_LOGI(TAG, "Starting to record in 3 seconds....");
	for(int i=3; i>0; i--)
	{
		ESP_LOGI(TAG, "....%d....", i);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	if(mic_flag == 0x01){
		ESP_LOGI(TAG, "Beginning to Reset Mic Task");
		goto FREE;
	}
	
	int res = i2s_record(i2s_read_data, i2s_read_buf);
	if(res != ESP_OK){
		ESP_LOGE(TAG, "Error during recording");
		return res;
	}
	
	
	FREE: free(i2s_read_buf);

	i2s_driver_uninstall(I2S_PORT);

	return ESP_OK;
}
