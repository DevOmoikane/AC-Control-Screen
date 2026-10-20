#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_EXTRAS = 2,
    SCREEN_ID_NETWORK = 3,
    _SCREEN_ID_LAST = 3
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *extras;
    lv_obj_t *network;
    lv_obj_t *header_divider_main;
    lv_obj_t *brand_name_main;
    lv_obj_t *brand_subline_main;
    lv_obj_t *link_status_main;
    lv_obj_t *power_button_main;
    lv_obj_t *power_button_label_main;
    lv_obj_t *dial_plate;
    lv_obj_t *temperature_arc;
    lv_obj_t *dial_caption;
    lv_obj_t *dial_temperature;
    lv_obj_t *dial_mode_hint;
    lv_obj_t *temperature_down;
    lv_obj_t *temperature_down_label;
    lv_obj_t *temperature_up;
    lv_obj_t *temperature_up_label;
    lv_obj_t *right_section;
    lv_obj_t *mode_card;
    lv_obj_t *mode_card_eyebrow;
    lv_obj_t *mode_card_value;
    lv_obj_t *mode_card_arrow;
    lv_obj_t *fan_card;
    lv_obj_t *fan_card_eyebrow;
    lv_obj_t *fan_card_value;
    lv_obj_t *fan_card_arrow;
    lv_obj_t *swing_card;
    lv_obj_t *swing_card_eyebrow;
    lv_obj_t *swing_card_value;
    lv_obj_t *swing_card_arrow;
    lv_obj_t *nav_line_main;
    lv_obj_t *tab_climate_main;
    lv_obj_t *tab_climate_label_main;
    lv_obj_t *tab_extras_main;
    lv_obj_t *tab_extras_label_main;
    lv_obj_t *tab_network_main;
    lv_obj_t *tab_network_label_main;
    lv_obj_t *header_divider_extras;
    lv_obj_t *brand_name_extras;
    lv_obj_t *brand_subline_extras;
    lv_obj_t *link_status_extras;
    lv_obj_t *power_button_extras;
    lv_obj_t *power_button_label_extras;
    lv_obj_t *extras_title;
    lv_obj_t *extras_subtitle;
    lv_obj_t *tile_eco;
    lv_obj_t *title_0;
    lv_obj_t *value_0;
    lv_obj_t *subtitle_0;
    lv_obj_t *tile_turbo;
    lv_obj_t *title_1;
    lv_obj_t *value_1;
    lv_obj_t *subtitle_1;
    lv_obj_t *tile_display_led;
    lv_obj_t *title_2;
    lv_obj_t *value_2;
    lv_obj_t *subtitle_2;
    lv_obj_t *tile_sleep;
    lv_obj_t *title_3;
    lv_obj_t *value_3;
    lv_obj_t *subtitle_3;
    lv_obj_t *nav_line_extras;
    lv_obj_t *tab_climate_extras;
    lv_obj_t *tab_climate_label_extras;
    lv_obj_t *tab_extras_extras;
    lv_obj_t *tab_extras_label_extras;
    lv_obj_t *tab_network_extras;
    lv_obj_t *tab_network_label_extras;
    lv_obj_t *header_divider_network;
    lv_obj_t *brand_name_network;
    lv_obj_t *brand_subline_network;
    lv_obj_t *link_status_network;
    lv_obj_t *power_button_network;
    lv_obj_t *power_button_label_network;
    lv_obj_t *network_title;
    lv_obj_t *network_desc;
    lv_obj_t *espnow_panel;
    lv_obj_t *espnow_label;
    lv_obj_t *espnow_value;
    lv_obj_t *espnow_description;
    lv_obj_t *ha_panel;
    lv_obj_t *ha_title;
    lv_obj_t *ha_value;
    lv_obj_t *ha_sub;
    lv_obj_t *pair_button;
    lv_obj_t *pair_button_label;
    lv_obj_t *retry_button;
    lv_obj_t *retry_button_label;
    lv_obj_t *nav_line_network;
    lv_obj_t *tab_climate_network;
    lv_obj_t *tab_climate_label_network;
    lv_obj_t *tab_extras_network;
    lv_obj_t *tab_extras_label_network;
    lv_obj_t *tab_network_network;
    lv_obj_t *tab_network_label_network;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_extras();
void tick_screen_extras();

void create_screen_network();
void tick_screen_network();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/