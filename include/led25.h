#ifndef LED25_H
#define LED25_H
/* ----------------------------------------------------- */
#include "pico/stdlib.h"
#include <stdbool.h>

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#ifndef LED_DELAY_MS
#define LED_DELAY_MS 500
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* --- LED 初期化 --- */
extern int led_init(void);
/* toggle on/off */
extern void led_sw(void);
/* led on/off */
extern void led_onoff(bool flag);

#ifdef __cplusplus
}
#endif /* __cplusplus */
/* ----------------------------------------------------- */
#endif //LED25_H