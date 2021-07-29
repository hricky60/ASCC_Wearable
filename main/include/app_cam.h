#ifndef _APP_CAM_H_
#define _APP_CAM_H_

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_cam(uint8_t *fb, size_t *fs);

esp_err_t cam_send(uint8_t *fb, size_t *fs, int sock);

#ifdef __cplusplus
}
#endif

#endif /* _APP_CAM_WIFI_H_ */
