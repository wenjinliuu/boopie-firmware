/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boopie_vpn_nodes.h"

/*
 * Boopie's VPN (docs/boopie-interaction.md): one switch, nodes from a
 * Shadowsocks subscription, and only Muse goes through it; Xiaozhi, the
 * clock and updates stay direct.
 *
 * How Muse goes through it without touching Muse's code: while it's on, the
 * names Muse connects to (*.muse.ai, *.metaaivm.com) resolve to the board
 * itself (lwip's resolve hook), where a relay reads which name each TLS
 * connection asks for (its SNI) and carries it, still encrypted end to end,
 * through the chosen node. The node looks the name up, so nothing here
 * depends on local DNS.
 *
 * Any task may call these.
 */

#define BOOPIE_VPN_NODES_MAX 64

/* Loads the settings and nodes; starts the relay if it's on. After the user
 * data partition is mounted. `extra_host` is one more name to send through
 * it (Muse's own server, if it's been changed), or NULL. */
void boopie_vpn_init(const char *extra_host);

bool boopie_vpn_on(void);
void boopie_vpn_set_on(bool on);

int boopie_vpn_count(void);
/* A node, its password left out. */
bool boopie_vpn_node(int index, boopie_vpn_node_t *out);
int boopie_vpn_current(void);            /* -1: none chosen */
void boopie_vpn_select(int index);

/* The last speed test of a node: ms to connect, -1 not tested, -2 failed. */
int boopie_vpn_latency(int index);

typedef enum {
    BOOPIE_VPN_IDLE,
    BOOPIE_VPN_UPDATING,     /* fetching the subscription */
    BOOPIE_VPN_TESTING,      /* timing the nodes */
} boopie_vpn_busy_t;

/* What it's doing, and the last outcome in words ("已更新：12 个节点"). */
boopie_vpn_busy_t boopie_vpn_busy(char *msg, size_t cap);

/* In the background: fetch the subscription again; time every node. */
void boopie_vpn_update(void);
/* The network came up: with a subscription saved but no nodes yet (it was
 * imported just before a restart), they're fetched now. */
void boopie_vpn_net_up(void);
/* Nodes pasted rather than fetched (a subscription's content, ss:// links one
 * a line, or Clash YAML): for when the subscription's site can't be reached
 * from here. Returns how many Shadowsocks nodes it found and kept. */
int boopie_vpn_import(const char *text, size_t len);
void boopie_vpn_test(void);

/* Whether the relay carried Muse lately, for the status line. */
bool boopie_vpn_active(void);
/* Boopie: a line saying why the tunnel keeps failing (three in a row, the last
 * few minutes), for the pet to pass on; NULL while it works or is off. */
const char *boopie_vpn_trouble(void);
