#ifndef _APP_SPEAKER_H_
#define _APP_SPEAKER_H_

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_receive(uint8_t *data, size_t audio_size, int sock);

esp_err_t app_speaker(uint8_t *data, size_t *audio_size, uint8_t *flag);

#ifdef __cplusplus
}
#endif

#endif /* _APP_SPEAKER_H_ */
