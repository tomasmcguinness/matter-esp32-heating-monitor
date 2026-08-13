#include <stdint.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <esp_matter_controller_subscribe_command.h>

#include "subscription_manager.h"
#include "node_manager.h"
#include "app_main.h"

using namespace chip::app::Clusters;
using namespace esp_matter::controller;

static const char *TAG = "subscription_manager";

#define SUBSCRIPTION_QUEUE_DEPTH 16
#define SUBSCRIPTION_TASK_STACK 4096
#define SUBSCRIPTION_TASK_PRIORITY 5

// Minimum interval between subscription attempts. A node that keeps failing re-queues itself
// immediately, so without this the worker would spin on an unreachable sensor.
#define SUBSCRIBE_PACING_MS 1000

// How often to look for nodes that ought to have a subscription but don't.
#define SWEEP_INTERVAL_MS 60000

// Subscription reporting intervals, unchanged from the call sites this replaces.
#define SUBSCRIBE_MIN_INTERVAL 0
#define SUBSCRIBE_MAX_INTERVAL 60

static QueueHandle_t s_queue = NULL;
static node_manager_t *s_node_manager = NULL;

/**
 * Runs on the CHIP event loop, so it is safe to touch the controller from here.
 */
static void send_subscription(intptr_t arg)
{
    uint64_t node_id = (uint64_t)arg;

    matter_node_t *node = find_node(s_node_manager, node_id);

    if (!node)
    {
        ESP_LOGW(TAG, "Node 0x%016llX went away before we could subscribe", node_id);
        return;
    }

    // What we subscribe to is decided by what the node says it is.
    bool wants_temperature = node_has_device_type(node, DEVICE_TYPE_TEMPERATURE_SENSOR);
    bool wants_flow = node_has_device_type(node, DEVICE_TYPE_FLOW_SENSOR);
    bool wants_battery = node_is_battery_powered(node);

    if (!wants_temperature && !wants_flow && !wants_battery)
    {
        ESP_LOGW(TAG, "Node 0x%016llX has no temperature or flow device types and is not on battery; nothing to subscribe to", node_id);

        bool create_new_subscription = false;
        mark_node_has_no_subscription(s_node_manager, node_id, 0, &create_new_subscription);
        return;
    }

    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc((wants_temperature ? 1 : 0) + (wants_flow ? 1 : 0) + (wants_battery ? 2 : 0));

    if (!attr_paths.Get())
    {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return;
    }

    size_t path_index = 0;

    if (wants_temperature)
    {
        attr_paths[path_index++] = AttributePathParams(TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id);
    }

    if (wants_flow)
    {
        attr_paths[path_index++] = AttributePathParams(FlowMeasurement::Id, FlowMeasurement::Attributes::MeasuredValue::Id);
    }

    if (wants_battery)
    {
        // The endpoint is left wildcard because PowerSource does not have to be on the root, and a
        // bridge can have one instance per bridged device. BatVoltage is optional, but an unsupported
        // attribute on a wildcard path is skipped rather than reported as an error.
        //
        attr_paths[path_index++] = AttributePathParams(PowerSource::Id, PowerSource::Attributes::BatPercentRemaining::Id);
        attr_paths[path_index++] = AttributePathParams(PowerSource::Id, PowerSource::Attributes::BatVoltage::Id);
    }

    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0);

    ESP_LOGI(TAG, "Subscribing to node 0x%016llX (%u path(s): %s%s%s)", node_id, (unsigned)path_index,
             wants_temperature ? "temperature " : "", wants_flow ? "flow " : "", wants_battery ? "battery" : "");

    auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(node_id,
                                                                              std::move(attr_paths),
                                                                              std::move(event_paths),
                                                                              SUBSCRIBE_MIN_INTERVAL,
                                                                              SUBSCRIBE_MAX_INTERVAL,
                                                                              false, // auto resubscribe
                                                                              attribute_data_cb,
                                                                              nullptr,
                                                                              node_subscription_established_cb,
                                                                              node_subscription_terminated_cb,
                                                                              node_subscribe_failed_cb,
                                                                              false); // keep subscription

    if (!cmd)
    {
        ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
        return;
    }

    esp_err_t err = cmd->send_command();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send subscribe command: %s", esp_err_to_name(err));
    }
}

/**
 * Look for nodes that should have a subscription but don't, and queue them.
 *
 * ICD (sleepy) nodes are skipped: they are only worth subscribing to when they check in, which
 * on_icd_checkin_callback handles.
 */
static void sweep_for_unsubscribed_nodes(void)
{
    matter_node_t *node = s_node_manager->node_list;

    while (node)
    {
        if (!node->is_icd && node_needs_subscription(s_node_manager, node->node_id))
        {
            ESP_LOGI(TAG, "Sweep found node 0x%016llX without a subscription", node->node_id);
            enqueue_subscription(node->node_id);
        }

        node = node->next;
    }
}

static void subscription_task(void *arg)
{
    ESP_LOGI(TAG, "Subscription worker started");

    while (true)
    {
        uint64_t node_id = 0;

        if (xQueueReceive(s_queue, &node_id, pdMS_TO_TICKS(SWEEP_INTERVAL_MS)) == pdTRUE)
        {
            // State may have moved on while the request sat in the queue.
            matter_node_t *node = find_node(s_node_manager, node_id);

            if (!node)
            {
                ESP_LOGW(TAG, "Dropping queued subscription for unknown node 0x%016llX", node_id);
                continue;
            }

            if (node->has_subscription)
            {
                ESP_LOGI(TAG, "Node 0x%016llX already subscribed; skipping", node_id);
                continue;
            }

            chip::DeviceLayer::PlatformMgr().ScheduleWork(send_subscription, (intptr_t)node_id);

            vTaskDelay(pdMS_TO_TICKS(SUBSCRIBE_PACING_MS));
        }
        else
        {
            sweep_for_unsubscribed_nodes();
        }
    }
}

esp_err_t subscription_manager_init(node_manager_t *manager)
{
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_node_manager = manager;

    s_queue = xQueueCreate(SUBSCRIPTION_QUEUE_DEPTH, sizeof(uint64_t));

    if (!s_queue)
    {
        ESP_LOGE(TAG, "Failed to create subscription queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(subscription_task, "matter_sub", SUBSCRIPTION_TASK_STACK, NULL, SUBSCRIPTION_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create subscription task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t enqueue_subscription(uint64_t node_id)
{
    if (!s_queue)
    {
        ESP_LOGE(TAG, "Subscription manager not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    // Marking it pending is also what stops the sweep queueing the same node again.
    mark_node_subscription_pending(s_node_manager, node_id);

    if (xQueueSend(s_queue, &node_id, 0) != pdTRUE)
    {
        // Clear the flag again, otherwise the node would look permanently in-flight and the sweep
        // would never pick it back up.
        bool create_new_subscription = false;
        mark_node_has_no_subscription(s_node_manager, node_id, 0, &create_new_subscription);

        ESP_LOGW(TAG, "Subscription queue full; dropped node 0x%016llX (the sweep will retry)", node_id);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Queued subscription for node 0x%016llX", node_id);

    return ESP_OK;
}
