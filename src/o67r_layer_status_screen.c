/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include <zmk/display/status_screen.h>

#define SCREEN_SIZE 240
#define CST816S_TOUCHES_REG 0x02
#define CST816S_XY_REG 0x03
#define SWIPE_THRESHOLD 30
#define TOUCH_POLL_MS 20

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

#define CST816S_NODE DT_NODELABEL(cst816s)

#if DT_NODE_HAS_STATUS(CST816S_NODE, okay)

static const struct i2c_dt_spec cst816s_i2c = I2C_DT_SPEC_GET(CST816S_NODE);
static lv_obj_t *swipe_label;
static bool touch_active;
static uint16_t touch_start_y;
static uint16_t touch_last_y;

static int cst816s_read_touch_y(uint16_t *y) {
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

    *y = ((uint16_t)(xy[2] & 0x0f) << 8) | xy[3];
    return 1;
}

static void touch_poll_timer_cb(lv_timer_t *timer) {
    uint16_t y;
    int touch_state;

    LV_UNUSED(timer);

    touch_state = cst816s_read_touch_y(&y);
    if (touch_state < 0) {
        return;
    }

    if (touch_state > 0) {
        if (!touch_active) {
            touch_start_y = y;
            touch_active = true;
        }

        touch_last_y = y;
        return;
    }

    if (!touch_active) {
        return;
    }

    if ((int32_t)touch_start_y - (int32_t)touch_last_y >= SWIPE_THRESHOLD) {
        lv_label_set_text(swipe_label, "UP");
    } else if ((int32_t)touch_last_y - (int32_t)touch_start_y >= SWIPE_THRESHOLD) {
        lv_label_set_text(swipe_label, "DOWN");
    }

    touch_active = false;
}

static void init_swipe_status(lv_obj_t *screen) {
    if (!device_is_ready(cst816s_i2c.bus)) {
        return;
    }

    swipe_label = lv_label_create(screen);
    lv_label_set_text(swipe_label, "");
    lv_obj_set_style_text_color(swipe_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(swipe_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_bg_color(swipe_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(swipe_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(swipe_label, 8, LV_PART_MAIN);
    lv_obj_center(swipe_label);

    lv_timer_create(touch_poll_timer_cb, TOUCH_POLL_MS, NULL);
}

#else

static void init_swipe_status(lv_obj_t *screen) {
    LV_UNUSED(screen);
}

#endif

lv_obj_t *zmk_display_status_screen(void) {
    static lv_color_filter_dsc_t rgb_to_gbr_filter;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *image = lv_image_create(screen);
    lv_color_filter_dsc_init(&rgb_to_gbr_filter, rgb_to_gbr);
    lv_obj_set_style_color_filter_dsc(image, &rgb_to_gbr_filter, LV_PART_MAIN);
    lv_obj_set_style_color_filter_opa(image, LV_OPA_COVER, LV_PART_MAIN);
    lv_image_set_src(image, &disp);
    lv_obj_center(image);

    init_swipe_status(screen);

    return screen;
}
