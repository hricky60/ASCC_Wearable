#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "driver/i2s.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "tcpip_adapter.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "app_gpio.h"

// Pin numbers for ESP-CAM
#define I2S_WS  	4			//LRC  -- Final pin = 4;	Testing pins = 14, 4 
#define I2S_SD 		3			//DIN  -- Final pin = 3;	Testing pins = 2, 14
#define I2S_SCK 	1			//BCLK -- Final pin = 1;	Testing pins = 15, 15
#define I2S_PORT 	I2S_NUM_1	//Port 0 taken by camera thus port 1 used

//max dma buffer parameters allowed by driver
#define I2S_DMA_BUF_COUNT 32	// Number of DMA buffers
#define I2S_DMA_BUF_LEN   125	// Number of samples per buffer

#define I2S_SAMPLE_RATE   16000
#define I2S_SAMPLE_BITS   16
#define I2S_CHANNEL_NUM   1
#define MAX_DMA_LEN	  	  I2S_DMA_BUF_LEN * I2S_DMA_BUF_COUNT * (I2S_SAMPLE_BITS / 8)

#define TAG "app_speaker"
#define WIFI_TAG "app_wifi"

const int MAX_AUDIO_TIME = 15; //unit seconds
const int MAX_AUDIO_SIZE = (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS/ 8 * MAX_AUDIO_TIME);



esp_err_t i2s_init(void)
{
	esp_err_t ret;
	
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,			
        .sample_rate = I2S_SAMPLE_RATE,
        .channel_format = I2S_CHANNEL_FMT_ALL_LEFT, // Outputs only the left channels but still need to send right channel data
        .bits_per_sample = I2S_SAMPLE_BITS,             
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB, // Philips Standard
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .intr_alloc_flags = 0
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,  
        .ws_io_num = I2S_WS,   
        .data_out_num = I2S_SD,
        .data_in_num = -1 
    };
    
    ret = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Completed driver installation");
    
    ret = i2s_set_pin(I2S_PORT, &pin_config);
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Completed pin configuration");
    
    i2s_zero_dma_buffer(I2S_PORT);

    return ret;
}

/*esp_err_t i2s_audio_out(char *data, size_t size)
{
	uint32_t bytes_written = 0;
	esp_err_t res = ESP_FAIL;
	uint32_t data_output_len = 2;
	uint32_t sum = 0;

	ESP_LOGI(TAG, "\n *** Starting Audio Output *** ");

	for(size_t i=0; i<size; i++)
	{
		ESP_LOGI(TAG, "Data Address: %p", data);
		ESP_LOGI(TAG, "Data: %x", *(uint16_t *)data);
		res = i2s_write(I2S_PORT, data, data_output_len, &bytes_written, portMAX_DELAY);
		if (res != ESP_OK){
			ESP_LOGE(TAG, "Failed to output data");
			return res;
		}

		sum += bytes_written;
		if(sum >= 16){
			data = data - (sum - data_output_len);
			sum = 0;
		}
		else{
			data += bytes_written;
		}

		ESP_LOGI(TAG, "Outputted %d bytes of audio data", bytes_written);
		ESP_LOGI(TAG, "Total bytes_written: %d", sum);
		ESP_LOGI(TAG, " ---- Completed %d Cycle out of %d ---- \n", i, size);
		
	}
	ESP_LOGI(TAG, " *** Audio Output Complete *** ");
	return res;
}*/

esp_err_t audio_out(uint8_t *data, size_t *audio_size)
{   
    uint32_t size = *(uint32_t *)audio_size;
    //uint32_t remain_data = size;
    uint32_t bytes_written = 0;
    esp_err_t res = ESP_FAIL;
    uint32_t data_output_sum = 0;

    ESP_LOGI(TAG, "\n *** Starting Audio Output *** ");
    ESP_LOGI(TAG, "Data base address: 0x%p", (void *)data);

    /*while(data_output_sum < size)
    {
		if(remain_data < MAX_DMA_LEN)
			res = i2s_write(I2S_PORT, data, remain_data, &bytes_written, portMAX_DELAY);
		else
			res = i2s_write(I2S_PORT, data, MAX_DMA_LEN, &bytes_written, portMAX_DELAY);

		if (res != ESP_OK){
				ESP_LOGE(TAG, "Failed to output audio");
				return res;
		}
		
		data_output_sum += bytes_written;
		data += bytes_written;

		if ((data_output_sum % 1000) == 0){
			ESP_LOGI(TAG, "Outputted %d bytes of audio data", bytes_written);
			ESP_LOGI(TAG, "---------------Audio Output %u%%---------------", data_output_sum * 100 / size);
		}
		
		remain_data = size - data_output_sum;
    }*/

    res = i2s_write(I2S_PORT, data, size, &bytes_written, portMAX_DELAY);
    ESP_LOGI(TAG, " *** Audio Output Complete *** \n");

    ESP_LOGI(TAG, "Data base address after completion: 0x%p", (void *)data);
    data -= data_output_sum;
    ESP_LOGI(TAG, "Data base address after removing offset: 0x%p", (void *)data);

    return res;
}

esp_err_t audio_receive(uint8_t *data, size_t audio_size, int sock)
{

	uint8_t *buf  = (uint8_t *)heap_caps_malloc(MAX_DMA_LEN * sizeof(uint8_t), MALLOC_CAP_8BIT);
	if(!buf){
		ESP_LOGE(WIFI_TAG, "\t\t\t\tMemory allocation for i2s buffer failed");
		return ESP_FAIL;
	}

	ESP_LOGI(WIFI_TAG, "\t\t\t\t *** Audio Transfer Start *** ");
	
	/*uint32_t *audio_size = malloc(4);
	ESP_LOGI(TAG, "Reading audio size...");
	int retsize = read(sock, audio_size, sizeof(uint32_t));
	if(retsize < 0){
		ESP_LOGE(TAG, "Error in receiving audio size");
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Audio size (hex/decimal): 0x%08x/%u", *audio_size, *audio_size);*/
	
	uint32_t size = (uint32_t)audio_size;
	uint32_t diff = (uint32_t)audio_size;
	size_t data_r_size = 0;
	int retsize;
	while(data_r_size < size)
	{
		ESP_LOGI(WIFI_TAG, "\t\t\t\tReading audio data.....");
		if(diff > MAX_DMA_LEN){
			ESP_LOGI(WIFI_TAG, "\t\t\t\tAudio transfer of %d bytes into %dB buffer.....", MAX_DMA_LEN, MAX_DMA_LEN * sizeof(char));
			retsize = read(sock, (void *)buf, MAX_DMA_LEN);
		}
		else{
			ESP_LOGI(WIFI_TAG, "\t\t\t\tAudio transfer of %d bytes into %dB buffer.....", diff, MAX_DMA_LEN * sizeof(char));
			retsize = read(sock, (void *)buf, diff);
		}
			
		if(retsize < 0){
			ESP_LOGE(WIFI_TAG, "\t\t\t\tError in reading audio data");
			return ESP_FAIL;
		}
		
		ESP_LOGI(WIFI_TAG, "\t\t\t\tCopying data from buffer to memory.....");
		memcpy(data, buf, retsize);
		
		data += retsize;
		data_r_size += retsize;
		ESP_LOGI(WIFI_TAG, "\t\t\t\t---------------Audio Transfer %u%%---------------", data_r_size * 100 / size);
		
		diff = size - data_r_size;
	}
	ESP_LOGI(WIFI_TAG, "\t\t\t\t *** Audio Transfer End *** \n");

	// Reset address to beginning of data
	data -= size;

	unsigned char doneString[5] = {'d', 'o', 'n', 'e', '\0'};
	if (write(sock, doneString, 5) < 0){
		ESP_LOGE(WIFI_TAG, "\t\t\t\tError during transfer of DONE string");
		return ESP_FAIL;
	}
	
	/*for (int i=0; i<1; i++){	
		ESP_LOGI(TAG, "--------Audio Cycle %d---------", i);
		retsize = i2s_audio_out(data, &size);

		if(retsize != ESP_OK){
			ESP_LOGE(TAG, "Error during output of audio");
			return ESP_FAIL;
		}
		if (write(sock, doneString, 5) < 0){
			ESP_LOGE(TAG, "Error during transfer of DONE string");
			return ESP_FAIL;
		}
	}
	ESP_LOGI(TAG, "Completed Audio Cycles");*/
	
	free(buf);
	
	return ESP_OK;
}

esp_err_t app_speaker(uint8_t *data, size_t *audio_size, uint8_t *flag)
{
	esp_err_t ret = ESP_OK;
	*flag = 0x00;

	//UART TEST
	uart_word_length_t data_length = 0;
	uart_get_word_length(UART_NUM_0, &data_length);

	uart_stop_bits_t num_stop_bits = 0;
	uart_get_stop_bits(UART_NUM_0, &num_stop_bits);

	uart_parity_t parity_mode = 0;
	uart_get_parity(UART_NUM_0, &parity_mode);

	uint32_t baudrate = 0;
	uart_get_baudrate(UART_NUM_0, &baudrate);

	uart_hw_flowcontrol_t flow_control = 0;
	uart_get_hw_flow_ctrl(UART_NUM_0, &flow_control);
	//END OF TEST

	
	ESP_LOGI(TAG, "Initializing I2S communication with speaker amplifier\n");
	ret = i2s_init();

	ESP_LOGI(TAG, "Setting GPIO #1 on expander to HIGH for Left Channel Selection");
	if(ESP_OK == gpio_set_pin(0x02)){
		ESP_LOGI(TAG, "Finished write of GPIO register! Checking register...");
		uint8_t gpio_val;
		gpio_read_pin(&gpio_val);
		if((gpio_val & 0x02) == 0x02){
			ESP_LOGI(TAG, "Correct value for GPIO register! Register holds: 0x%02x", gpio_val);
		}else{
			ESP_LOGE(TAG, "Error: Incorrect value for GPIO register! Register holds: 0x%02x", gpio_val);
			ret = ESP_FAIL;
			*flag = 0x02; // Flag to indicate gpio output failed
			goto RESET;
		}
	}else{
		ESP_LOGE(TAG, "Error: Could not complete write of GPIO register\n");
		ret = ESP_FAIL;
		*flag = 0x01; //Indicates initializtion failed
		goto RESET;
	}

	//Outputting audio on speaker
	if(audio_out(data, audio_size) != ESP_OK){
		ESP_LOGE(TAG, "Failed to output audio on speaker");
		ret = ESP_FAIL;
		*flag = 0x03; //Indicates audio out failed
	}


	RESET: ESP_LOGI(TAG, "Resetting GPIO #1 to LOW");
	if (ESP_OK == gpio_set_pin(0x00)){
		ESP_LOGI(TAG, "Write complete!");
	}else{
		ESP_LOGE(TAG, "Error: Write of GPIO register incomplete");
		*flag = 0x04;
	}

	i2s_driver_uninstall(I2S_PORT);

	//Resetting GPIO pins 1 and 3 on ESP32 so UART communication resumes
	gpio_reset_pin(1);
	gpio_reset_pin(3);

	//RESET OF UART TEST
	uart_config_t uart_config = {
		.baud_rate = baudrate,
		.data_bits = data_length,
		.parity = parity_mode,
		.stop_bits = num_stop_bits,
		.flow_ctrl = flow_control,
		.rx_flow_ctrl_thresh = 122,
	};

	ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART_NUM_0_TXD_DIRECT_GPIO_NUM, UART_NUM_0_RXD_DIRECT_GPIO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	/*int uart_buffer_size = 1024 * 2;
	QueueHandle_t uart_queue;
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0));*/
	//END OF TEST

	return ret;
}
