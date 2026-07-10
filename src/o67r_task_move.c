/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_o67r_task_move

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <dt-bindings/zmk/keys.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#define TASK_MOVE_ALT_TIMEOUT_MS 2000
#define TASK_MOVE_TAP_MS 30
#define KEY_PRESS DEVICE_DT_NAME(DT_INST(0, zmk_behavior_key_press))

struct task_move_config {
    bool reverse;
};

static bool alt_pressed;
static bool shift_pressed;
static bool tab_pressed;

static void invoke_keycode(uint32_t keycode, bool pressed) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = KEY_PRESS,
        .param1 = keycode,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    zmk_behavior_invoke_binding(&binding, event, pressed);
}

static void release_alt(void) {
    if (!alt_pressed) {
        return;
    }

    alt_pressed = false;
    invoke_keycode(LALT, false);
}

static void release_tab_combo(void) {
    if (tab_pressed) {
        tab_pressed = false;
        invoke_keycode(TAB, false);
    }

    if (shift_pressed) {
        shift_pressed = false;
        invoke_keycode(LSHFT, false);
    }
}

static void alt_release_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    release_tab_combo();
    release_alt();
}

static void tab_release_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    release_tab_combo();
}

K_WORK_DELAYABLE_DEFINE(alt_release_work, alt_release_work_handler);
K_WORK_DELAYABLE_DEFINE(tab_release_work, tab_release_work_handler);

static void reset_alt_timer(void) {
    if (alt_pressed) {
        k_work_reschedule(&alt_release_work, K_MSEC(TASK_MOVE_ALT_TIMEOUT_MS));
    }
}

static int task_move_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    reset_alt_timer();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(o67r_task_move_listener, task_move_listener);
ZMK_SUBSCRIPTION(o67r_task_move_listener, zmk_position_state_changed);

static int on_task_move_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct task_move_config *config = dev->config;

    ARG_UNUSED(event);

    k_work_cancel_delayable(&tab_release_work);
    release_tab_combo();

    if (!alt_pressed) {
        alt_pressed = true;
        invoke_keycode(LALT, true);
    }

    if (config->reverse) {
        shift_pressed = true;
        invoke_keycode(LSHFT, true);
    }

    tab_pressed = true;
    invoke_keycode(TAB, true);

    k_work_schedule(&tab_release_work, K_MSEC(TASK_MOVE_TAP_MS));
    reset_alt_timer();

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_task_move_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api task_move_driver_api = {
    .binding_pressed = on_task_move_pressed,
    .binding_released = on_task_move_released,
};

#define TASK_MOVE_INST(n)                                                                         \
    static const struct task_move_config task_move_config_##n = {                                  \
        .reverse = DT_INST_PROP(n, reverse),                                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &task_move_config_##n, POST_KERNEL,               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &task_move_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TASK_MOVE_INST)
