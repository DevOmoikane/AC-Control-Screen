#ifndef AC_UI_BRIDGE_H
#define AC_UI_BRIDGE_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t temperature_c;   /* 17..30 deg C */
    uint8_t power;           /* 0/1 */
    uint8_t mode;            /* 0 auto, 1 cool, 2 dry, 3 fan, 4 heat */
    uint8_t fan;             /* 0 auto, 1 low, 2 medium, 3 high */
    uint8_t swing, eco, turbo, display_led, sleep;
} ac_ui_state_t;

/* Firmware hooks: provide STRONG definitions in your main firmware source.
 * Weak no-op defaults are provided by actions.c to allow standalone builds. */
void ac_ui_send_state(const ac_ui_state_t *state);
void ac_ui_start_pairing(void);
void ac_ui_request_status(void);

/* Call these on LVGL/UI thread after receiving ESP-NOW/Wi-Fi messages. */
void ac_ui_set_link_status(int online, const char *message);
void ac_ui_set_mqtt_status(int online);
void ac_ui_apply_remote_state(const ac_ui_state_t *state);
const ac_ui_state_t *ac_ui_current_state(void);

#ifdef __cplusplus
}
#endif
#endif
