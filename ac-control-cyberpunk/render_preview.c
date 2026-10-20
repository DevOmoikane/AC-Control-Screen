/* Render the generated EEZ/LVGL UI on the host.
 *
 * Used by render-preview.sh: it drives LVGL with a memory-backed display and
 * captures what the ILI9341 would receive, so the UI can be checked without
 * hardware. The pixel path mirrors the firmware's dispFlush(): RGB565 in
 * memory order, no byte swap.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"
#include "screens.h"

#define W 320
#define H 240

static uint16_t panel[W * H];
static lv_color_t buf[W * 21];
static uint32_t tick = 0;

static uint32_t host_tick(void) { return ++tick; }

static void flush_cb(lv_display_t * d, const lv_area_t * a, uint8_t * px) {
    const uint32_t w = a->x2 - a->x1 + 1;
    const uint32_t h = a->y2 - a->y1 + 1;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t * p = px + (y * w + x) * 2;
            panel[(a->y1 + y) * W + (a->x1 + x)] = (uint16_t)(p[0] | (p[1] << 8));
        }
    }
    lv_display_flush_ready(d);
}

static void write_ppm(const char * path) {
    FILE * f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t v = panel[i];
        fputc(((v >> 11) & 0x1F) << 3, f);
        fputc(((v >> 5) & 0x3F) << 2, f);
        fputc((v & 0x1F) << 3, f);
    }
    fclose(f);
}

int main(int argc, char ** argv) {
    const char * out = argc > 1 ? argv[1] : "preview.ppm";

    lv_init();
    lv_tick_set_cb(host_tick);

    lv_display_t * disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ui_init();
    ui_tick();
    for (int i = 0; i < 40; i++) { lv_timer_handler(); tick += 20; }
    lv_refr_now(disp);

    write_ppm(out);
    printf("wrote %s\n", out);
    return 0;
}
