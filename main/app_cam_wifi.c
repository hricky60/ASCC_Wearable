#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_camera.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "tcpip_adapter.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "app_camera.h"
#include "app_speaker.h"
#include "app_mic.h"

// Constant variables initialized
#define HOST_IP_ADDR "192.168.1.144"
#define PORT 3333

#define IMAGE_NUM 10

static const char *CAM_TAG = "app_cam";
static const char *WIFI_TAG = "app_wifi";

void camera_capture(uint8_t *fb, size_t *fs){
	
	camera_fb_t *single_fb;
	size_t single_fs;
	size_t offset = 0;
	
	ESP_LOGI(CAM_TAG, "*** Preparing to capture %d JPG frames ***", IMAGE_NUM);
	
	for(int i = 0; i < IMAGE_NUM; i++){
		
		if(i == 0){
			single_fb = esp_camera_fb_get();
			if(!single_fb)
				ESP_LOGE(CAM_TAG, "Camera capture failed");
				
			ESP_LOGI(CAM_TAG, "Captured Num. %d Frame", i);
			
			memcpy(fb, single_fb->buf, single_fb->len);
			memcpy(fs, &single_fb->len, sizeof(size_t));
		}
		else{
			single_fs = fs[i-1];
			offset += single_fs;
			
			single_fb = esp_camera_fb_get();
			if(!single_fb)
				ESP_LOGE(CAM_TAG, "Camera capture failed");
				
			ESP_LOGI(CAM_TAG, "Captured Num. %d Frame", i);
				
			memcpy((fb + offset), single_fb->buf, single_fb->len);
			
			fs[i] = single_fb->len;
		}	
		esp_camera_fb_return(single_fb);
	}
	
	ESP_LOGI(CAM_TAG, "*** Completed capture of %d JPG frames ***", IMAGE_NUM);
}

int socket_send(uint8_t *fb, size_t *fs, int sock)
{
	int64_t fr_start, fr_end;
	int res = -1;
	uint32_t offset = 0;
	char *cam_tag = "app_cam";
	
	ESP_LOGI(WIFI_TAG, "*** Preparing to send JPG frames ***");
	res = send(sock, cam_tag, 7, 0);
	if (res < 0){
		ESP_LOGE(WIFI_TAG, "Failed to send camera tag");
		return res;
	}
	ESP_LOGI(WIFI_TAG, "Sent %d bytes for camera tag \"%s\"", res, cam_tag);

	for(int i = 0; i < IMAGE_NUM; i++){
		
		fr_start = esp_timer_get_time();
		
		res = send(sock, &fs[i], sizeof(size_t), 0);
		ESP_LOGI(WIFI_TAG, "Sent JPG Num.%d byte size", i);
		ESP_LOGI(WIFI_TAG, "Byte size (decimal/hex): %zu/0x%08x", fs[i], (uint32_t)fs[i]);
		
		if(i == 0){
			res = send(sock, fb, fs[i], 0);
			ESP_LOGI(WIFI_TAG, "Sent %d bytes", res);
		}
		else{
			offset += (uint32_t)fs[i-1];
			res = send(sock, (fb + offset), fs[i], 0);
			ESP_LOGI(WIFI_TAG, "Sent %d bytes", res);
		}
			
		if(res < 0){
			ESP_LOGE(WIFI_TAG, "Error occurred during image sending: errno %d", errno);
			break;
		}
			
		fr_end = esp_timer_get_time();
		ESP_LOGI(WIFI_TAG, "JPG Num.%d: %u Bytes @ %u ms", i, (uint32_t)fs[i], (uint32_t)((fr_end - fr_start) / 1000));
	}
	ESP_LOGI(WIFI_TAG, " *** JPG Transmission Complete *** ");
	
	return res;
}

esp_err_t camera_task(int sock, uint8_t *fb, size_t *fs)
{
    char rx_buffer[128];
    int err = socket_send(fb, fs, sock);
    if(err < 0)
	return ESP_FAIL;

    int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
	
    if (len < 0)
        return ESP_FAIL;
    else {
	rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
	ESP_LOGI(CAM_TAG, "Received %d bytes:", len);
	ESP_LOGI(CAM_TAG, "Response: %s", rx_buffer);
	
	if(strstr(rx_buffer, "False") != NULL){
		ESP_LOGI(CAM_TAG, "A fall was NOT DETECTED");
	}
	else if(strstr(rx_buffer, "True") != NULL){
		ESP_LOGI(CAM_TAG, "A fall was DETECTED");
		
		err = app_speaker(sock);
		if (err != ESP_OK){
			ESP_LOGE(CAM_TAG, "Failed to output audio on speaker");
			return ESP_FAIL;
		}
		
		err = app_mic(sock);
		if (err != ESP_OK){
			ESP_LOGE(CAM_TAG, "Failed while microphone operation");
			return ESP_FAIL;
		}
	}
	else{
		ESP_LOGE(CAM_TAG, "Received unsatisfactory response from server");
		return ESP_FAIL;
	}
    }
		
    return ESP_OK;
}

esp_err_t tcp_initialize(int *sock){

    char addr_str[128];
    int addr_family;
    int ip_protocol;
    int err;
    int flag = 0;
    int count = 0;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    while(count < 100){
	count += 1;

	if (flag == 0)
        	*sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
	
        if (*sock < 0) {
            ESP_LOGE(WIFI_TAG, "Unable to create socket: errno %d", errno);
            continue;
   	}
	flag = 1;
        ESP_LOGI(WIFI_TAG, "Socket created, connecting to %s:%d", HOST_IP_ADDR, PORT);

	if (flag == 1)
        	err = connect(*sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0){
            ESP_LOGE(WIFI_TAG, "Socket unable to connect: errno %d", errno);
            continue;
        }
        ESP_LOGI(WIFI_TAG, "Successfully connected");

	return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t app_cam_wifi()
{
	esp_err_t res = ESP_FAIL;
	int sock;

	// Allocating buffers for images along with their respective sizes
	uint8_t *frame_buffer = (uint8_t *)heap_caps_malloc(IMAGE_NUM*62000*sizeof(uint8_t), MALLOC_CAP_SPIRAM); //Allocated enough space for 10 frames @ 61kB size
	size_t *frame_size    = (size_t *)heap_caps_malloc(sizeof(size_t)*IMAGE_NUM, MALLOC_CAP_8BIT);          //Allocated space for 10 size_t that are 2 bytes each

	if(!frame_buffer){
		ESP_LOGE(CAM_TAG, "Memory allocation for frame buffer returned NULL pointer");
		return ESP_FAIL;
	}
		
	if(!frame_size){
		ESP_LOGE(CAM_TAG, "Memory allocation for frame size buffer returned NULL pointer");
		return ESP_FAIL;
	}

	// Capture 10 images along with their respective sizes	
	camera_capture(frame_buffer, frame_size);
	
	/* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
	* Read "Establishing Wi-Fi or Ethernet Connection" section in
	* examples/protocols/README.md for more information about this function.
	*/
	ESP_ERROR_CHECK(example_connect());

	res = tcp_initialize(&sock);
	if (res != ESP_OK)
		ESP_LOGE(WIFI_TAG, "TCP connection failed");
	else {
		res = camera_task(sock, frame_buffer, frame_size);
		if (res != ESP_OK)
			ESP_LOGE(CAM_TAG, "Failed to send camera images");

		ESP_LOGI(WIFI_TAG, "Shutting down socket....");
		shutdown(sock, 0);
		close(sock);
	}

	ESP_ERROR_CHECK(example_disconnect());

	free(frame_buffer);
	free(frame_size);

	return res;
}
