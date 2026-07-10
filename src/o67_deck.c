/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <dt-bindings/zmk/keys.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/events/battery_state_changed.h>
#endif
#include <zmk/keymap.h>

#define SCREEN_SIZE 240
#define CST816S_TOUCHES_REG 0x02
#define CST816S_XY_REG 0x03
#define SWIPE_THRESHOLD 30
#define TOUCH_POLL_MS 20
#define TAP_RELEASE_MS 20
#define GUIDE_REFRESH_MS 500
#define BATTERY_ARC_DEGREES 90
#define BATTERY_ARC_COUNT 2
#define BATTERY_ARC_INNER_OFFSET 5
#define BATTERY_ARC_INNER_WIDTH 4
#define BATTERY_ARC_UNKNOWN UINT8_MAX
#define LAYER_NAME_BOTTOM_OFFSET 5
/* 0x808080 pre-corrected for the display's RGB565 byte order. */
// #define DISPLAY_GRAY_HEX 0x101021

#ifndef TP_DEBUG
#define TP_DEBUG false
#endif

#ifndef TP_GUIDE
#define TP_GUIDE true
#endif

#ifndef TP_PAGE_COUNT
#define TP_PAGE_COUNT 3
#endif

#define SWIPE_RIGHT_POSITION 13U
#define SWIPE_LEFT_POSITION 14U

#if TP_PAGE_COUNT < 1
#error "TP_PAGE_COUNT must be at least 1"
#endif

extern const lv_image_dsc_t disp;

static uint32_t current_page = 0U;

#define DISPLAY_BACKLIGHT_NODE DT_NODELABEL(display_bl)

static const struct pwm_dt_spec display_backlight = PWM_DT_SPEC_GET(DISPLAY_BACKLIGHT_NODE);

struct battery_arc_state {
    uint8_t source;
    uint8_t level;
};

// static lv_obj_t *battery_outer_arcs[BATTERY_ARC_COUNT];
static lv_obj_t *battery_inner_arcs[BATTERY_ARC_COUNT];
static uint8_t battery_arc_levels[BATTERY_ARC_COUNT] = {BATTERY_ARC_UNKNOWN,
                                                        BATTERY_ARC_UNKNOWN};
static const uint16_t battery_arc_zero_angles[BATTERY_ARC_COUNT] = {225, 315};
static lv_obj_t *layer_name_label;
// static lv_obj_t *layer_name_shadow_label;

struct layer_name_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void set_display_brightness(void) {
    if (!pwm_is_ready_dt(&display_backlight)) {
        return;
    }

    uint32_t pulse =
        ((uint64_t)display_backlight.period * CONFIG_O67R_DISPLAY_BRIGHTNESS) / 100U;
    pwm_set_pulse_dt(&display_backlight, pulse);
}

static uint16_t battery_arc_wrap_angle(uint16_t angle) {
    return angle >= 360U ? angle - 360U : angle;
}

static uint16_t battery_arc_level_degrees(uint8_t level) {
    return ((uint32_t)BATTERY_ARC_DEGREES * level) / 100U;
}

static uint16_t battery_arc_start_angle(uint8_t index, uint8_t level) {
    uint16_t degrees = battery_arc_level_degrees(level);

    if (index == 0U) {
        return (battery_arc_zero_angles[index] + 360U - degrees) % 360U;
    }

    return battery_arc_zero_angles[index];
}

static uint16_t battery_arc_end_angle(uint8_t index, uint8_t level) {
    uint16_t degrees = battery_arc_level_degrees(level);

    if (index == 0U) {
        return battery_arc_zero_angles[index];
    }

    return battery_arc_wrap_angle(battery_arc_zero_angles[index] + degrees);
}

static void set_battery_arc_level(uint8_t index, uint8_t level) {
    if (index >= BATTERY_ARC_COUNT) {
        return;
    }

    if (level > 100U) {
        level = 100U;
    }

    battery_arc_levels[index] = level;
    uint16_t start_angle = battery_arc_start_angle(index, level);
    uint16_t end_angle = battery_arc_end_angle(index, level);
    lv_opa_t opa = level > 0U ? LV_OPA_COVER : LV_OPA_TRANSP;

    // if (battery_outer_arcs[index] != NULL) {
    //     lv_arc_set_angles(battery_outer_arcs[index], start_angle, end_angle);
    //     lv_obj_set_style_arc_opa(battery_outer_arcs[index], opa, LV_PART_INDICATOR);
    // }
    if (battery_inner_arcs[index] != NULL) {
        lv_arc_set_angles(battery_inner_arcs[index], start_angle, end_angle);
        lv_obj_set_style_arc_opa(battery_inner_arcs[index], opa, LV_PART_INDICATOR);
    }
}

static void set_layer_name_label_text(lv_obj_t *label, struct layer_name_state state) {
    if (label == NULL) {
        return;
    }

    if (state.label != NULL && strlen(state.label) > 0) {
        lv_label_set_text(label, state.label);
    } else {
        lv_label_set_text_fmt(label, "%u", state.index);
    }
}

static void layer_name_update_cb(struct layer_name_state state) {
    set_layer_name_label_text(layer_name_label, state);
    // set_layer_name_label_text(layer_name_shadow_label, state);
}

static struct layer_name_state layer_name_get_state(const zmk_event_t *eh) {
    LV_UNUSED(eh);

    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();

    return (struct layer_name_state){
        .index = index,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(o67r_layer_name, struct layer_name_state, layer_name_update_cb,
                            layer_name_get_state);
ZMK_SUBSCRIPTION(o67r_layer_name, zmk_layer_state_changed);

static void init_layer_name(lv_obj_t *screen) {
    // layer_name_shadow_label = lv_label_create(screen);
    // lv_label_set_text(layer_name_shadow_label, "");
    // lv_obj_set_style_text_color(layer_name_shadow_label, lv_color_hex(0x000000), LV_PART_MAIN);
    // lv_obj_set_style_text_font(layer_name_shadow_label, &lv_font_montserrat_32, LV_PART_MAIN);
    // lv_obj_align(layer_name_shadow_label, LV_ALIGN_BOTTOM_MID, 1,
    //              -LAYER_NAME_BOTTOM_OFFSET + 1);

    layer_name_label = lv_label_create(screen);
    lv_label_set_text(layer_name_label, "");
    lv_obj_set_style_text_color(layer_name_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(layer_name_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(layer_name_label, LV_ALIGN_BOTTOM_MID, 0, -LAYER_NAME_BOTTOM_OFFSET);

    o67r_layer_name_init();
    layer_name_update_cb(layer_name_get_state(NULL));
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
static lv_obj_t *position_labels[4];
static char position_label_text[4][12];

static void update_position_labels(void);

static uint32_t touch_position_from_coordinates(uint16_t x, uint16_t y) {
    uint32_t page_position = current_page * 4U;

    if (y <= 120) {
        return page_position + (x <= 120 ? 1U : 2U);
    }

    return page_position + (x <= 120 ? 4U : 3U);
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
            send_touch_position_tap(SWIPE_LEFT_POSITION);
        } else {
            show_swipe_direction("RIGHT");
            send_touch_position_tap(SWIPE_RIGHT_POSITION);
        }
    } else if (distance_y >= SWIPE_THRESHOLD) {
        swipe_detected = true;
        if (delta_y < 0) {
            show_swipe_direction("UP");
            change_page(1);
        } else {
            show_swipe_direction("DOWN");
            change_page(-1);
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
                                    lv_coord_t x_offset, lv_coord_t y_offset,
                                    lv_coord_t outer_offset, lv_coord_t width,
                                    lv_color_t color) {
    lv_obj_t *arc = lv_arc_create(screen);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, SCREEN_SIZE - (outer_offset * 2), SCREEN_SIZE - (outer_offset * 2));
    lv_obj_align(arc, LV_ALIGN_CENTER, x_offset, y_offset);
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

static lv_obj_t *create_key_name_label(lv_obj_t *screen, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, x - (SCREEN_SIZE / 2), y - (SCREEN_SIZE / 2));

    return label;
}

static const char *hex_keycode_label(uint32_t keycode, char *fallback, size_t fallback_size) {
    static const char hex[] = "0123456789ABCDEF";

    if (fallback_size < 7U) {
        return "?";
    }

    fallback[0] = '0';
    fallback[1] = 'x';
    fallback[2] = hex[(keycode >> 12) & 0x0f];
    fallback[3] = hex[(keycode >> 8) & 0x0f];
    fallback[4] = hex[(keycode >> 4) & 0x0f];
    fallback[5] = hex[keycode & 0x0f];
    fallback[6] = '\0';

    return fallback;
}

static const char *keycode_label(uint32_t keycode, char *fallback, size_t fallback_size) {
    switch (keycode) {
    case A:
        return "A";
    case B:
        return "B";
    case C:
        return "C";
    case D:
        return "D";
    case E:
        return "E";
    case F:
        return "F";
    case G:
        return "G";
    case H:
        return "H";
    case I:
        return "I";
    case J:
        return "J";
    case K:
        return "K";
    case L:
        return "L";
    case M:
        return "M";
    case N:
        return "N";
    case O:
        return "O";
    case P:
        return "P";
    case Q:
        return "Q";
    case R:
        return "R";
    case S:
        return "S";
    case T:
        return "T";
    case U:
        return "U";
    case V:
        return "V";
    case W:
        return "W";
    case X:
        return "X";
    case Y:
        return "Y";
    case Z:
        return "Z";
    case N0:
        return "0";
    case N1:
        return "1";
    case N2:
        return "2";
    case N3:
        return "3";
    case N4:
        return "4";
    case N5:
        return "5";
    case N6:
        return "6";
    case N7:
        return "7";
    case N8:
        return "8";
    case N9:
        return "9";
    case ESC:
        return "ESC";
    case TAB:
        return "TAB";
    case SPACE:
        return "SPC";
    case BSPC:
        return "BSPC";
    case DEL:
        return "DEL";
    case ENTER:
        return "ENT";
    case KP_ENTER:
        return "KPENT";
    case LSHFT:
        return "LSFT";
    case RSHFT:
        return "RSFT";
    case LCTRL:
        return "LCTL";
    case LALT:
        return "LALT";
    case LWIN:
        return "LWIN";
    case LEFT:
        return "LEFT";
    case RIGHT:
        return "RGHT";
    case UP:
        return "UP";
    case DOWN:
        return "DOWN";
    case HOME:
        return "HOME";
    case END:
        return "END";
    case PG_UP:
        return "PGUP";
    case PG_DN:
        return "PGDN";
    case F1:
        return "F1";
    case F2:
        return "F2";
    case F3:
        return "F3";
    case F4:
        return "F4";
    case F5:
        return "F5";
    case F6:
        return "F6";
    case F7:
        return "F7";
    case F8:
        return "F8";
    case F9:
        return "F9";
    case F10:
        return "F10";
    case PSCRN:
        return "PSCR";
    case GRAVE:
        return "`";
    case MINUS:
        return "-";
    case EQUAL:
        return "=";
    case LBKT:
        return "[";
    case RBKT:
        return "]";
    case BSLH:
        return "\\";
    case SEMI:
        return ";";
    case SQT:
        return "'";
    case COMMA:
        return ",";
    case DOT:
        return ".";
    case SLASH:
        return "/";
    case LANG1:
        return "LN1";
    case LANG2:
        return "LN2";
    }

    return hex_keycode_label(keycode, fallback, fallback_size);
}

static bool binding_is_transparent(const struct zmk_behavior_binding *binding) {
    if (binding->behavior_dev == NULL) {
        return false;
    }

#if DT_NODE_EXISTS(DT_NODELABEL(trans))
    return strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(trans))) == 0;
#else
    return false;
#endif
}

static const struct zmk_behavior_binding *active_binding_at_position(uint32_t position) {
    zmk_keymap_layer_index_t highest_layer = zmk_keymap_highest_layer_active();

    for (int32_t layer_index = highest_layer; layer_index >= 0; layer_index--) {
        zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(layer_index);
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer_id, position);

        if (binding == NULL || binding_is_transparent(binding)) {
            continue;
        }

        return binding;
    }

    return NULL;
}

static const char *binding_label(const struct zmk_behavior_binding *binding, char *fallback,
                                 size_t fallback_size) {
    if (binding == NULL) {
        return "?";
    }
    if (binding->behavior_dev == NULL) {
        return "?";
    }

    if (strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_INST(0, zmk_behavior_key_press))) == 0) {
        return keycode_label(binding->param1, fallback, fallback_size);
    }

#if DT_NODE_EXISTS(DT_NODELABEL(task_move_u))
    if (strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(task_move_u))) == 0) {
        return "TMU";
    }
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(task_move_d))
    if (strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(task_move_d))) == 0) {
        return "TMD";
    }
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(none))
    if (strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(none))) == 0) {
        return "";
    }
#endif

    return "?";
}

static void update_position_labels(void) {
    static const lv_coord_t label_x[] = {80, 160, 160, 80};
    static const lv_coord_t label_y[] = {80, 80, 160, 160};

    for (uint32_t index = 0; index < 4U; index++) {
        uint32_t position = current_page * 4U + index + 1U;

        if (position_labels[index] == NULL) {
            continue;
        }

        lv_label_set_text(position_labels[index],
                          binding_label(active_binding_at_position(position),
                                        position_label_text[index],
                                        sizeof(position_label_text[index])));
        lv_obj_align(position_labels[index], LV_ALIGN_CENTER, label_x[index] - (SCREEN_SIZE / 2),
                     label_y[index] - (SCREEN_SIZE / 2));
    }
}

static void guide_refresh_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    update_position_labels();
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

    if (TP_GUIDE) {
        //create_separator(screen, 30, 120, 71, 1);
        //create_separator(screen, 140, 120, 71, 1);
        //create_separator(screen, 120, 30, 1, 71);
        //create_separator(screen, 120, 140, 1, 71);
        create_separator(screen, 30, 120, 61, 1);
        create_separator(screen, 150, 120, 61, 1);
        create_separator(screen, 120, 30, 1, 61);
        create_separator(screen, 120, 150, 1, 61);
    }

    // battery_outer_arcs[0] =
    //     create_battery_arc(screen, battery_arc_start_angle(0, 0),
    //                        battery_arc_end_angle(0, 0), 5, 1,
    //                        BATTERY_ARC_OUTER_OFFSET, BATTERY_ARC_OUTER_WIDTH,
    //                        lv_color_hex(0x000000));
    // battery_outer_arcs[1] =
    //     create_battery_arc(screen, battery_arc_start_angle(1, 0),
    //                        battery_arc_end_angle(1, 0), -3, 1,
    //                        BATTERY_ARC_OUTER_OFFSET, BATTERY_ARC_OUTER_WIDTH,
    //                        lv_color_hex(0x000000));
    battery_inner_arcs[0] =
        create_battery_arc(screen, battery_arc_start_angle(0, 0), battery_arc_end_angle(0, 0), 4,
                           0, BATTERY_ARC_INNER_OFFSET, BATTERY_ARC_INNER_WIDTH,
                           lv_color_hex(0xffffff));
    battery_inner_arcs[1] =
        create_battery_arc(screen, battery_arc_start_angle(1, 0), battery_arc_end_angle(1, 0), -4,
                           0, BATTERY_ARC_INNER_OFFSET, BATTERY_ARC_INNER_WIDTH,
                           lv_color_hex(0xffffff));
    init_battery_arc_listener();

    for (uint8_t index = 0; index < BATTERY_ARC_COUNT; index++) {
        if (battery_arc_levels[index] != BATTERY_ARC_UNKNOWN) {
            set_battery_arc_level(index, battery_arc_levels[index]);
        }
    }

    if (TP_GUIDE) {
        position_labels[0] = create_key_name_label(screen, 80, 80);
        position_labels[1] = create_key_name_label(screen, 160, 80);
        position_labels[2] = create_key_name_label(screen, 160, 160);
        position_labels[3] = create_key_name_label(screen, 80, 160);
        update_position_labels();
        lv_timer_create(guide_refresh_timer_cb, GUIDE_REFRESH_MS, NULL);
    }
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
    init_layer_name(screen);

    return screen;
}
