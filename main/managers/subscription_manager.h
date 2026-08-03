#include <stdint.h>
#include <esp_err.h>

#include "node_manager.h"

#pragma once

/**
 * Serialises Matter subscriptions through a queue drained by a single worker task.
 *
 * Every trigger (discovery, ICD check-in, subscription terminated, subscription failed, or a manual
 * request from the web UI) just enqueues a node id. The worker decides what to subscribe to by
 * looking at the node's device type list, so callers do not have to know or remember.
 *
 * Serialising matters: the controller is capped at CHIP_CONFIG_CONTROLLER_MAX_ACTIVE_DEVICES
 * simultaneous connections, and firing every subscription at once can exhaust it.
 */
esp_err_t subscription_manager_init(node_manager_t *manager);

/**
 * Queue a subscription attempt for a node. Marks the node's subscription as pending, so a node that
 * is already queued or in flight will not be queued twice.
 */
esp_err_t enqueue_subscription(uint64_t node_id);
