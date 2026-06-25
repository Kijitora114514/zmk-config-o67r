/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/events/battery_state_changed.h>
#include <zmk/event_manager.h>
#endif
#include <zmk/keymap.h>

#define SCREEN_SIZE 240
#define CST816S_TOUCHES_REG 0x02
#define CST816S_XY_REG 0x03
#define SWIPE_THRESHOLD 30
#define TOUCH_POLL_MS 20
#define TAP_RELEASE_MS 20
#define BATTERY_ARC_DEGREES 90
#define BATTERY_ARC_COUNT 2
#define BATTERY_ARC_STEPS 10
#define BATTERY_ARC_OUTER_OFFSET 4
#define BATTERY_ARC_OUTER_WIDTH 6
#define BATTERY_ARC_INNER_OFFSET 5
#define BATTERY_ARC_INNER_WIDTH 4
#define BATTERY_ARC_UNKNOWN UINT8_MAX
/* 0x808080 pre-corrected for the display's RGB565 byte order. */
// #define DISPLAY_GRAY_HEX 0x101021

#ifndef TP_DEBUG
#define TP_DEBUG false
#endif

#ifndef TP_PAGE_COUNT
#define TP_PAGE_COUNT 3
#endif

#if TP_PAGE_COUNT < 1
#error "TP_PAGE_COUNT must be at least 1"
#endif

extern const lv_image_dsc_t disp;

#define DISPLAY_BACKLIGHT_NODE DT_NODELABEL(display_bl)

static const struct pwm_dt_spec display_backlight = PWM_DT_SPEC_GET(DISPLAY_BACKLIGHT_NODE);

struct battery_arc_state {
    uint8_t source;
    uint8_t level;
};

static lv_obj_t *battery_outer_arcs[BATTERY_ARC_COUNT][BATTERY_ARC_STEPS];
static lv_obj_t *battery_inner_arcs[BATTERY_ARC_COUNT][BATTERY_ARC_STEPS];
static uint8_t battery_arc_levels[BATTERY_ARC_COUNT] = {BATTERY_ARC_UNKNOWN,
                                                        BATTERY_ARC_UNKNOWN};
static const uint16_t battery_arc_zero_angles[BATTERY_ARC_COUNT] = {225, 315};

static void set_display_brightness(void) {
    if (!pwm_is_ready_dt(&display_backlight)) {
        return;
    }

    uint32_t pulse =
        ((uint64_t)display_backlight.period * CONFIG_O67R_DISPLAY_BRIGHTNESS) / 100U;
    pwm_set_pulse_dt(&display_backlight, pulse);
}

static uint8_t battery_arc_level_step(uint8_t level) {
    uint8_t step = (level + (100U / BATTERY_ARC_STEPS) - 1U) / (100U / BATTERY_ARC_STEPS);

    return MIN(step, BATTERY_ARC_STEPS);
}

static uint16_t battery_arc_segment_degrees(void) {
    return BATTERY_ARC_DEGREES / BATTERY_ARC_STEPS;
}

static uint16_t battery_arc_wrap_angle(uint16_t angle) {
    return angle >= 360U ? angle - 360U : angle;
}

static uint16_t battery_arc_segment_start_angle(uint8_t index, uint8_t segment) {
    if (index == 0U) {
        return (battery_arc_zero_angles[index] + 360U -
                (uint16_t)(segment + 1U) * battery_arc_segment_degrees()) %
               360U;
    }

    return battery_arc_wrap_angle(battery_arc_zero_angles[index] +
                                  (uint16_t)segment * battery_arc_segment_degrees());
}

static uint16_t battery_arc_segment_end_angle(uint8_t index, uint8_t segment) {
    if (index == 0U) {
        return (battery_arc_zero_angles[index] + 360U -
                (uint16_t)segment * battery_arc_segment_degrees()) %
               360U;
    }

    return battery_arc_wrap_angle(battery_arc_zero_angles[index] +
                                  (uint16_t)(segment + 1U) * battery_arc_segment_degrees());
}

static void set_battery_arc_level(uint8_t index, uint8_t level) {
    if (index >= BATTERY_ARC_COUNT) {
        return;
    }

    if (level > 100U) {
        level = 100U;
    }

    battery_arc_levels[index] = level;

    for (uint8_t segment = 0; segment < BATTERY_ARC_STEPS; segment++) {
        lv_opa_t opa = segment < battery_arc_level_step(level) ? LV_OPA_COVER : LV_OPA_TRANSP;

        if (battery_outer_arcs[index][segment] != NULL) {
            lv_obj_set_style_arc_opa(battery_outer_arcs[index][segment], opa,
                                     LV_PART_INDICATOR);
        }
        if (battery_inner_arcs[index][segment] != NULL) {
            lv_obj_set_style_arc_opa(battery_inner_arcs[index][segment], opa,
                                     LV_PART_INDICATOR);
        }
    }
}

#if IS_ENABLED(CONFIG_ZMK_BLE)
static void battery_arc_update_cb(struct battery_arc_state state) {
    if (state.source >= BATTERY_ARC_COUNT) {
        return;
    }

    set_battery_arc_level(state.source, state.level);
}

static struct battery_arc_state battery_arc_get_state(const zmk_event_t *eh) {
    if (eh == NULL) {
        return (struct battery_arc_state){
            .source = BATTERY_ARC_COUNT,
            .level = 0,
        };
    }

    const struct zmk_peripheral_battery_state_changed *event =
        as_zmk_peripheral_battery_state_changed(eh);

    return (struct battery_arc_state){
        .source = event->source,
        .level = event->state_of_charge,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(o67r_battery_arc, struct battery_arc_state, battery_arc_update_cb,
                            battery_arc_get_state);
ZMK_SUBSCRIPTION(o67r_battery_arc, zmk_peripheral_battery_state_changed);

static void init_battery_arc_listener(void) { o67r_battery_arc_init(); }
#else
static void init_battery_arc_listener(void) {}
#endif

#define CST816S_NODE DT_NODELABEL(cst816s)

#if DT_NODE_HAS_STATUS(CST816S_NODE, okay)

static const struct i2c_dt_spec cst816s_i2c = I2C_DT_SPEC_GET(CST816S_NODE);
static lv_obj_t *swipe_label;
static bool touch_active;
static uint16_t touch_start_x;
static uint16_t touch_start_y;
static uint16_t touch_last_x;
static uint16_t touch_last_y;
static uint32_t touch_key_position;
static uint32_t pending_release_position;
static uint32_t current_page = 0U;
static lv_obj_t *position_labels[4];
static lv_obj_t *position_shadow_labels[4];

static void update_position_labels(void);

static uint32_t touch_position_from_coordinates(uint16_t x, uint16_t y) {
    uint32_t page_position = current_page * 4U;

    if (y <= 120) {
        return page_position + (x <= 120 ? 1U : 2U);
    }

    return page_position + (x <= 120 ? 4U : 3U);
}

static uint32_t vertical_swipe_position(bool swipe_up) {
    return (TP_PAGE_COUNT * 4U) + (current_page * 2U) + (swipe_up ? 1U : 2U);
}

static void change_page(int32_t direction) {
    if (direction < 0) {
        current_page = current_page == 0U ? TP_PAGE_COUNT - 1U : current_page - 1U;
    } else {
        current_page = current_page + 1U >= TP_PAGE_COUNT ? 0U : current_page + 1U;
    }

    update_position_labels();
}

static void set_touch_position_state(uint32_t position, bool pressed) {
    zmk_keymap_position_state_changed(ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, position, pressed,
                                      k_uptime_get());
}

static void touch_position_release_work_handler(struct k_work *work) {
    LV_UNUSED(work);
    set_touch_position_state(pending_release_position, false);
}

K_WORK_DELAYABLE_DEFINE(touch_position_release_work, touch_position_release_work_handler);

static void send_touch_position_tap(uint32_t position) {
    if (k_work_delayable_is_pending(&touch_position_release_work)) {
        return;
    }

    pending_release_position = position;
    set_touch_position_state(position, true);
    k_work_schedule(&touch_position_release_work, K_MSEC(TAP_RELEASE_MS));
}

static void show_swipe_direction(const char *direction) {
    if (swipe_label != NULL) {
        lv_label_set_text(swipe_label, direction);
    }
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
    bool swipe_detected = false;

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

            touch_key_position = touch_position_from_coordinates(x, y);
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
        swipe_detected = true;
        if (delta_x < 0) {
            show_swipe_direction("LEFT");
            change_page(-1);
        } else {
            show_swipe_direction("RIGHT");
            change_page(1);
        }
    } else if (distance_y >= SWIPE_THRESHOLD) {
        swipe_detected = true;
        if (delta_y < 0) {
            show_swipe_direction("UP");
            send_touch_position_tap(vertical_swipe_position(true));
        } else {
            show_swipe_direction("DOWN");
            send_touch_position_tap(vertical_swipe_position(false));
        }
    }

    if (!swipe_detected) {
        send_touch_position_tap(touch_key_position);
    }

    touch_active = false;
}

static void create_separator(lv_obj_t *screen, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                             lv_coord_t height) {
    lv_obj_t *shadow = lv_obj_create(screen);
    lv_obj_remove_style_all(shadow);
    lv_obj_set_pos(shadow, x + 1, y + 1);
    lv_obj_set_size(shadow, width, height);
    lv_obj_set_style_bg_color(shadow, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shadow, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *separator = lv_obj_create(screen);
    lv_obj_remove_style_all(separator);
    lv_obj_set_pos(separator, x, y);
    lv_obj_set_size(separator, width, height);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_battery_arc(lv_obj_t *screen, uint16_t start_angle, uint16_t end_angle,
                                    lv_coord_t x_offset, lv_coord_t outer_offset,
                                    lv_coord_t width, lv_color_t color) {
    lv_obj_t *arc = lv_arc_create(screen);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, SCREEN_SIZE - (outer_offset * 2), SCREEN_SIZE - (outer_offset * 2));
    lv_obj_align(arc, LV_ALIGN_CENTER, x_offset, 0);
    lv_arc_set_angles(arc, start_angle, end_angle);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);

    return arc;
}

static lv_obj_t *create_rotated_number(lv_obj_t *screen, const char *text, lv_coord_t x,
                                       lv_coord_t y, int32_t rotation, lv_obj_t **shadow_label) {
    *shadow_label = lv_label_create(screen);
    lv_label_set_text(*shadow_label, text);
    lv_obj_set_style_text_color(*shadow_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(*shadow_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(*shadow_label, rotation, LV_PART_MAIN);
    lv_obj_align(*shadow_label, LV_ALIGN_CENTER, x + 1 - (SCREEN_SIZE / 2),
                 y + 1 - (SCREEN_SIZE / 2));

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(label, rotation, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, x - (SCREEN_SIZE / 2), y - (SCREEN_SIZE / 2));

    return label;
}

static void update_position_labels(void) {
    static const lv_coord_t label_x[] = {80, 160, 160, 80};
    static const lv_coord_t label_y[] = {80, 80, 160, 160};

    for (uint32_t index = 0; index < 4U; index++) {
        if (position_labels[index] == NULL) {
            continue;
        }

        lv_label_set_text_fmt(position_labels[index], "%u", current_page * 4U + index + 1U);
        lv_label_set_text_fmt(position_shadow_labels[index], "%u",
                              current_page * 4U + index + 1U);
        lv_obj_align(position_labels[index], LV_ALIGN_CENTER, label_x[index] - (SCREEN_SIZE / 2),
                     label_y[index] - (SCREEN_SIZE / 2));
        lv_obj_align(position_shadow_labels[index], LV_ALIGN_CENTER,
                     label_x[index] + 1 - (SCREEN_SIZE / 2),
                     label_y[index] + 1 - (SCREEN_SIZE / 2));
    }
}

static void init_touchpad_overlay(lv_obj_t *screen) {
    if (TP_DEBUG) {
        swipe_label = lv_label_create(screen);
        lv_label_set_text(swipe_label, "");
        lv_obj_set_style_text_color(swipe_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(swipe_label, &lv_font_montserrat_32, LV_PART_MAIN);
        lv_obj_set_style_bg_color(swipe_label, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swipe_label, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_pad_all(swipe_label, 8, LV_PART_MAIN);
        lv_obj_center(swipe_label);
        return;
    }

    //create_separator(screen, 30, 120, 71, 1);
    //create_separator(screen, 140, 120, 71, 1);
    //create_separator(screen, 120, 30, 1, 71);
    //create_separator(screen, 120, 140, 1, 71);
    create_separator(screen, 30, 120, 61, 1);
    create_separator(screen, 150, 120, 61, 1);
    create_separator(screen, 120, 30, 1, 61);
    create_separator(screen, 120, 150, 1, 61);

    for (uint8_t segment = 0; segment < BATTERY_ARC_STEPS; segment++) {
        battery_outer_arcs[0][segment] =
            create_battery_arc(screen, battery_arc_segment_start_angle(0, segment),
                               battery_arc_segment_end_angle(0, segment), 4,
                               BATTERY_ARC_OUTER_OFFSET, BATTERY_ARC_OUTER_WIDTH,
                               lv_color_hex(0x000000));
        battery_outer_arcs[1][segment] =
            create_battery_arc(screen, battery_arc_segment_start_angle(1, segment),
                               battery_arc_segment_end_angle(1, segment), -4,
                               BATTERY_ARC_OUTER_OFFSET, BATTERY_ARC_OUTER_WIDTH,
                               lv_color_hex(0x000000));
    }
    for (uint8_t segment = 0; segment < BATTERY_ARC_STEPS; segment++) {
        battery_inner_arcs[0][segment] =
            create_battery_arc(screen, battery_arc_segment_start_angle(0, segment),
                               battery_arc_segment_end_angle(0, segment), 4,
                               BATTERY_ARC_INNER_OFFSET, BATTERY_ARC_INNER_WIDTH,
                               lv_color_hex(0xffffff));
        battery_inner_arcs[1][segment] =
            create_battery_arc(screen, battery_arc_segment_start_angle(1, segment),
                               battery_arc_segment_end_angle(1, segment), -4,
                               BATTERY_ARC_INNER_OFFSET, BATTERY_ARC_INNER_WIDTH,
                               lv_color_hex(0xffffff));
    }
    init_battery_arc_listener();

    for (uint8_t index = 0; index < BATTERY_ARC_COUNT; index++) {
        if (battery_arc_levels[index] != BATTERY_ARC_UNKNOWN) {
            set_battery_arc_level(index, battery_arc_levels[index]);
        }
    }

    position_labels[0] =
        create_rotated_number(screen, "1", 80, 80, 0, &position_shadow_labels[0]);
    position_labels[1] =
        create_rotated_number(screen, "2", 160, 80, 0, &position_shadow_labels[1]);
    position_labels[2] =
        create_rotated_number(screen, "3", 160, 160, 0, &position_shadow_labels[2]);
    position_labels[3] =
        create_rotated_number(screen, "4", 80, 160, 0, &position_shadow_labels[3]);
    update_position_labels();
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
    set_display_brightness();

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *image = lv_image_create(screen);
    lv_image_set_src(image, &disp);
    lv_obj_center(image);

    init_swipe_status(screen);

    return screen;
}
