#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_arc_changed(lv_event_t * e);
extern void action_arc_commit(lv_event_t * e);
extern void action_temp_down(lv_event_t * e);
extern void action_temp_up(lv_event_t * e);
extern void action_power_toggle(lv_event_t * e);
extern void action_cycle_mode(lv_event_t * e);
extern void action_cycle_fan(lv_event_t * e);
extern void action_toggle_swing(lv_event_t * e);
extern void action_toggle_eco(lv_event_t * e);
extern void action_toggle_turbo(lv_event_t * e);
extern void action_toggle_led(lv_event_t * e);
extern void action_toggle_sleep(lv_event_t * e);
extern void action_start_pairing(lv_event_t * e);
extern void action_retry_link(lv_event_t * e);
extern void action_go_main(lv_event_t * e);
extern void action_go_extras(lv_event_t * e);
extern void action_go_network(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/