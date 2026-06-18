/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/keymap.h>

static struct zmk_widget_layer_status layer_status_widget;

#define LAYER_LABEL_WIDTH 170
#define LAYER_LABEL_HEIGHT 44
//#define LAYER_LABEL_Y_OFFSET 28
#define LAYER_LABEL_Y_OFFSET 14

#define PATTERN_SIZE 212
#define PATTERN_BORDER_WIDTH 50
#define PATTERN_FILL_WIDTH 44
#define PATTERN_BORDER_COLOR 0x9aa1a8
#define PATTERN_FILL_COLOR 0x1457a7

static void set_initial_layer_text(lv_obj_t *label) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));

    if (name != NULL && name[0] != '\0') {
        lv_label_set_text(label, name);
    } else {
        lv_label_set_text_fmt(label, "Layer %u", index);
    }
}

static void style_pattern_arc(lv_obj_t *arc, uint32_t color, lv_coord_t width) {
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, PATTERN_SIZE, PATTERN_SIZE);
    lv_obj_center(arc);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
}

static void create_pattern_arc(lv_obj_t *screen, uint16_t start, uint16_t end) {
    //lv_obj_t *border = lv_arc_create(screen);
    //style_pattern_arc(border, PATTERN_BORDER_COLOR, PATTERN_BORDER_WIDTH);
    //lv_arc_set_angles(border, start, end);

    lv_obj_t *fill = lv_arc_create(screen);
    style_pattern_arc(fill, PATTERN_FILL_COLOR, PATTERN_FILL_WIDTH);
    lv_arc_set_angles(fill, start, end);
}

static void create_background_pattern(lv_obj_t *screen) {
    create_pattern_arc(screen, 18, 72);
    create_pattern_arc(screen, 108, 162);
    create_pattern_arc(screen, 198, 252);
    create_pattern_arc(screen, 288, 342);
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);

    create_background_pattern(screen);

    zmk_widget_layer_status_init(&layer_status_widget, screen);

    lv_obj_t *layer_label = zmk_widget_layer_status_obj(&layer_status_widget);
    lv_obj_set_size(layer_label, LAYER_LABEL_WIDTH, LAYER_LABEL_HEIGHT);
    lv_label_set_long_mode(layer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_align(layer_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(layer_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    set_initial_layer_text(layer_label);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, LAYER_LABEL_Y_OFFSET);

    return screen;
}
