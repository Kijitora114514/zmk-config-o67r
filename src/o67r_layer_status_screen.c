/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/keymap.h>

static struct zmk_widget_layer_status layer_status_widget;

#define LAYER_LABEL_WIDTH 170
#define LAYER_LABEL_HEIGHT 44
//#define LAYER_LABEL_Y_OFFSET 28
#define LAYER_LABEL_Y_OFFSET 14

#define TOUCH_DOT_SIZE 5
#define TOUCH_DOT_VISIBLE_MS 5000
#define TOUCH_POLL_MS 25

#define CST816S_TOUCHES_REG 0x02
#define CST816S_XY_REG 0x03
#define CST816S_MAX_COORD 239

static lv_obj_t *touch_dot;
static int64_t touch_dot_expires_at;
static lv_coord_t last_touch_x;
static lv_coord_t last_touch_y;
static bool has_last_touch;

static void set_initial_layer_text(lv_obj_t *label) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));

    if (name != NULL && name[0] != '\0') {
        lv_label_set_text(label, name);
    } else {
        lv_label_set_text_fmt(label, "Layer %u", index);
    }
}

#define CST816S_NODE DT_NODELABEL(cst816s)

#if DT_NODE_HAS_STATUS(CST816S_NODE, okay)

static const struct i2c_dt_spec cst816s_i2c = I2C_DT_SPEC_GET(CST816S_NODE);
static const struct gpio_dt_spec cst816s_irq = GPIO_DT_SPEC_GET(CST816S_NODE, irq_gpios);
static const struct gpio_dt_spec cst816s_reset = GPIO_DT_SPEC_GET(CST816S_NODE, rst_gpios);

static lv_coord_t clamp_touch_coord(uint16_t coord) {
    if (coord > CST816S_MAX_COORD) {
        return CST816S_MAX_COORD;
    }

    return (lv_coord_t)coord;
}

static bool cst816s_read_touch(lv_coord_t *x, lv_coord_t *y) {
    uint8_t touch_count;
    uint8_t xy[4];
    int ret;

    ret = i2c_reg_read_byte_dt(&cst816s_i2c, CST816S_TOUCHES_REG, &touch_count);
    if (ret < 0 || (touch_count & 0x0f) == 0) {
        return false;
    }

    ret = i2c_burst_read_dt(&cst816s_i2c, CST816S_XY_REG, xy, sizeof(xy));
    if (ret < 0) {
        return false;
    }

    *x = clamp_touch_coord(((uint16_t)(xy[0] & 0x0f) << 8) | xy[1]);
    *y = clamp_touch_coord(((uint16_t)(xy[2] & 0x0f) << 8) | xy[3]);

    return true;
}

static void update_touch_dot(lv_coord_t x, lv_coord_t y) {
    lv_coord_t dot_x = x - (TOUCH_DOT_SIZE / 2);
    lv_coord_t dot_y = y - (TOUCH_DOT_SIZE / 2);
    lv_coord_t max_pos = CST816S_MAX_COORD - TOUCH_DOT_SIZE + 1;

    if (dot_x < 0) {
        dot_x = 0;
    } else if (dot_x > max_pos) {
        dot_x = max_pos;
    }

    if (dot_y < 0) {
        dot_y = 0;
    } else if (dot_y > max_pos) {
        dot_y = max_pos;
    }

    lv_obj_set_pos(touch_dot, dot_x, dot_y);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(touch_dot);
    touch_dot_expires_at = k_uptime_get() + TOUCH_DOT_VISIBLE_MS;
    last_touch_x = x;
    last_touch_y = y;
    has_last_touch = true;
}

static void touch_poll_timer_cb(lv_timer_t *timer) {
    lv_coord_t x;
    lv_coord_t y;
    bool touch_is_new;
    int irq_active;

    ARG_UNUSED(timer);

    if (touch_dot == NULL) {
        return;
    }

    if (cst816s_read_touch(&x, &y)) {
        irq_active = gpio_pin_get_dt(&cst816s_irq);
        touch_is_new = !has_last_touch || x != last_touch_x || y != last_touch_y || irq_active > 0;

        if (touch_is_new) {
            update_touch_dot(x, y);
        }

        if (touch_dot_expires_at != 0 && k_uptime_get() >= touch_dot_expires_at) {
            lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
            touch_dot_expires_at = 0;
        }

        return;
    }

    if (touch_dot_expires_at != 0 && k_uptime_get() >= touch_dot_expires_at) {
        lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
        touch_dot_expires_at = 0;
        has_last_touch = false;
    }
}

static void reset_cst816s(void) {
    if (!gpio_is_ready_dt(&cst816s_reset)) {
        return;
    }

    gpio_pin_configure_dt(&cst816s_reset, GPIO_OUTPUT_INACTIVE);
    gpio_pin_set_dt(&cst816s_reset, 1);
    k_msleep(5);
    gpio_pin_set_dt(&cst816s_reset, 0);
    k_msleep(50);
}

static void init_touch_status(lv_obj_t *screen) {
    if (!device_is_ready(cst816s_i2c.bus) || !gpio_is_ready_dt(&cst816s_irq)) {
        return;
    }

    reset_cst816s();
    gpio_pin_configure_dt(&cst816s_irq, GPIO_INPUT);

    touch_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(touch_dot);
    lv_obj_set_size(touch_dot, TOUCH_DOT_SIZE, TOUCH_DOT_SIZE);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(touch_dot, lv_color_hex(0xff0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(touch_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(touch_dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(touch_poll_timer_cb, TOUCH_POLL_MS, NULL);
}

#else

static void init_touch_status(lv_obj_t *screen) {
    ARG_UNUSED(screen);
}

#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);

    zmk_widget_layer_status_init(&layer_status_widget, screen);

    lv_obj_t *layer_label = zmk_widget_layer_status_obj(&layer_status_widget);
    lv_obj_set_size(layer_label, LAYER_LABEL_WIDTH, LAYER_LABEL_HEIGHT);
    lv_label_set_long_mode(layer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_align(layer_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(layer_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    set_initial_layer_text(layer_label);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, LAYER_LABEL_Y_OFFSET);

    init_touch_status(screen);

    return screen;
}
