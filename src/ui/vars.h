#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_TEMPERATURE = 0,
    FLOW_GLOBAL_VARIABLE_POWER_TEXT = 1,
    FLOW_GLOBAL_VARIABLE_TEMP_TEXT = 2,
    FLOW_GLOBAL_VARIABLE_MODE_TEXT = 3,
    FLOW_GLOBAL_VARIABLE_FAN_TEXT = 4,
    FLOW_GLOBAL_VARIABLE_SWING_TEXT = 5,
    FLOW_GLOBAL_VARIABLE_ECO_TEXT = 6,
    FLOW_GLOBAL_VARIABLE_TURBO_TEXT = 7,
    FLOW_GLOBAL_VARIABLE_LED_TEXT = 8,
    FLOW_GLOBAL_VARIABLE_SLEEP_TEXT = 9,
    FLOW_GLOBAL_VARIABLE_LINK_BADGE = 10,
    FLOW_GLOBAL_VARIABLE_NETWORK_TEXT = 11,
    FLOW_GLOBAL_VARIABLE_NETWORK_DETAIL = 12,
    FLOW_GLOBAL_VARIABLE_MQTT_TEXT = 13
};

// Native global variables

extern int32_t get_var_temperature();
extern void set_var_temperature(int32_t value);
extern const char *get_var_power_text();
extern void set_var_power_text(const char *value);
extern const char *get_var_temp_text();
extern void set_var_temp_text(const char *value);
extern const char *get_var_mode_text();
extern void set_var_mode_text(const char *value);
extern const char *get_var_fan_text();
extern void set_var_fan_text(const char *value);
extern const char *get_var_swing_text();
extern void set_var_swing_text(const char *value);
extern const char *get_var_eco_text();
extern void set_var_eco_text(const char *value);
extern const char *get_var_turbo_text();
extern void set_var_turbo_text(const char *value);
extern const char *get_var_led_text();
extern void set_var_led_text(const char *value);
extern const char *get_var_sleep_text();
extern void set_var_sleep_text(const char *value);
extern const char *get_var_link_badge();
extern void set_var_link_badge(const char *value);
extern const char *get_var_network_text();
extern void set_var_network_text(const char *value);
extern const char *get_var_network_detail();
extern void set_var_network_detail(const char *value);
extern const char *get_var_mqtt_text();
extern void set_var_mqtt_text(const char *value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/