#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "actions.h"
#include "ui.h"
#include "screens.h"
#include "ac_ui_bridge.h"

#if defined(__GNUC__)
#define AC_WEAK __attribute__((weak))
#else
#define AC_WEAK
#endif

static ac_ui_state_t s_ac = {22, 1, 1, 0, 0, 0, 0, 1, 0};
static int s_link = 0;
static int s_mqtt = 0;
static char s_network_detail[64] = "Looking for ESP32-C3 transmitter";
static int16_t clamp_temp(int16_t t) { return t < 17 ? 17 : (t > 30 ? 30 : t); }
static const char *const modes[] = {"AUTO","COOL","DRY","FAN","HEAT"};
static const char *const fans[]  = {"AUTO","LOW","MED","HIGH"};

AC_WEAK void ac_ui_send_state(const ac_ui_state_t *state) { (void)state; }
AC_WEAK void ac_ui_start_pairing(void) {}
AC_WEAK void ac_ui_request_status(void) {}

const ac_ui_state_t *ac_ui_current_state(void) { return &s_ac; }

void ac_ui_set_link_status(int online, const char *message) {
    s_link = online != 0;
    if(message && message[0]) {
        snprintf(s_network_detail, sizeof(s_network_detail), "%s", message);
    } else {
        snprintf(s_network_detail, sizeof(s_network_detail), "%s",
                 online ? "IR transmitter responding" : "Looking for ESP32-C3 transmitter");
    }
}
void ac_ui_set_mqtt_status(int online) { s_mqtt = online != 0; }
void ac_ui_apply_remote_state(const ac_ui_state_t *state) {
    if (!state) return;
    s_ac = *state;
    s_ac.temperature_c = clamp_temp(s_ac.temperature_c);
    if(s_ac.mode > 4) s_ac.mode=1;
    if(s_ac.fan > 3) s_ac.fan=0;
}
static void emit(void) { ac_ui_send_state(&s_ac); }

/* EEZ Studio "native variable" getters/setters; generated screens.c uses these. */
int32_t get_var_temperature(void) { return s_ac.temperature_c; }
void set_var_temperature(int32_t v) { s_ac.temperature_c = clamp_temp((int16_t)v); }
/* The AC does not report its state back, so the button does not show one.
 * It is a plain power button, like on the original remote. */
const char *get_var_power_text(void) { return "Power"; }
const char *get_var_temp_text(void) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d\302\260C", (int)s_ac.temperature_c);
    return buf;
}
const char *get_var_mode_text(void) { return modes[s_ac.mode]; }
const char *get_var_fan_text(void) { return fans[s_ac.fan]; }
const char *get_var_swing_text(void) { return s_ac.swing ? "ON" : "OFF"; }
const char *get_var_eco_text(void) { return s_ac.eco ? "ON" : "OFF"; }
const char *get_var_turbo_text(void) { return s_ac.turbo ? "ON" : "OFF"; }
const char *get_var_led_text(void) { return s_ac.display_led ? "ON" : "OFF"; }
const char *get_var_sleep_text(void) { return s_ac.sleep ? "ON" : "OFF"; }
const char *get_var_link_badge(void) { return s_link ? "LINKED" : "OFFLINE"; }
const char *get_var_network_text(void) { return s_link ? "CONNECTED" : "OFFLINE"; }
const char *get_var_network_detail(void) { return s_network_detail; }
const char *get_var_mqtt_text(void) { return s_mqtt ? "CONNECTED" : "NOT CONFIGURED"; }

/* Arc VALUE_CHANGED updates the local setpoint, RELEASED transmits once. */
void action_arc_changed(lv_event_t *e) {
    lv_obj_t *arc = lv_event_get_target_obj(e);
    set_var_temperature(lv_arc_get_value(arc));
}
void action_arc_commit(lv_event_t *e) { (void)e; emit(); }
void action_temp_down(lv_event_t *e) { (void)e; set_var_temperature(s_ac.temperature_c - 1); emit(); }
void action_temp_up(lv_event_t *e) { (void)e; set_var_temperature(s_ac.temperature_c + 1); emit(); }
void action_power_toggle(lv_event_t *e) { (void)e; s_ac.power ^= 1; emit(); }
void action_cycle_mode(lv_event_t *e) { (void)e; s_ac.mode=(s_ac.mode+1)%5; emit(); }
void action_cycle_fan(lv_event_t *e) { (void)e; s_ac.fan=(s_ac.fan+1)%4; emit(); }
void action_toggle_swing(lv_event_t *e) { (void)e; s_ac.swing ^= 1; emit(); }
void action_toggle_eco(lv_event_t *e) { (void)e; s_ac.eco ^= 1; emit(); }
void action_toggle_turbo(lv_event_t *e) { (void)e; s_ac.turbo ^= 1; emit(); }
void action_toggle_led(lv_event_t *e) { (void)e; s_ac.display_led ^= 1; emit(); }
void action_toggle_sleep(lv_event_t *e) { (void)e; s_ac.sleep ^= 1; emit(); }
void action_start_pairing(lv_event_t *e) { (void)e; ac_ui_start_pairing(); }
void action_retry_link(lv_event_t *e) { (void)e; ac_ui_request_status(); }
void action_go_main(lv_event_t *e) { (void)e; loadScreen(SCREEN_ID_MAIN); }
void action_go_extras(lv_event_t *e) { (void)e; loadScreen(SCREEN_ID_EXTRAS); }
void action_go_network(lv_event_t *e) { (void)e; loadScreen(SCREEN_ID_NETWORK); }
