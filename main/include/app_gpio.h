#ifndef _APP_GPIO_H_
#define _APP_GPIO_H_

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gpio_read_reg(uint8_t reg, uint8_t *data);

esp_err_t gpio_write_reg(uint8_t reg, uint8_t comd);

esp_err_t gpio_set_pin(uint8_t comd);

esp_err_t gpio_read_pin(uint8_t *gpio_val);

void gpio_expander_init();

#ifdef __cplusplus
}
#endif

#endif /* _APP_GPIO_H_ */
