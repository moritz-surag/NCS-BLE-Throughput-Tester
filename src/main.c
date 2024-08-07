/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>

#include "cts.h"

#define DELAY_US 1000000
#define COUNTER_MAX_US 1000000
#define ALARM_CHANNEL_ID 0
#define TIMER DT_NODELABEL(timer2)

struct counter_alarm_cfg alarm_cfg;
const struct device *const counter_dev = DEVICE_DT_GET(TIMER);

#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);
#define RUN_STATUS_LED DK_LED1
#define CONN_STATUS_LED DK_LED2
#define RUN_LED_BLINK_INTERVAL 1000

static struct bt_conn *current_conn;

/* Declarations */
void on_connected(struct bt_conn *conn, uint8_t err);
void on_disconnected(struct bt_conn *conn, uint8_t reason);
void on_notif_changed(enum bt_button_notifications_enabled status);
void on_data_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len);

static uint8_t SEND_DATA = 0;
static uint8_t m_array[244] = {0};
static uint8_t m_array_size = 244;

struct bt_conn_cb bluetooth_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};
struct bt_remote_service_cb remote_callbacks = {
    .notif_changed = on_notif_changed,
    .data_received = on_data_received,
};

/* Callbacks */
void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("connection err: %d", err);
        return;
    }
    LOG_INF("Connected.");
    current_conn = bt_conn_ref(conn);
    dk_set_led_on(CONN_STATUS_LED);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason: %d)", reason);
    dk_set_led_off(CONN_STATUS_LED);
    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
}

void on_notif_changed(enum bt_button_notifications_enabled status)
{
    if (status == BT_BUTTON_NOTIFICATIONS_ENABLED)
    {
        LOG_INF("Notifications enabled");
    }
    else
    {
        LOG_INF("Notifications disabled");
    }
}

void on_data_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
    uint8_t temp_str[len + 1];
    memcpy(temp_str, data, len);
    temp_str[len] = 0x00;

    LOG_INF("Received data on conn %p. Len: %d", (void *)conn, len);
    LOG_INF("Data: %s", temp_str);
}

void button_handler(uint32_t button_state, uint32_t has_changed)
{
    // int err;
    // int button_pressed = 0;
    if (has_changed & button_state)
    {
        switch (has_changed)
        {
        case DK_BTN1_MSK:
            // button_pressed = 1;
            if (SEND_DATA == 0)
            {
                SEND_DATA = 1;
                counter_start(counter_dev);
            }
            else
            {
                SEND_DATA = 0;
                counter_stop(counter_dev);
            }
            break;
        case DK_BTN2_MSK:
            // button_pressed = 2;
            break;
        case DK_BTN3_MSK:
            // button_pressed = 3;
            break;
        case DK_BTN4_MSK:
            // button_pressed = 4;
            break;
        default:
            break;
        }
        // LOG_INF("Button %d pressed.", button_pressed);
        // set_button_value(button_pressed);
        // err = send_button_notification(current_conn, button_pressed);
        // if (err) {
        //     LOG_ERR("Couldn't send notificaton. (err: %d)", err);
        // }
    }
}

/* Configurations */
static void configure_dk_buttons_leds(void)
{
    int err;
    err = dk_leds_init();
    if (err)
    {
        LOG_ERR("Couldn't init LEDS (err %d)", err);
    }
    err = dk_buttons_init(button_handler);
    if (err)
    {
        LOG_ERR("Couldn't init buttons (err %d)", err);
    }
}

int calc_throughput(int secs) {
    int throughput = (get_bytes_sent() / secs);
    return throughput;
}

static void counter_timeout_handler(const struct device *counter_dev,
                                    uint8_t chan_id, uint32_t ticks,
                                    void *user_data)
{
    struct counter_alarm_cfg *config = user_data;
    uint32_t now_ticks;
    uint64_t now_usec;
    int now_sec;
    int err;

    err = counter_get_value(counter_dev, &now_ticks);
    if (err)
    {
        printk("Failed to read counter value (err %d)", err);
        return;
    }

    now_usec = counter_ticks_to_us(counter_dev, now_ticks);
    now_sec = (int)(now_usec / USEC_PER_SEC);

    int current_throughput = calc_throughput(now_sec);
    // char buffer[50]; // Buffer to hold the string representation of the double
    // snprintf(buffer, sizeof(buffer), "Current throughput: %f", current_throughput);

    if (now_sec >= 10) {
        printk("%u seconds sent.\n", now_sec);
        printk("Stopping timer\n");
        printk("Current throughput: %d bytes per second.\n", current_throughput);
        SEND_DATA = 0;
        counter_stop(counter_dev);
        return;
    } else {
        printk("%u seconds sent.\n", now_sec);
        printk("Current throughput: %d bytes per second.\n", current_throughput);
    }

    /* Set a new alarm with a double length duration */
    // config->ticks = config->ticks * 2U;

    printk("Set alarm in %u sec (%u ticks)\n",
           (uint32_t)(counter_ticks_to_us(counter_dev,
                                          config->ticks) /
                      USEC_PER_SEC),
           config->ticks);

    err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
                                    user_data);
    if (err != 0)
    {
        printk("Alarm could not be set\n");
    }
}

/* configure timer */
int configure_timer(void)
{
    int err;

    printk("Counter alarm sample\n\n");

    if (!device_is_ready(counter_dev))
    {
        printk("device not ready.\n");
        return 0;
    }

    // counter_start(counter_dev);
    
    alarm_cfg.flags = 0;
    alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY_US);
    alarm_cfg.callback = counter_timeout_handler;
    alarm_cfg.user_data = &alarm_cfg;

    err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
                                    &alarm_cfg);
    printk("Set alarm in %u sec (%u ticks)\n",
           (uint32_t)(counter_ticks_to_us(counter_dev,
                                          alarm_cfg.ticks) /
                      USEC_PER_SEC),
           alarm_cfg.ticks);

    if (-EINVAL == err)
    {
        printk("Alarm settings invalid\n");
    }
    else if (-ENOTSUP == err)
    {
        printk("Alarm setting request not supported\n");
    }
    else if (err != 0)
    {
        printk("Error\n");
    }

    return err;
}
/* Main */
int main(void)
{
    int err;
    int blink_status = 0;
    LOG_INF("Hello World! %s\n", CONFIG_BOARD);

    configure_dk_buttons_leds();

    err = configure_timer();
    if (err)
    {
        LOG_INF("Couldn't configure timer, err: %d", err);
    }

    err = bluetooth_init(&bluetooth_callbacks, &remote_callbacks);
    if (err)
    {
        LOG_INF("Couldn't initialize Bluetooth. err: %d", err);
    }

    LOG_INF("Running...");
    for (;;)
    {
        dk_set_led(RUN_STATUS_LED, (blink_status++) % 2);
        k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));

        while (SEND_DATA == 1)
        {
            send_button_notification(current_conn, m_array);
            for (int i = 0; i < m_array_size; i++)
            {
                m_array[i]++;
            }
        }
    }
}
