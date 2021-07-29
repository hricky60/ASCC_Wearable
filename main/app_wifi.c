#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "tcpip_adapter.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

// Constant variables initialized
#define HOST_IP_ADDR "192.168.90.246"
#define PORT 3333

static const char *WIFI_TAG = "app_wifi";

const int MAX_WIFI_LEN = 100000;


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
