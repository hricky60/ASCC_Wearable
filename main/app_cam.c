#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "app_camera.h"

static const char *CAM_TAG = "app_cam";
static const char *WIFI_TAG = "app_wifi";

const int IMAGE_NUM = 10;


esp_err_t app_cam(uint8_t *fb, size_t *fs, int img_num){
	
	camera_fb_t *single_fb;
	size_t single_fs;
	size_t offset = 0;
	
	ESP_LOGI(CAM_TAG, "*** Preparing to capture %d JPG frames ***", img_num);
	
	for(int i = 0; i < img_num; i++){
		
		if(i == 0){
			single_fb = esp_camera_fb_get();
			if(!single_fb){
				ESP_LOGE(CAM_TAG, "Camera capture failed");
				return ESP_FAIL;
			}
				
			ESP_LOGI(CAM_TAG, "Captured Num. %d Frame", i);
			
			memcpy(fb, single_fb->buf, single_fb->len);
			memcpy(fs, &single_fb->len, sizeof(size_t));
		}
		else{
			single_fs = fs[i-1];
			offset += single_fs;
			
			single_fb = esp_camera_fb_get();
			if(!single_fb){
				ESP_LOGE(CAM_TAG, "Camera capture failed");
				return ESP_FAIL;
			}
				
			ESP_LOGI(CAM_TAG, "Captured Num. %d Frame", i);
				
			memcpy((fb + offset), single_fb->buf, single_fb->len);
			
			fs[i] = single_fb->len;
		}	
		esp_camera_fb_return(single_fb);
	}
	
	ESP_LOGI(CAM_TAG, "*** Completed capture of %d JPG frames ***", img_num);
	return ESP_OK;
}

esp_err_t cam_send(uint8_t *fb, size_t *fs, int sock)
{
	int64_t fr_start, fr_end;
	int res = -1;
	uint32_t offset = 0;
	char *cam_tag = "app_cam";
	
	ESP_LOGI(WIFI_TAG, "\t\t\t\t *** Preparing to send JPG frames *** ");
	res = send(sock, cam_tag, 7, 0);
	if (res < 0){
		ESP_LOGE(WIFI_TAG, "\t\t\t\tFailed to send camera tag");
		return ESP_FAIL;
	}
	ESP_LOGI(WIFI_TAG, "\t\t\t\tSent %d bytes for camera tag \"%s\"", res, cam_tag);

	for(int i = 0; i < IMAGE_NUM; i++){
		
		fr_start = esp_timer_get_time();
		
		res = send(sock, &fs[i], sizeof(size_t), 0);
		ESP_LOGI(WIFI_TAG, "\t\t\t\tSent JPG Num.%d byte size", i);
		ESP_LOGI(WIFI_TAG, "\t\t\t\tByte size (decimal/hex): %zu/0x%08x", fs[i], (uint32_t)fs[i]);
		
		if(i == 0){
			res = send(sock, fb, fs[i], 0);
			ESP_LOGI(WIFI_TAG, "\t\t\t\tSent %d bytes", res);
		}
		else{
			offset += (uint32_t)fs[i-1];
			res = send(sock, (fb + offset), fs[i], 0);
			ESP_LOGI(WIFI_TAG, "\t\t\t\tSent %d bytes", res);
		}
			
		if(res < 0){
			ESP_LOGE(WIFI_TAG, "\t\t\t\tError occurred during image sending: errno %d", errno);
			return ESP_FAIL;
		}
			
		fr_end = esp_timer_get_time();
		ESP_LOGI(WIFI_TAG, "\t\t\t\tJPG Num.%d: %u Bytes @ %u ms", i, (uint32_t)fs[i], (uint32_t)((fr_end - fr_start) / 1000));

		if((i % 4) == 0){
			ESP_LOGI(WIFI_TAG, "\t\t\t\tResetting interrupt watchdog");
			esp_task_wdt_reset();
		}
	}
	ESP_LOGI(WIFI_TAG, "\t\t\t\t *** JPG Transmission Complete *** ");
	
	return ESP_OK;
}