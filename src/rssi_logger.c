/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Temporary diagnostic: logs RSSI for every active BLE connection every 5s.
 * Enable with CONFIG_ZMK_RSSI_LOGGER=y (requires CONFIG_BT_CTLR_CONN_RSSI=y).
 * Remove once the long-range instability is diagnosed.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rssi_logger, CONFIG_ZMK_LOG_LEVEL);

static int8_t read_conn_rssi(struct bt_conn *conn) {
    uint16_t handle;
    if (bt_hci_get_conn_handle(conn, &handle) != 0) {
        return 127;
    }

    /* bt_hci_cmd_alloc() was removed upstream; bt_hci_cmd_create() takes the
     * opcode + param length directly and reserves the same headroom. */
    struct net_buf *buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(struct bt_hci_cp_read_rssi));
    if (!buf) {
        return 127;
    }

    struct bt_hci_cp_read_rssi *cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(handle);

    struct net_buf *rsp;
    if (bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp) != 0) {
        return 127;
    }

    struct bt_hci_rp_read_rssi *rp = (void *)rsp->data;
    int8_t rssi = rp->rssi;
    net_buf_unref(rsp);
    return rssi;
}

static void log_conn(struct bt_conn *conn, void *user_data) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("RSSI %s: %d dBm", addr, read_conn_rssi(conn));
}

static void rssi_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rssi_work, rssi_work_handler);

static void rssi_work_handler(struct k_work *work) {
    bt_conn_foreach(BT_CONN_TYPE_LE, log_conn, NULL);
    k_work_reschedule(&rssi_work, K_SECONDS(5));
}

static int rssi_logger_init(void) {
    k_work_reschedule(&rssi_work, K_SECONDS(10));
    return 0;
}

SYS_INIT(rssi_logger_init, APPLICATION, 90);
