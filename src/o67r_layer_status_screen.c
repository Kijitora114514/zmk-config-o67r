/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zmk/display/status_screen.h>
#include <zmk/keymap.h>

#define LAYER_LABEL_WIDTH 170
#define LAYER_LABEL_HEIGHT 44
//#define LAYER_LABEL_Y_OFFSET 28
//#define LAYER_LABEL_Y_OFFSET 14
#define LAYER_LABEL_Y_OFFSET 0

#define SCREEN_SIZE 240
#define BOTTOM_CLEAR_HEIGHT 56

static void set_initial_layer_text(lv_obj_t *label) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));

    if (name != NULL && name[0] != '\0') {
        lv_label_set_text(label, name);
    } else {
        lv_label_set_text_fmt(label, "Layer %u", index);
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *layer_label = lv_label_create(screen);
    lv_obj_remove_style_all(layer_label);
    lv_obj_set_size(layer_label, LAYER_LABEL_WIDTH, LAYER_LABEL_HEIGHT);
    lv_label_set_long_mode(layer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_bg_color(layer_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_align(layer_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(layer_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    set_initial_layer_text(layer_label);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, LAYER_LABEL_Y_OFFSET);

    lv_obj_t *bottom_clear = lv_obj_create(screen);
    lv_obj_remove_style_all(bottom_clear);
    lv_obj_set_size(bottom_clear, SCREEN_SIZE, BOTTOM_CLEAR_HEIGHT);
    lv_obj_set_style_bg_color(bottom_clear, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom_clear, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(bottom_clear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bottom_clear, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_move_foreground(bottom_clear);

    return screen;
}
