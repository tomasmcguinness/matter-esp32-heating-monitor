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
 * Pacing alone is not enough. An attempt holds one of the controller's CASE session-setup slots
 * (CHIP_CONFIG_CONTROLLER_MAX_ACTIVE_CASE_CLIENTS) until it resolves, and over Thread an attempt at
 * an unreachable node can take tens of seconds to time out. Starting one a second therefore piles
 * them up until the pool is empty, and every other connection -- reads, ICD check-ins, commissioning
 * -- then fails with CHIP_ERROR_NO_MEMORY at OperationalSessionSetup::EstablishConnection. So the
 * worker also caps how many attempts are in flight, and waits for one to finish before starting
 * another once the cap is reached.
 */
esp_err_t subscription_manager_init(node_manager_t *manager);

/**
 * Report that a subscription attempt for a node has resolved -- established, failed to connect, or
 * ended before it was established -- so its in-flight slot can go to the next node. Safe to call more
 * than once, or for a node with no attempt in flight: only the first call for an attempt frees a slot.
 */
void subscription_attempt_finished(uint64_t node_id);

/**
 * Queue a subscription attempt for a node. Marks the node's subscription as pending, so a node that
 * is already queued or in flight will not be queued twice.
 */
esp_err_t enqueue_subscription(uint64_t node_id);
