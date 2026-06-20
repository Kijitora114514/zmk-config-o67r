/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include <dt-bindings/zmk/pointing.h>
#include <zmk/display/status_screen.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

#define SCREEN_SIZE 240
#define CST816S_TOUCHES_REG 0x02
#define CST816S_XY_REG 0x03
#define SWIPE_THRESHOLD 30
#define TOUCH_POLL_MS 20
#define IMAGE_DATA_SIZE (SCREEN_SIZE * SCREEN_SIZE * 2)
/* 0x808080 pre-corrected for the display's RGB565 byte order. */
#define DISPLAY_GRAY_HEX 0x101021

#ifndef TP_DEBUG
#define TP_DEBUG false
#endif

extern const lv_image_dsc_t disp;

static LV_ATTRIBUTE_MEM_ALIGN uint8_t swapped_image_data[IMAGE_DATA_SIZE];
static lv_image_dsc_t swapped_image;

static const lv_image_dsc_t *get_byte_swapped_image(void) {
    const uint8_t *rgb_data = disp.data;

    if (disp.header.cf != LV_COLOR_FORMAT_RGB565 || disp.data_size > sizeof(swapped_image_data)) {
        return &disp;
    }

    for (uint32_t index = 0; index + 1 < disp.data_size; index += 2) {
        swapped_image_data[index] = rgb_data[index + 1];
        swapped_image_data[index + 1] = rgb_data[index];
    }

    swapped_image = disp;
    swapped_image.data = swapped_image_data;

    return &swapped_image;
}

#define CST816S_NODE DT_NODELABEL(cst816s)

#if DT_NODE_HAS_STATUS(CST816S_NODE, okay)

static const struct i2c_dt_spec cst816s_i2c = I2C_DT_SPEC_GET(CST816S_NODE);
static lv_obj_t *swipe_label;
static bool touch_active;
static uint16_t touch_start_x;
static uint16_t touch_start_y;
static uint16_t touch_last_x;
static uint16_t touch_last_y;

static void show_swipe_direction(const char *direction) {
    if (swipe_label != NULL) {
        lv_label_set_text(swipe_label, direction);
    }
}

static void send_mouse_scroll(int16_t x, int16_t y) {
    zmk_hid_mouse_scroll_set(x, y);
    zmk_endpoint_send_mouse_report();
    zmk_hid_mouse_scroll_set(0, 0);
}

static int cst816s_read_touch_position(uint16_t *x, uint16_t *y) {
    uint8_t touch_count;
    uint8_t xy[4];
    int ret;

    ret = i2c_reg_read_byte_dt(&cst816s_i2c, CST816S_TOUCHES_REG, &touch_count);
    if (ret < 0) {
        return ret;
    }

    if ((touch_count & 0x0f) == 0) {
        return 0;
    }

    ret = i2c_burst_read_dt(&cst816s_i2c, CST816S_XY_REG, xy, sizeof(xy));
    if (ret < 0) {
        return ret;
    }

    *x = ((uint16_t)(xy[0] & 0x0f) << 8) | xy[1];
    *y = ((uint16_t)(xy[2] & 0x0f) << 8) | xy[3];
    return 1;
}

static void touch_poll_timer_cb(lv_timer_t *timer) {
    uint16_t x;
    uint16_t y;
    int32_t delta_x;
    int32_t delta_y;
    int32_t distance_x;
    int32_t distance_y;
    int touch_state;

    LV_UNUSED(timer);

    touch_state = cst816s_read_touch_position(&x, &y);
    if (touch_state < 0) {
        return;
    }

    if (touch_state > 0) {
        if (!touch_active) {
            touch_start_x = x;
            touch_start_y = y;
            touch_active = true;
        }

        touch_last_x = x;
        touch_last_y = y;
        return;
    }

    if (!touch_active) {
        return;
    }

    delta_x = (int32_t)touch_last_x - (int32_t)touch_start_x;
    delta_y = (int32_t)touch_last_y - (int32_t)touch_start_y;
    distance_x = delta_x < 0 ? -delta_x : delta_x;
    distance_y = delta_y < 0 ? -delta_y : delta_y;

    if (distance_x > distance_y && distance_x >= SWIPE_THRESHOLD) {
        if (delta_x < 0) {
            show_swipe_direction("LEFT");
            send_mouse_scroll(-ZMK_POINTING_DEFAULT_SCRL_VAL, 0);
        } else {
            show_swipe_direction("RIGHT");
            send_mouse_scroll(ZMK_POINTING_DEFAULT_SCRL_VAL, 0);
        }
    } else if (distance_y >= SWIPE_THRESHOLD) {
        if (delta_y < 0) {
            show_swipe_direction("UP");
            send_mouse_scroll(0, ZMK_POINTING_DEFAULT_SCRL_VAL);
        } else {
            show_swipe_direction("DOWN");
            send_mouse_scroll(0, -ZMK_POINTING_DEFAULT_SCRL_VAL);
        }
    }

    touch_active = false;
}

static void create_separator(lv_obj_t *screen, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                             lv_coord_t height) {
    lv_obj_t *separator = lv_obj_create(screen);
    lv_obj_remove_style_all(separator);
    lv_obj_set_pos(separator, x, y);
    lv_obj_set_size(separator, width, height);
    lv_obj_set_style_bg_color(separator, lv_color_hex(DISPLAY_GRAY_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_rotated_number(lv_obj_t *screen, const char *text, lv_coord_t x, lv_coord_t y,
                                  int32_t rotation) {
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(DISPLAY_GRAY_HEX), LV_PART_MAIN);
    lv_obj_set_style_text_opa(label, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(label, rotation, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, x - (SCREEN_SIZE / 2), y - (SCREEN_SIZE / 2));
}

static void init_touchpad_overlay(lv_obj_t *screen) {
    if (TP_DEBUG) {
        swipe_label = lv_label_create(screen);
        lv_label_set_text(swipe_label, "");
        lv_obj_set_style_text_color(swipe_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_opa(swipe_label, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_text_font(swipe_label, &lv_font_montserrat_32, LV_PART_MAIN);
        lv_obj_set_style_bg_color(swipe_label, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swipe_label, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_pad_all(swipe_label, 8, LV_PART_MAIN);
        lv_obj_center(swipe_label);
        return;
    }

    create_separator(screen, 20, 120, 81, 2);
    create_separator(screen, 140, 120, 81, 2);
    create_separator(screen, 120, 20, 2, 81);
    create_separator(screen, 120, 140, 2, 81);

    create_rotated_number(screen, "1", 60, 60, 0);
    create_rotated_number(screen, "2", 180, 60, 0);
    create_rotated_number(screen, "3", 180, 180, 0);
    create_rotated_number(screen, "4", 60, 180, 0);
}

static void init_swipe_status(lv_obj_t *screen) {
    init_touchpad_overlay(screen);

    if (!device_is_ready(cst816s_i2c.bus)) {
        return;
    }

    lv_timer_create(touch_poll_timer_cb, TOUCH_POLL_MS, NULL);
}

#else

static void init_swipe_status(lv_obj_t *screen) {
    LV_UNUSED(screen);
}

#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *image = lv_image_create(screen);
    lv_image_set_src(image, get_byte_swapped_image());
    lv_obj_center(image);

    init_swipe_status(screen);

    return screen;
}
