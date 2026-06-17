/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/layer_status.h>

static struct zmk_widget_layer_status layer_status_widget;

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    zmk_widget_layer_status_init(&layer_status_widget, screen);

    lv_obj_t *layer_label = zmk_widget_layer_status_obj(&layer_status_widget);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_align(layer_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, 0);

    return screen;
}
