#ifndef _APP_MIC_H_
#define _APP_MIC_H_

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t mic_flag;

esp_err_t mic_send(int sock, uint8_t *buf);

esp_err_t app_mic(uint8_t *i2s_read_data, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* _APP_MIC_H_ */
