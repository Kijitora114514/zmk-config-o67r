/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zmk/display/status_screen.h>

#define SCREEN_SIZE 240

extern const lv_image_dsc_t disp;

static lv_color_t rgb_to_gbr(const lv_color_filter_dsc_t *filter, lv_color_t color,
                             lv_opa_t opacity) {
    LV_UNUSED(filter);
    LV_UNUSED(opacity);

    return (lv_color_t){
        .red = color.green,
        .green = color.blue,
        .blue = color.red,
    };
}

lv_obj_t *zmk_display_status_screen(void) {
    static lv_color_filter_dsc_t rgb_to_gbr_filter;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *image = lv_image_create(screen);
    lv_color_filter_dsc_init(&rgb_to_gbr_filter);
    rgb_to_gbr_filter.filter_cb = rgb_to_gbr;
    lv_obj_set_style_color_filter_dsc(image, &rgb_to_gbr_filter, LV_PART_MAIN);
    lv_obj_set_style_color_filter_opa(image, LV_OPA_COVER, LV_PART_MAIN);
    lv_image_set_src(image, &disp);
    lv_obj_center(image);

    return screen;
}
