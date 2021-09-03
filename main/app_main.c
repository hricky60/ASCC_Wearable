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
#include "esp_event.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "nvs_flash.h"

#include "tcpip_adapter.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "app_adxl345.h"
#include "app_camera.h"
#include "app_gpio.h"
#include "app_mic.h"
#include "app_cam.h"
#include "app_speaker.h"
#include "app_wifi.h"

#define ACCEL_POWER_CTL_REGISTER	0x2d

#define WIFI_SEND					0x00
#define WIFI_RECEIVE				0x01
#define HEARTBEAT 					0x10
#define CAM_TYPE					0x02
#define MIC_TYPE					0x04
#define SPEAKER_TYPE				0x08

extern int RECORD_SIZE;
extern int IMAGE_NUM;
extern int MAX_WIFI_LEN;
extern int MAX_AUDIO_SIZE;

static const char *TASK_TAG   = "app_task";
static const char *WIFI_TAG   = "app_wifi";
static const char *ADX_TAG = "app_adxl";
static const char *MIC_TAG = "app_mic";



typedef struct {
	uint8_t  send_receive;	//Determine if sending or receiving data
	uint8_t *data_pointer;	//Pointer that holds address of start of data
	size_t   data_size;	    //Size of data
} MicData;

typedef struct {
	uint8_t  send_receive;
	uint8_t *data_pointer;
	size_t  *data_size;
} CamData;



xQueueHandle xqueueADXtoWIFI; 	//Queue to communicate from ADXL345 task to Wifi task
xQueueHandle xqueueMICtoWIFI;	//Queue to communicate from Microphone task to Wifi task
xQueueHandle xqueueWIFItoADX;	//Queue to communicate from Wifi to ADXL345
xQueueHandle xqueueWIFItoMIC;	//Queue to communicate from Wifi to Mic

TaskHandle_t xTaskWifi;		//Handle of task 0 which is wifi task
TaskHandle_t xTaskADXL345;	//Handle of task 1 which is adxl345 task
TaskHandle_t xTaskMic;		//Handle of task 2 which is microphone task


uint8_t mic_flag = 0x00;


static void adxl345_task(void *pvParameters){

	BaseType_t xStatus;
	uint8_t speaker_flag;
	uint8_t doneArray[5] = {0x64, 0x6f, 0x6e, 0x65, 0x00}; //"done" in hex values

	const TickType_t xTicksToWait = pdMS_TO_TICKS(200);
	const TickType_t xTicksToWait4Response = pdMS_TO_TICKS(700);

	const CamData heartbeat_data = {
		.send_receive = HEARTBEAT,
	};

	CamData start_mes = {
		.data_pointer = malloc(sizeof(uint8_t)),
	};

	//Speaker struct
	CamData test_data = {
		.send_receive = WIFI_SEND|CAM_TYPE
	};

	//Mic struct
	CamData mic_data = {
		.send_receive = WIFI_SEND|MIC_TYPE,
		.data_size = malloc(sizeof(size_t))
	};
	mic_data.data_size = (size_t *)RECORD_SIZE;

	//Done struct for speaker server
	CamData done_data = {
		.send_receive = WIFI_SEND,
		.data_pointer = malloc(5),
		.data_size = malloc(sizeof(size_t))
	};
	done_data.data_pointer = doneArray;
	size_t done_size = 5;
	done_data.data_size = &done_size;

	//Confirmation struct which holds message from wifi task so data can be freed
	CamData confirm_mes = {
		.data_pointer = malloc(5),
		.data_size = malloc(sizeof(size_t))
	};

	//Response struct which receives "true" or "false" from server
	CamData response_data = {
		.send_receive = WIFI_RECEIVE,
		.data_pointer = (uint8_t *)heap_caps_malloc(6*sizeof(uint8_t), MALLOC_CAP_8BIT), //6 bytes to fit 5 characters plus null byte
		.data_size = (size_t *)heap_caps_malloc(sizeof(size_t), MALLOC_CAP_8BIT),
	};
	*response_data.data_size = 5;

	//Speaker struct
	CamData speaker_data = {
		.send_receive = WIFI_RECEIVE|SPEAKER_TYPE,
		.data_size = malloc(sizeof(size_t)),
	};

	//Response struct which is used after mic transfer which determines if a response from server is going to occur
	CamData mic_response = {
		.send_receive = WIFI_RECEIVE,
		.data_pointer = (uint8_t *)heap_caps_malloc(sizeof(uint8_t), MALLOC_CAP_8BIT),
		.data_size = malloc(sizeof(size_t))
	};
	*mic_response.data_size = 1;

	MicData mic_response1 = {
		.data_pointer = malloc(sizeof(uint8_t)),
		.data_size = 1,
	};


	ESP_LOGI(ADX_TAG, "ADXL345 Task is running on: %d", xPortGetCoreID());

	//Initialize Accelerometer	
	adxl345_init();

	//Initialize Camera
	app_camera_main();
	
	// Buffer of angles and magnitude for future use
	double_t alpha[100], beta[100], gamma[100];
	float_t mag[100];

	WAIT: if(xQueueReceive(xqueueWIFItoADX, &start_mes, xTicksToWait) != pdPASS){
		ESP_LOGI(ADX_TAG, "Waiting on Wifi connection...");
		vTaskDelay(200);
		goto WAIT;
	}else if(*start_mes.data_pointer != 0x01){
		ESP_LOGI(ADX_TAG, "Error: Waiting on Wifi connection...");
		goto WAIT;
	}
	
	esp_err_t flag = ESP_OK;
	int i = 0;
	LOOP: while(flag == ESP_OK){

		mag_and_dir(&alpha[i], &beta[i], &gamma[i], &mag[i]);
		ESP_LOGI(ADX_TAG, "X:%02.2f Y:%02.2f Z:%02.2f Mag:%02.2f", alpha[i], beta[i], gamma[i], mag[i]);

		if(mag[i] > 1.5){
			mic_flag = 0x01;

			test_data.data_pointer = (uint8_t *)heap_caps_malloc(IMAGE_NUM*32000*sizeof(uint8_t), MALLOC_CAP_SPIRAM); //Allocated enough space for IMAGE_NUM frames @ 32kB size
			test_data.data_size = (size_t *)heap_caps_malloc(sizeof(size_t)*IMAGE_NUM, MALLOC_CAP_8BIT);          //Allocated space for IMAGE_NUM size_t that are 2 bytes each

			if(!test_data.data_pointer){
				ESP_LOGE(ADX_TAG, "Memory allocation for frame buffer returned NULL pointer");
				goto LOOP;
			}
			if(!test_data.data_size){
				ESP_LOGE(ADX_TAG, "Memory allocation for frame size buffer returned NULL pointer");
				goto LOOP;
			}
			
			// Initialize camera and run camera code
			flag = app_cam(test_data.data_pointer, test_data.data_size, IMAGE_NUM);
			xStatus = xQueueSendToFront(xqueueADXtoWIFI, &test_data, xTicksToWait);
			vTaskDelay(200);

			// Check flag to suspend Mic Task for ADX task top priority
			if(mic_flag == 0x02){
				ESP_LOGI(ADX_TAG, "Suspending Mic Task");
				vTaskSuspend(xTaskMic);
				mic_flag = 0x03;
			}

			// Confirm that the images have completed transmission and allocation can be freed
			CONFIRM: xStatus = xQueueReceive(xqueueWIFItoADX, &confirm_mes, xTicksToWait4Response);
			if(xStatus != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive confirmation of transmission from WIFI task");
				goto CONFIRM;
			}
			else {
				free(test_data.data_pointer);
				free(test_data.data_size);
				if(confirm_mes.send_receive == 0xff){
					ESP_LOGE(ADX_TAG, "Received error from wifi task");
					goto END;
				}else if(strstr((char *)confirm_mes.data_pointer, "done") != NULL)
					ESP_LOGI(ADX_TAG, "Received confirmation of transmission from WIFI task");
				else{
					ESP_LOGE(ADX_TAG, "Transmission error");
					goto END;
				}
			}

			// Check flag to suspend Mic Task for ADX task top priority
			if(mic_flag == 0x02){
				ESP_LOGI(ADX_TAG, "Suspending Mic Task");
				vTaskSuspend(xTaskMic);
				mic_flag = 0x03;
			}

			// Sending to wifi task a request to read response from server; "true" or "false"
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &response_data, xTicksToWait);
			vTaskDelay(200);
			RESPONSE: if(xQueueReceive(xqueueWIFItoADX, &response_data, xTicksToWait4Response) != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive response from server");
				goto RESPONSE;
			}
			else if(response_data.send_receive == 0xff){
				ESP_LOGE(ADX_TAG, "Received error from wifi task");
				goto END;
			}
			ESP_LOGI(ADX_TAG, "Received \"%s\" response from server of size %zu", (char *)response_data.data_pointer, *(size_t *)response_data.data_size);
			if(strstr((char *)response_data.data_pointer, "True") != NULL){
				ESP_LOGI(ADX_TAG, "Fall was detected!");
			}
			else{
				ESP_LOGI(ADX_TAG, "False alarm. Images indicate no fall");
				goto END;
			}


			// Check flag to suspend Mic Task for ADX task top priority
			if(mic_flag == 0x02){
				ESP_LOGI(ADX_TAG, "Suspending Mic Task");
				vTaskSuspend(xTaskMic);
				mic_flag = 0x03;
			}


			// Sending wifi task a need to read speaker data
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &speaker_data, xTicksToWait);
			vTaskDelay(500);
			SPEAKER: if(xQueueReceive(xqueueWIFItoADX, &speaker_data, xTicksToWait4Response) != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive response from server");
				goto SPEAKER;
			}
			else if(speaker_data.send_receive == 0xff){
				ESP_LOGE(ADX_TAG, "Received error from wifi task");
				free(speaker_data.data_pointer);
				goto END;
			}
			ESP_LOGI(ADX_TAG, "Received speaker data for output");
			

			// Check flag to suspend Mic Task for ADX task top priority
			// Will wait indefintely until the Mic task can be suspended due to the Speaker using I2S as well as the Mic
			FLAGCHECK: if(mic_flag == 0x02){
				ESP_LOGI(ADX_TAG, "Suspending Mic Task");
				vTaskSuspend(xTaskMic);
			}
			else if(mic_flag == 0x03)
				ESP_LOGI(ADX_TAG, "Mic Task has been suspended");
			else{
				ESP_LOGI(ADX_TAG, "Waiting on Mic Task to finish");
				goto FLAGCHECK;
			}

			flag = app_speaker(speaker_data.data_pointer, speaker_data.data_size, &speaker_flag);
			free(speaker_data.data_pointer);
			if(flag == ESP_FAIL){
				ESP_LOGE(ADX_TAG, "Failed to output audio");
				ESP_LOGE(ADX_TAG, "Error number: 0x%x", speaker_flag);
				goto END;
			}

			// Sending "done" message to server. Necessary after audio output completion
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &done_data, xTicksToWait);

			//Begin recording response from user
			ESP_LOGI(ADX_TAG, "Starting to read in audio from mic");
			mic_data.data_pointer = (uint8_t *)heap_caps_malloc(RECORD_SIZE*sizeof(uint8_t), MALLOC_CAP_SPIRAM);
			ESP_LOGI(ADX_TAG, "Allocated space for the audio data");
			flag = app_mic(mic_data.data_pointer, mic_data.data_size);

			if(flag != ESP_OK){
				ESP_LOGI(ADX_TAG, "Failed to record audio");
				goto FREE;
			}

			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &mic_data, xTicksToWait);
			ESP_LOGI(ADX_TAG, "Struct transfer to WIFI task complete");
			vTaskDelay(500);

			CONFIRM0: xStatus = xQueueReceive(xqueueWIFItoADX, &confirm_mes, xTicksToWait);
			if(xStatus != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive confirmation of transmission from WIFI task");
				goto CONFIRM0;
			}
			else{
				if(confirm_mes.send_receive == 0xff)
					ESP_LOGE(ADX_TAG, "Received error from wifi task");
				else if(strstr((char *)confirm_mes.data_pointer, "done") != NULL)
					ESP_LOGI(ADX_TAG, "Received confirmation of transmission from WIFI task");
				else{
					ESP_LOGE(ADX_TAG, "Transmission error");
					goto END;
				}
			}

			FREE: free(mic_data.data_pointer);

			// Read from server a response struct which determines if there will be audio coming from server
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &mic_response, xTicksToWait);
			ESP_LOGI(ADX_TAG, "Mic response struct sent to WIFI task");
			vTaskDelay(200);

			MICRESPONSE: xStatus = xQueueReceive(xqueueWIFItoADX, &mic_response, xTicksToWait);
			if(xStatus != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive confirmation of transmission from WIFI task");
				goto MICRESPONSE;
			}
			else{
				if(mic_response.send_receive == 0xff)
					ESP_LOGE(ADX_TAG, "Received error from wifi task");
				else if(*mic_response.data_pointer != 0x01){
					ESP_LOGI(ADX_TAG, "No response needed from server");
					goto END;
				}
			}

			// Sending wifi task a need to read speaker data
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &speaker_data, xTicksToWait);
			vTaskDelay(500);

			SPEAKER1: if(xQueueReceive(xqueueWIFItoADX, &speaker_data, xTicksToWait4Response) != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive response from server");
				goto SPEAKER1;
			}
			else if(speaker_data.send_receive == 0xff){
				ESP_LOGE(ADX_TAG, "Received error from wifi task");
				free(speaker_data.data_pointer);
				goto END;
			}

			ESP_LOGI(ADX_TAG, "Received speaker data for output");
			flag = app_speaker(speaker_data.data_pointer, speaker_data.data_size, &speaker_flag);
			free(speaker_data.data_pointer);

			if(flag == ESP_FAIL){
				ESP_LOGE(ADX_TAG, "Failed to output audio");
				ESP_LOGE(ADX_TAG, "Error number: 0x%x", speaker_flag);
				goto END;
			}

			// Sending "done" message to server
			// Is needed after audio output completion
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &done_data, xTicksToWait);

			END:
			vTaskResume(xTaskMic);

			mic_flag = 0x00;
		}
		
		// Periodic blocking for lower priority tasks to run
		if(i == 50){
			xStatus = xQueueSendToBack(xqueueADXtoWIFI, &heartbeat_data, xTicksToWait);
			if(xStatus == pdPASS)
				ESP_LOGI(ADX_TAG, "Send of heartbeat_0 OK");
			
			vTaskDelay(5);	
		}

		// Queue to receive mic_response which indicates if to capture images
		xStatus = xQueueReceive(xqueueMICtoADX, &mic_response1, xTicksToWait);
		if(mic_response1 == 0x02 && xStatus == pdPASS){

			test_data.data_pointer = (uint8_t *)heap_caps_malloc(32000*sizeof(uint8_t), MALLOC_CAP_SPIRAM); //Allocated enough space for IMAGE_NUM frames @ 32kB size
			test_data.data_size = (size_t *)heap_caps_malloc(sizeof(size_t), MALLOC_CAP_8BIT);          //Allocated space for IMAGE_NUM size_t that are 2 bytes each

			if(!test_data.data_pointer){
				ESP_LOGE(ADX_TAG, "Memory allocation for frame buffer returned NULL pointer");
				goto LOOP;
			}
			if(!test_data.data_size){
				ESP_LOGE(ADX_TAG, "Memory allocation for frame size buffer returned NULL pointer");
				goto LOOP;
			}
			
			// Initialize camera and run camera code
			flag = app_cam(test_data.data_pointer, test_data.data_size, 1);
			xStatus = xQueueSendToFront(xqueueADXtoWIFI, &test_data, xTicksToWait);
			vTaskDelay(200);

			// Confirm that the images have completed transmission and allocation can be freed
			CONFIRM1: xStatus = xQueueReceive(xqueueWIFItoADX, &confirm_mes, xTicksToWait4Response);
			if(xStatus != pdPASS){
				ESP_LOGE(ADX_TAG, "Could not receive confirmation of transmission from WIFI task");
				goto CONFIRM1;
			}
			free(test_data.data_pointer);
			free(test_data.data_size);

			if(confirm_mes.send_receive == 0xff){
				ESP_LOGE(ADX_TAG, "Received error from wifi task");
			}else if(strstr((char *)confirm_mes.data_pointer, "done") != NULL)
				ESP_LOGI(ADX_TAG, "Received confirmation of transmission from WIFI task");
			else{
				ESP_LOGE(ADX_TAG, "Transmission error");
			}
		}
		
		i = (i + 1) % 100;

		vTaskDelay(10);
	}
	
	//Setting ADXL345 in Standby Mode
	ESP_LOGI(ADX_TAG, "Setting ADXL345 back to standby mode.....");
	if(ESP_OK == adxl345_write_reg(ACCEL_POWER_CTL_REGISTER, 0x00)){
		ESP_LOGI(ADX_TAG, "Finished write of power control register! Checking register for 0x00...");
		uint8_t power_ctl;
		adxl345_read_reg(ACCEL_POWER_CTL_REGISTER, &power_ctl);
		if(power_ctl == 0x00){
			ESP_LOGI(ADX_TAG, "Correct value for power control register! Register holds: 0x%02x", power_ctl);
		}else{
			ESP_LOGE(ADX_TAG, "Error: Incorrect value for power control register! Register holds: 0x%02x", power_ctl);
		}
	}
}

static void mic_task(void *pvParameters){

	BaseType_t xStatus;
	const TickType_t xTicksToWait = pdMS_TO_TICKS(200);
	const TickType_t xTicksToWait4Response = pdMS_TO_TICKS(3000);
	MicData confirm_mes;
	uint8_t doneArray[5] = {0x64, 0x6f, 0x6e, 0x65, 0x00}; //"done" in hex values

	const MicData heartbeat_data = {
		.send_receive = HEARTBEAT
	};

	MicData start_mes = {
		.data_pointer = malloc(sizeof(uint8_t)),
		.data_size = 1
	};

	MicData test_data = {
		.send_receive = WIFI_SEND|MIC_TYPE,
		.data_pointer = NULL,
		.data_size = RECORD_SIZE
	};

	MicData response_data = {
		.send_receive = WIFI_RECEIVE,
		.data_pointer = malloc(sizeof(char)),
		.data_size = sizeof(char)
	};

	MicData speaker_data = {
		.send_receive = WIFI_RECEIVE|SPEAKER_TYPE,
		.data_size = sizeof(size_t)
	};

	//Done struct for speaker server
	MicData done_data = {
		.send_receive = WIFI_SEND,
		.data_pointer = malloc(5),
		.data_size = 5
	};
	done_data.data_pointer = doneArray;

	ESP_LOGI(MIC_TAG, "\t\tMicrophone Task is running on: %d", xPortGetCoreID());

	vTaskDelay(50);

	//Initialize GPIO expander for switch use
	gpio_expander_init();

	WAIT1: if(xQueueReceive(xqueueWIFItoMIC, &start_mes, xTicksToWait) != pdPASS){
		ESP_LOGI(MIC_TAG, "\t\tWaiting on Wifi connection...");
		vTaskDelay(200);
		goto WAIT1;
	}else if(*start_mes.data_pointer != 0x01){
		ESP_LOGI(MIC_TAG, "\t\tError: Waiting on Wifi connection...");
		goto WAIT1;
	}

	esp_err_t flag = ESP_OK;
	int i = 0;
	uint8_t val = 0;
	while(flag == ESP_OK){

		//Read pin 1 from gpio expander
		flag = gpio_read_pin(&val);
		
		ESP_LOGI(MIC_TAG, "\t\tGPIO value: 0x%08x", val);

		//Check if pin 1 equal to 1 and if mic task isn't being asked to be suspended
		if((val & 0x01) == 0x01 && mic_flag == 0x00){

			//Before flow starts, check that mic task isn't being asked to be suspended
			if(mic_flag == 0x01){
				ESP_LOGI(MIC_TAG, "\t\tResetting Mic Task");
				goto FREE;
			}

			//Allocating data for microphone audio
			test_data.data_pointer = (uint8_t *)heap_caps_malloc(RECORD_SIZE*sizeof(uint8_t), MALLOC_CAP_SPIRAM);
			ESP_LOGI(MIC_TAG, "\t\tPointer address: %p", test_data.data_pointer);
			//Recording microphone audio
			flag = app_mic(test_data.data_pointer, &test_data.data_size);

			//If mic is to be suspended
			if(mic_flag == 0x01){
				ESP_LOGI(MIC_TAG, "\t\tResetting Mic Task");
				goto FREE;
			}
			
			//Sending Mic audio to wifi task
			xStatus = xQueueSendToBack(xqueueMICtoWIFI, &test_data, xTicksToWait);
			ESP_LOGI(MIC_TAG, "\t\tStruct transfer to WIFI task complete");
			vTaskDelay(200);
			//Receive confirmation from wifi task so mic data can be freed
			CONFIRM: xStatus = xQueueReceive(xqueueWIFItoMIC, &confirm_mes, xTicksToWait4Response);
			if(xStatus != pdPASS){
				ESP_LOGE(MIC_TAG, "\t\tCould not receive confirmation of transmission from WIFI task");
				goto CONFIRM;
			}
			else if(confirm_mes.send_receive == 0xff){
				ESP_LOGE(MIC_TAG, "\t\tReceived error from WIFI task");
				goto FREE;
			}
			//Confirmation message should hold "done"
			if(strstr((char *)confirm_mes.data_pointer, "done") != NULL)
				ESP_LOGI(MIC_TAG, "\t\tReceived confirmation of transmission from WIFI task");
			else
				ESP_LOGE(MIC_TAG, "\t\tTransmission error");

			FREE: free(test_data.data_pointer);

			// Check if task needs to be suspended
			if(mic_flag == 0x01){
				ESP_LOGI(MIC_TAG, "\t\tResetting Mic Task");
				goto END;
			}

			//Send response struct so a response can be transfered from server to indicate if speaker audio is about to be sent
			xStatus = xQueueSendToBack(xqueueMICtoWIFI, &response_data, xTicksToWait);
			ESP_LOGI(MIC_TAG, "\t\tSent response struct to WIFI task");
			vTaskDelay(200);
			RESPONSE: xStatus = xQueueReceive(xqueueWIFItoMIC, &response_data, xTicksToWait4Response);
			if(xStatus != pdPASS){
				ESP_LOGE(MIC_TAG, "\t\tCould not receive response data from WIFI task");
				goto RESPONSE;
			}
			else if(response_data.send_receive == 0xff){
				ESP_LOGE(MIC_TAG, "\t\tReceived error from WIFI task");
				goto END;
			}

			//If response is true then speaker audio needs to be transferred otherwise end this branch of code
			if(*response_data.data_pointer != 0x01){
				if(*response_data.data_pointer == 0x02){
					ESP_LOGI(MIC_TAG, "\t\tSending mic response to ADX task");
					xStatus = xQueueSend(xqueueMICtoADX, &response_data, xTicksToWait);
				}
				goto END;
			}

			//Send speaker struct to wifi task
			xStatus = xQueueSendToBack(xqueueMICtoWIFI, &speaker_data, xTicksToWait);
			ESP_LOGI(MIC_TAG, "\t\tSent speaker struct to WIFI task");
			vTaskDelay(500);
			//Receive speaker data from wifi task
			SPEAKER: xStatus = xQueueReceive(xqueueWIFItoMIC, &speaker_data, xTicksToWait4Response);
			if(xStatus != pdPASS){
				ESP_LOGE(MIC_TAG, "\t\tCould not receive speaker audio from WIFI task");
				goto SPEAKER;
			}
			else if(speaker_data.send_receive == 0xff){
				ESP_LOGE(MIC_TAG, "\t\tReceived error from WIFI task");
				goto END;
			}
			ESP_LOGI(MIC_TAG, "\t\tReceived speaker audio for WIFI task");

			//Output speaker data
			uint8_t speaker_flag;
			esp_err_t flag = app_speaker(speaker_data.data_pointer, &speaker_data.data_size, &speaker_flag);
			free(speaker_data.data_pointer);
			if(flag == ESP_FAIL){
				ESP_LOGE(MIC_TAG, "Failed to output audio");
				ESP_LOGE(MIC_TAG, "Error number: 0x%x", speaker_flag);
				goto END;
			}

			// Sending "done" message to server
			// Is needed after audio output completion
			xStatus = xQueueSendToBack(xqueueMICtoWIFI, &done_data, xTicksToWait);

			END: if(mic_flag == 0x01){
				ESP_LOGI(MIC_TAG, "\t\tSetting Mic Flag for suspension");
				mic_flag = 0x02;
			}
		}

		if(i == 50){
			xStatus = xQueueSendToBack(xqueueMICtoWIFI, &heartbeat_data, xTicksToWait);
			if(xStatus == pdPASS)
				ESP_LOGI(MIC_TAG, "\t\tSend of heartbeat_1 OK");
			
			vTaskDelay(5);
		}

		i = (i + 1) % 100;

		vTaskDelay(10);

		if(mic_flag == 0x01)
			mic_flag = 0x02;
	}
}

static void wifi_task(void *pvParameters){

	BaseType_t xStatus0, xStatus1;
	const TickType_t xTicksToWait0 = pdMS_TO_TICKS(20);
	const TickType_t xTicksToWait1 = pdMS_TO_TICKS(20);
	CamData data_received0;
	MicData data_received1;

	uint8_t doneArray[5] = {0x64, 0x6f, 0x6e, 0x65, 0x00}; //done in hex
	MicData confirm_mes1 = {
		.send_receive = 0x00,
		.data_pointer = doneArray,
		.data_size = 4,
	};

	size_t confirm_mes_size = 4;
	CamData confirm_mes0 = {
		.send_receive = 0x00,
		.data_pointer = doneArray,
		.data_size = &confirm_mes_size
	};

	uint8_t start_mes = 0x01;
	MicData start_mes1 = {
		.data_pointer = &start_mes,
	};

	CamData start_mes0 = {
		.data_pointer = &start_mes,
	};

	MicData error_mes1 = {
		.send_receive = 0xff
	};

	CamData error_mes0 = {
		.send_receive = 0xff
	};

	ESP_LOGI(WIFI_TAG, "\t\t\t\tWifi Task is running on: %d", xPortGetCoreID());

	int sock;
	ESP_ERROR_CHECK(example_connect());
	esp_err_t flag = tcp_initialize(&sock);

	xQueueSend(xqueueWIFItoADX, &start_mes0, xTicksToWait0);
	xQueueSend(xqueueWIFItoMIC, &start_mes1, xTicksToWait1);

	int res;
	//uint32_t size, bytes_sent, bytes_received;
	while(1){
		
		res = -1;
		//bytes_sent = 0;
		//bytes_received = 0;

		data_received0.send_receive = 0x44;
		data_received1.send_receive = 0x44;

		xStatus0 = pdFALSE;
		xStatus1 = pdFALSE;

		//ADXL345 Queue 
		xStatus0 = xQueueReceive(xqueueADXtoWIFI, &data_received0, xTicksToWait0);
		
		if(xStatus0 == pdPASS){

			ESP_LOGI(WIFI_TAG, "\t\t\t\tADXL345 Data received OK");
			ESP_LOGI(WIFI_TAG, "\t\t\t\tSend or Receive: 0x%08x", data_received0.send_receive);


			if((data_received0.send_receive & 0xf1) == WIFI_SEND){

				ESP_LOGI(WIFI_TAG, "\t\t\t\tPointer address: %p", data_received0.data_pointer);

				if((data_received0.send_receive & 0x0e) == CAM_TYPE){
			
					flag = cam_send(data_received0.data_pointer, data_received0.data_size, sock);

					if(flag == ESP_FAIL){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send camera images to server");
						goto ERROR;
					}
					
					ADXCONFIRM: if(xQueueSendToBack(xqueueWIFItoADX, &confirm_mes0, xTicksToWait0) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send confirmation of transmission to MIC task");
						goto ADXCONFIRM;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent confirmation of transmission to ADX task");
				}
				else if((data_received0.send_receive & 0x0e) == MIC_TYPE){
					ESP_LOGI(WIFI_TAG, "\t\t\t\tPointer address: %p", data_received0.data_pointer);
					//ESP_LOGI(WIFI_TAG, "\t\t\t\tData size: %d", *data_received0.data_size);

					flag = mic_send(sock, data_received0.data_pointer);
					if(flag == ESP_FAIL){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send microphone data to server");
						goto ERROR;
					}


					CONFIRM: if(xQueueSendToBack(xqueueWIFItoADX, &confirm_mes0, xTicksToWait0) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send confirmation of transmission to MIC task");
						goto CONFIRM;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent confirmation of transmission to ADX task");
				}
				else{
					ESP_LOGI(WIFI_TAG, "\t\t\t\tSending %zu bytes to server", *data_received0.data_size);
					res = send(sock, (void *)data_received0.data_pointer, *data_received0.data_size, 0);
					if(res <= 0)
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send data to server");
				}
			}
			else if((data_received0.send_receive & 0xf1) == WIFI_RECEIVE){

				if((data_received0.send_receive & 0x0e) == SPEAKER_TYPE){

					ESP_LOGI(WIFI_TAG, "\t\t\t\tReading size of data from server");
					res = read(sock, (void *)data_received0.data_size, sizeof(size_t));

					if(res <= 0){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to read from server");
						goto ERROR;
					}

					ESP_LOGI(WIFI_TAG, "\t\t\t\tSize of data to be read from server: %zu", *data_received0.data_size);
					res = send(sock, (void *)data_received0.data_size, sizeof(size_t), 0);
					ESP_LOGI(WIFI_TAG, "\t\t\t\tSent back the data size to server for confirmation");

					data_received0.data_pointer = (uint8_t *)heap_caps_malloc(MAX_AUDIO_SIZE * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
					if(!data_received0.data_pointer){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tMemory allocation for i2s data failed");
						goto ERROR;
					}

					flag = audio_receive(data_received0.data_pointer, *data_received0.data_size, sock);
					if(flag == ESP_FAIL){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to read audio data from server");
						goto ERROR;
					}

					ADXREAD2: if(xQueueSendToBack(xqueueWIFItoADX, &data_received0, xTicksToWait1) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send data from server to ADX task");
						goto ADXREAD2;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent data from server to ADX task");
				}
				else{

					ESP_LOGI(WIFI_TAG, "\t\t\t\tReading %d bytes from server", *data_received0.data_size);
					res = read(sock, data_received0.data_pointer, *data_received0.data_size);

					if(res <= 0){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to read from server");
						goto ERROR;
					}

					*data_received0.data_size = res;

					ADXREAD: if(xQueueSendToBack(xqueueWIFItoADX, &data_received0, xTicksToWait1) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send data from server to ADX task");
						goto ADXREAD;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent data from server to ADX task");
				} 
			}
			else if((data_received0.send_receive & 0xf0) == HEARTBEAT){

				ESP_LOGI(WIFI_TAG, "\t\t\t\tHeartbeat_0 received");
			}
		}
		else if(1==0){
			ERROR: ESP_LOGI(WIFI_TAG, "\t\t\t\tSending an error message to ADX task");
			if(xQueueSendToBack(xqueueWIFItoADX, &error_mes0, xTicksToWait1) != pdPASS){
				ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send error message to ADX task");
				goto ERROR;
			}else
				ESP_LOGI(WIFI_TAG, "\t\t\t\tSent error to ADX task");
		}

		//Microphone Queue
		xStatus1 = xQueueReceive(xqueueMICtoWIFI, &data_received1, xTicksToWait1);

		if(xStatus1 == pdPASS){

			ESP_LOGI(WIFI_TAG, "\t\t\t\tMIC Data received OK");
			ESP_LOGI(WIFI_TAG, "\t\t\t\tSend or Receive: 0x%08x", data_received1.send_receive);


			if((data_received1.send_receive & 0xf1) == WIFI_SEND){
				if((data_received1.send_receive & 0x0e) == MIC_TYPE){
					ESP_LOGI(WIFI_TAG, "\t\t\t\tPointer address and value: %p", data_received1.data_pointer);
					ESP_LOGI(WIFI_TAG, "\t\t\t\tData size: %d", data_received1.data_size);

					flag = mic_send(sock, data_received1.data_pointer);
					if(flag == ESP_FAIL){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send microphone data to server");
						goto ERROR1;
					}

					MICCONFIRM: if(xQueueSendToBack(xqueueWIFItoMIC, &confirm_mes1, xTicksToWait1) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send confirmation of transmission to MIC task");
						goto MICCONFIRM;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent confirmation of transmission to MIC task");
				}
				else{
					ESP_LOGI(WIFI_TAG, "\t\t\t\tSending %zu bytes to server", data_received1.data_size);
					res = send(sock, (void *)data_received1.data_pointer, data_received1.data_size, 0);
					if(res <= 0)
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send data to server");
				}
			}
			else if((data_received1.send_receive & 0xf1) == WIFI_RECEIVE){

				if((data_received1.send_receive & 0x0e) == SPEAKER_TYPE){

					ESP_LOGI(WIFI_TAG, "\t\t\t\tReading size of data from server");
					res = read(sock, (void *)&data_received1.data_size, sizeof(size_t));

					if(res <= 0){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to read from server");
						goto ERROR1;
					}

					ESP_LOGI(WIFI_TAG, "\t\t\t\tSize of data to be read from server: %zu", data_received1.data_size);
					res = send(sock, (void *)&data_received1.data_size, sizeof(size_t), 0);
					ESP_LOGI(WIFI_TAG, "\t\t\t\tSent back the data size to server for confirmation");

					data_received1.data_pointer = (uint8_t *)heap_caps_malloc(MAX_AUDIO_SIZE * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
					if(!data_received1.data_pointer){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tMemory allocation for i2s data failed");
						goto ERROR1;
					}

					flag = audio_receive(data_received1.data_pointer, data_received1.data_size, sock);
					if(flag == ESP_FAIL){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to read audio data from server");
						goto ERROR1;
					}

					MICREAD2: if(xQueueSendToBack(xqueueWIFItoMIC, &data_received1, xTicksToWait1) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send data from server to ADX task");
						goto MICREAD2;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent data from server to ADX task");
				}
				else{

					ESP_LOGI(WIFI_TAG, "\t\t\t\tReading %d bytes from server", data_received1.data_size);
					res = read(sock, data_received1.data_pointer, data_received1.data_size);

					if(res <= 0){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to read from server");
						goto ERROR1;
					}

					data_received1.data_size = res;

					MICREAD: if(xQueueSendToBack(xqueueWIFItoMIC, &data_received1, xTicksToWait1) != pdPASS){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send data from server to MIC task");
						goto MICREAD;
					}else
						ESP_LOGI(WIFI_TAG, "\t\t\t\tSent data from server to MIC task");
				}
			}
			else if((data_received1.send_receive & 0xf0) == HEARTBEAT){

				ESP_LOGI(WIFI_TAG, "\t\t\t\tHeartbeat_1 received");
			}
		}
		else if(1==0){
			ERROR1: ESP_LOGI(WIFI_TAG, "\t\t\t\tSending to MIC task an error message");
			if(xQueueSendToBack(xqueueWIFItoMIC, &error_mes1, xTicksToWait1) != pdPASS){
				ESP_LOGE(WIFI_TAG, "\t\t\t\tCould not send error message to MIC task");
				goto ERROR1;
			}else
				ESP_LOGI(WIFI_TAG, "\t\t\t\tSent error to MIC task");
		}

		vTaskDelay(5);
	}

	ESP_LOGI(WIFI_TAG, "\t\t\t\tShutting down socket....");
	shutdown(sock, 0);
	close(sock);

	ESP_ERROR_CHECK(example_disconnect());
	exit(EXIT_FAILURE);
}

void app_main(){
	
	ESP_LOGI(TASK_TAG, "****** Setup ******");

	ESP_ERROR_CHECK(nvs_flash_init());
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	xqueueADXtoWIFI = xQueueCreate(5, sizeof(CamData));
	xqueueMICtoWIFI = xQueueCreate(5, sizeof(MicData));

	xqueueWIFItoADX = xQueueCreate(5, sizeof(CamData));
	xqueueWIFItoMIC = xQueueCreate(5, sizeof(MicData));

	xqueueMICtoADX = xQueueCreate(5, sizeof(MicData));

	xTaskCreatePinnedToCore(wifi_task, "WifiTask", 10000, NULL, (configMAX_PRIORITIES-10), &xTaskWifi, 0);
	xTaskCreatePinnedToCore(adxl345_task, "Adxl345Task", 10000, NULL, (configMAX_PRIORITIES-1), &xTaskADXL345, 1);
	xTaskCreatePinnedToCore(mic_task, "MicTask", 10000, NULL, (configMAX_PRIORITIES-2), &xTaskMic, 1);

	ESP_LOGI(TASK_TAG, "Priority of Wifi Task: %d", uxTaskPriorityGet(xTaskWifi));
	ESP_LOGI(TASK_TAG, "Priority of Adxl345 Task: %d", uxTaskPriorityGet(xTaskADXL345));
	ESP_LOGI(TASK_TAG, "Priority of Microphone Task: %d", uxTaskPriorityGet(xTaskMic));

	if(xqueueADXtoWIFI != NULL && xqueueMICtoWIFI != NULL)
		ESP_LOGI(TASK_TAG, "****** Start of Tasks ******");

	ESP_LOGI(TASK_TAG, "********************************");
}







// Back up code for microphone socket transfer in wifi task

/*ESP_LOGI(WIFI_TAG, "\t\t\t\t*** Starting WAV Transmission ***");
				
				if(send(sock, MIC_TAG, 7, 0) <= 0){
					ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send MIC tag to server");
					break;
				}
				ESP_LOGI(WIFI_TAG, "\t\t\t\tSent MIC tag to server");

				if(send(sock, (void *)&data_received1.data_size, sizeof(size_t), 0) <= 0){
					ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send WAV size to server");
					break;
				}
				ESP_LOGI(WIFI_TAG, "\t\t\t\tSent WAV size of %dB to server", data_received1.data_size);

				size = data_received1.data_size;
				int count = 0;
				while(bytes_sent < size){
					if((size - bytes_sent) < MAX_WIFI_LEN)
						res = send(sock, data_received1.data_pointer, (size-bytes_sent), 0);
					else
						res = send(sock, data_received1.data_pointer, MAX_WIFI_LEN, 0);

					if(res <= 0){
						ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send WAV data");
						break;
					}

					ESP_LOGI(WIFI_TAG, "\t\t\t\tSent %d bytes of WAV data to server", res);

					bytes_sent += res;
					data_received1.data_pointer += res;

					if((count % 5) == 0 && count < 26){
						ESP_LOGI(WIFI_TAG, "\t\t\t\tResetting interrupt watchdog");
						esp_task_wdt_reset();
					}
					count += 1;
				}
			
				ESP_LOGI(WIFI_TAG, "\t\t\t\t*** WAV Transmission Complete ***");*/