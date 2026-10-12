/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include "nvs_flash.h"
#include "nvs.h"

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_icd_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_ota.h>
#include <common_macros.h>

#include <app/server/Server.h>
#include <credentials/FabricTable.h>

#include <esp_http_server.h>

#include <lwip/sockets.h>
#include <lwip/inet.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cmath>
#include <ctime>
#include <atomic>
#include <string>

#include <esp_matter.h>
#include <lib/dnssd/Types.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>
#include <platform/ESP32/ESP32EndpointQueueFilter.h>
#include <inet/UDPEndPointImpl.h>

#include "cJSON.h"

#include "app_main.h"
#include "managers/node_manager.h"
#include "managers/subscription_manager.h"
#include "managers/calculations_manager.h"
#include "managers/radiator_manager.h"
#include "managers/room_manager.h"
#include "managers/home_manager.h"
#include "managers/heat_pump_manager.h"

#include "heat_meter_cluster.h"
#include "commands/pairing_command.h"
#include "commands/identify_command.h"
#include "commands/commissioning_window_command.h"

#include "app/InteractionModelEngine.h"

#include <setup_payload/ManualSetupPayloadParser.h>
#include <setup_payload/QRCodeSetupPayloadParser.h>

#include "utilities/TokenIterator.h"
#include "utilities/UrlTokenBindings.h"

#include "mqtt/ha_discovery.h"
#include "mqtt/mqtt_settings.h"
#include "storage/history_api.h"
#include "storage/status_api.h"
#include "storage/history_logger.h"
#include "storage/sd_card.h"
#include "storage/time_sync.h"

#include "device_identity.h"
#include "node_api.h"
#include "companion_api.h"
#include "mcp/mcp_server.h"
#include "emoncms/emoncms_uploader.h"

#include "mqtt_client.h"

#include "mdns.h"

static const char *TAG = "app_main";

using chip::NodeId;
using chip::ScopedNodeId;
using chip::SessionHandle;
using chip::Controller::CommissioningParameters;
using chip::Messaging::ExchangeManager;

using namespace esp_matter;
using namespace esp_matter::controller;
using namespace chip::Controller;
using namespace chip;
using namespace chip::app::Clusters;

node_manager_t g_node_manager = {0};
radiator_manager_t g_radiator_manager = {0};
room_manager_t g_room_manager = {0};
home_manager_t g_home_manager = {0};
heat_pump_manager_t g_heat_pump_manager = {0};

esp_mqtt_client_handle_t _mqtt_client;
static bool is_mqtt_connected = false;

static bool has_subscribed_on_startup = false;
static httpd_handle_t server;

static void ws_async_send(void *arg);
static void ws_broadcast_json(cJSON *root);
static void broadcast_home_state(void);
static void broadcast_home_if_home_sensor(uint64_t node_id);
static bool node_is_subscribed(uint64_t node_id);
static void forget_outdoor_temperature_if_unsubscribed(uint64_t node_id);
static void forget_heat_pump_if_unsubscribed(uint64_t node_id);
static void update_heat_pump_running(void);
static void broadcast_radiator_state(const radiator_t *radiator);
static void forget_readings_if_unsubscribed(uint64_t node_id);
static void add_number_or_null(cJSON *object, const char *name, bool has_value, double value);

// Commissioning runs on the Matter task, but POST /api/nodes holds its connection open until
// the outcome is known: the companion app's Matter extension has no other way to learn whether
// pairing worked, and a 202 that quietly turns into nothing leaves iOS claiming the device was
// added when it wasn't. The handler arms the wait, kicks off the pairing command, and parks on
// s_commissioning_done until one of the pairing callbacks signals it.
//
// The companion app gives the request 90 seconds, so time out comfortably inside that.
#define COMMISSIONING_TIMEOUT_MS (75 * 1000)

// TemperatureMeasurement readings outside this range, in hundredths of a degree, are treated as
// no reading. Nothing a heating system measures gets near either end; a disconnected probe
// reads around -80°C.
#define MIN_PLAUSIBLE_TEMPERATURE (-5000)
#define MAX_PLAUSIBLE_TEMPERATURE (10000)

static SemaphoreHandle_t s_commissioning_lock = NULL; // serialises POST /api/nodes
static SemaphoreHandle_t s_commissioning_done = NULL; // given by the pairing callbacks
static std::atomic<bool> s_commissioning_waiting{false};
static bool s_commissioning_succeeded = false;
static CHIP_ERROR s_commissioning_error = CHIP_NO_ERROR;

// Called from the Matter task. The first result wins -- a PASE failure unregisters the pairing
// delegate, so the commissioning callbacks don't necessarily follow it, and a late result from
// an attempt the handler already gave up on must not wake the next one.
static void signal_commissioning_complete(bool succeeded, CHIP_ERROR error)
{
    if (!s_commissioning_waiting.exchange(false))
    {
        return;
    }

    s_commissioning_succeeded = succeeded;
    s_commissioning_error = error;

    xSemaphoreGive(s_commissioning_done);
}

void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data,
                       const chip::app::StatusIB &status);
void attribute_data_read_done(uint64_t remote_node_id, const ScopedMemoryBufferWithSize<AttributePathParams> &attr_path, const ScopedMemoryBufferWithSize<EventPathParams> &event_path);

#pragma region Command Callbacks

static void process_parts_list_attribute_response(uint64_t node_id,
                                                  const chip::app::ConcreteDataAttributePath &path,
                                                  chip::TLV::TLVReader *data,
                                                  const chip::app::StatusIB &status)
{
    ESP_LOGI(TAG, "Endpoint %u: Descriptor->PartsList (endpoint's list)", path.mEndpointId);

    if (!data)
    {
        ESP_LOGE(TAG, "TLVReader is null");
        return;
    }

    chip::TLV::TLVType containerType;

    if (data->EnterContainer(containerType) != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container");
        return;
    }

    matter_node_t *node = find_node(&g_node_manager, node_id);

    while (data->Next() == CHIP_NO_ERROR)
    {
        if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
        {
            uint16_t endpoint_id = 0;

            if (data->Get(endpoint_id) == CHIP_NO_ERROR)
            {
                ESP_LOGI(TAG, "Processing Endpoint ID: %u", endpoint_id);

                add_endpoint(node, endpoint_id);
            }
        }
    }

    data->ExitContainer(containerType);

    // Get all DeviceTypeIDs and Labels
    auto *args = new std::tuple<uint64_t>(node_id);

    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg)
                                                  {
                                                                auto *args = reinterpret_cast<std::tuple<uint64_t> *>(arg);

                                                                // We want to read a few attributes from the Basic Information cluster.
                                                                //
                                                                ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
                                                                attr_paths.Alloc(5);

                                                                if (!attr_paths.Get())
                                                                {
                                                                    ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
                                                                    delete args;
                                                                    return;
                                                                }

                                                                attr_paths[0] = AttributePathParams(Descriptor::Id, Descriptor::Attributes::DeviceTypeList::Id);
                                                                attr_paths[1] = AttributePathParams(FixedLabel::Id, FixedLabel::Attributes::LabelList::Id);
                                                                attr_paths[2] = AttributePathParams(PowerSource::Id, PowerSource::Attributes::FeatureMap::Id);

                                                                // TODO We should only ask for this if we know there were briged nodes.
                                                                attr_paths[3] = AttributePathParams(BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);

                                                                // Semantic tags, for the endpoints of a composed device that have no label of their own.
                                                                attr_paths[4] = AttributePathParams(Descriptor::Id, Descriptor::Attributes::TagList::Id);
                                                                
                                                                ScopedMemoryBufferWithSize<EventPathParams> event_paths;
                                                                event_paths.Alloc(0);

                                                                esp_matter::controller::read_command *read_attr_command = chip::Platform::New<read_command>(std::get<0>(*args),
                                                                                                                                                            std::move(attr_paths),
                                                                                                                                                            std::move(event_paths),
                                                                                                                                                            attribute_data_cb,
                                                                                                                                                            attribute_data_read_done,
                                                                                                                                                            nullptr);

                                                                delete args;
                                                                read_attr_command->send_command(); },
                                                  reinterpret_cast<intptr_t>(args));
}

void node_subscription_established_cb(uint64_t remote_node_id, uint32_t subscription_id)
{
    ESP_LOGI(TAG, "Successfully subscribed, node 0x%016llX, subscription id 0x%08X", remote_node_id, subscription_id);

    // Indicate we have a subscription!
    //
    mark_node_has_subscription(&g_node_manager, remote_node_id, subscription_id);

    // The attempt has resolved, so its in-flight slot can go to the next queued node.
    subscription_attempt_finished(remote_node_id);

    broadcast_home_if_home_sensor(remote_node_id);
}

void node_subscription_terminated_cb(uint64_t remote_node_id, uint32_t subscription_id)
{
    ESP_LOGI(TAG, "Subscription terminated, node 0x%016llX, subscription id 0x%08X", remote_node_id, subscription_id);

    // Indicate we have no subscription!
    //
    bool create_new_subscription = false;

    mark_node_has_no_subscription(&g_node_manager, remote_node_id, subscription_id, &create_new_subscription);

    // Also how an attempt ends that connected but never got as far as establishing -- esp-matter
    // reports that as a termination with subscription id 0. A no-op after an established one.
    subscription_attempt_finished(remote_node_id);

    forget_outdoor_temperature_if_unsubscribed(remote_node_id);
    forget_heat_pump_if_unsubscribed(remote_node_id);
    forget_readings_if_unsubscribed(remote_node_id);

    broadcast_home_if_home_sensor(remote_node_id);

    // The node might still have an active subscription, so only establish another if necessary.
    //
    if (create_new_subscription)
    {
        enqueue_subscription(remote_node_id);
    }
}

void node_subscribe_failed_cb(void *ctx, const chip::ScopedNodeId &peer_id, CHIP_ERROR error)
{
    // ctx is the subscribe_command, which esp-matter deletes as soon as we return. The node we
    // failed to reach is identified by peer_id.
    //
    uint64_t node_id = peer_id.GetNodeId();

    ESP_LOGE(TAG, "Failed to subscribe to node 0x%016llX: %s", node_id, error.AsString());

    // Clear both has_subscription and is_subscription_pending, otherwise the node looks like it
    // still has an attempt in flight and nothing will ever retry it.
    //
    bool create_new_subscription = false;

    mark_node_has_no_subscription(&g_node_manager, node_id, 0, &create_new_subscription);

    subscription_attempt_finished(node_id);

    forget_outdoor_temperature_if_unsubscribed(node_id);
    forget_heat_pump_if_unsubscribed(node_id);
    forget_readings_if_unsubscribed(node_id);

    broadcast_home_if_home_sensor(node_id);

    matter_node_t *node = find_node(&g_node_manager, node_id);

    // A sleepy device is not worth chasing - it will get a subscription when it next checks in.
    // Anything else is retried, but not at once: whatever stopped this attempt is unlikely to have
    // cleared a second later.
    //
    if (node && !node->is_icd)
    {
        enqueue_subscription_after_failure(node_id);
    }
}

static void process_device_type_list_attribute_response(uint64_t node_id,
                                                        const chip::app::ConcreteDataAttributePath &path,
                                                        chip::TLV::TLVReader *data,
                                                        const chip::app::StatusIB &status)
{
    ESP_LOGI(TAG, "Endpoint %u: Descriptor->DeviceTypeList", path.mEndpointId);
    if (!data)
    {
        ESP_LOGE(TAG, "TLVReader is null");
        return;
    }

    chip::TLV::TLVType containerType;

    if (data->EnterContainer(containerType) != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container");
        return;
    }

    matter_node_t *node = find_node(&g_node_manager, node_id);

    while (data->Next() == CHIP_NO_ERROR)
    {
        chip::TLV::TLVType listContainerType;

        if (data->EnterContainer(listContainerType) != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "Failed to enter TLV container");
            return;
        }

        int idx = 0;
        while (data->Next() == CHIP_NO_ERROR)
        {
            // We only care about the first item in the list, which is the DeviceTypeId
            if (idx == 0)
            {
                uint32_t device_type_id = 0;

                if (data->Get(device_type_id) == CHIP_NO_ERROR)
                {
                    ESP_LOGI(TAG, "DeviceTypeID[%d]: %lu", path.mEndpointId, device_type_id);

                    // We're only interested in device types that exist on Endpoints other than the root.
                    if (path.mEndpointId != 0) // RootNode
                    {
                        add_device_type(node, path.mEndpointId, device_type_id);
                    }
                }
            }

            idx++;
        }

        data->ExitContainer(listContainerType);
    }

    data->ExitContainer(containerType);

}

void attribute_data_read_done(uint64_t remote_node_id, const ScopedMemoryBufferWithSize<AttributePathParams> &attr_path, const ScopedMemoryBufferWithSize<EventPathParams> &event_path)
{
    ESP_LOGI(TAG, "\nRead Info done for Nodeid: %016llx  Endpoint: %u Cluster: " ChipLogFormatMEI " Attribute " ChipLogFormatMEI "\n",
             remote_node_id, attr_path[0].mEndpointId, ChipLogValueMEI(attr_path[0].mClusterId), ChipLogValueMEI(attr_path[0].mAttributeId));

    save_nodes_to_nvs(&g_node_manager);

    // Every endpoint's device type list is stored by now, so the subscription worker can work out
    // what this node is worth subscribing to.
    //
    if (node_needs_subscription(&g_node_manager, remote_node_id))
    {
        enqueue_subscription(remote_node_id);
    }
}

void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data,
                       const chip::app::StatusIB &status)
{
    // This handles all teh attribute updates.
    // It doesn't commit the changes to nvs. That is done by attribute_data_read_done, which is called once all the attributes in the Read command are done.
    //
    ChipLogProgress(chipTool, "attribute_data_cb: Nodeid: %016llx Endpoint: %u Cluster: " ChipLogFormatMEI " Attribute " ChipLogFormatMEI " DataVersion: %" PRIu32,
                    remote_node_id, path.mEndpointId, ChipLogValueMEI(path.mClusterId), ChipLogValueMEI(path.mAttributeId),
                    path.mDataVersion.ValueOr(0));

    // "Last seen" for the device list. Stamped before the null-data check below, because a report
    // carrying only a status is still the device answering us. Nothing is recorded until SNTP has
    // set the clock -- time(NULL) is in 1970 before that, and a 1970 date in the UI is worse than
    // no date at all, which the null lastSeen in nodes_get_handler already says plainly.
    //
    if (clock_is_valid())
    {
        mark_node_seen(&g_node_manager, remote_node_id, (uint32_t)time(NULL));
    }

    // ReadClient hands us a null reader whenever the report carries a status rather than a
    // value -- an unsupported attribute, cluster or endpoint, or an access denial. Not every
    // device implements everything we interrogate, so this is expected traffic, not an error.
    // Every branch below dereferences `data`, so bail out before any of them run.
    if (data == nullptr)
    {
        ESP_LOGW(TAG, "No data for cluster " ChipLogFormatMEI " attribute " ChipLogFormatMEI
                      " on node %016llx endpoint %u (status 0x%02x); skipping",
                 ChipLogValueMEI(path.mClusterId), ChipLogValueMEI(path.mAttributeId), remote_node_id,
                 path.mEndpointId, static_cast<unsigned>(status.mStatus));
        return;
    }

    if (path.mEndpointId == 0x0 && path.mClusterId == Descriptor::Id && path.mAttributeId == Descriptor::Attributes::PartsList::Id)
    {
        ESP_LOGI(TAG, "Processing Descriptor->PartsList attribute response...");
        process_parts_list_attribute_response(remote_node_id, path, data, status);
    }
    else if (path.mClusterId == Descriptor::Id && path.mAttributeId == Descriptor::Attributes::DeviceTypeList::Id)
    {
        ESP_LOGI(TAG, "Processing Descriptor->DeviceTypeList attribute response...");
        process_device_type_list_attribute_response(remote_node_id, path, data, status);
    }
    else if (path.mClusterId == FixedLabel::Id && path.mAttributeId == FixedLabel::Attributes::LabelList::Id)
    {
        ESP_LOGI(TAG, "FixedLabel Attribute Value received");

        chip::app::DataModel::DecodableList<chip::app::Clusters::FixedLabel::Structs::LabelStruct::DecodableType> value;
        chip::app::DataModel::Decode(*data, value);

        auto iter = value.begin();
        while (iter.Next())
        {
            auto &item = iter.GetValue();

            char *name = (char *)calloc(item.value.size() + 1, sizeof(char));
            memcpy(name, item.value.data(), item.value.size());

            ESP_LOGI(TAG, "Endpoint [%d] FixedLabel: %s", path.mEndpointId, name);

            matter_node_t *node = find_node(&g_node_manager, remote_node_id);

            set_endpoint_name(node, path.mEndpointId, name);

            free(name);

            break;
        }
    }
    else if (path.mClusterId == Descriptor::Id && path.mAttributeId == Descriptor::Attributes::TagList::Id)
    {
        // The sub-parts of a composed device carry their meaning in semantic tags rather than a
        // FixedLabel -- it is how a heat pump tells its outdoor, flow and return sensors apart. The
        // first tag with a label names the endpoint, unless it has been given a name already.
        //
        chip::app::DataModel::DecodableList<Descriptor::Structs::SemanticTagStruct::DecodableType> value;

        if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "Failed to decode Descriptor->TagList");
            return;
        }

        matter_node_t *node = find_node(&g_node_manager, remote_node_id);

        if (node == NULL)
        {
            return;
        }

        for (uint16_t i = 0; i < node->endpoints_count; i++)
        {
            if (node->endpoints[i].endpoint_id == path.mEndpointId && node->endpoints[i].name)
            {
                return;
            }
        }

        auto iter = value.begin();
        while (iter.Next())
        {
            auto &item = iter.GetValue();

            if (!item.label.HasValue() || item.label.Value().IsNull() || item.label.Value().Value().empty())
            {
                continue;
            }

            chip::CharSpan label = item.label.Value().Value();

            char *name = (char *)calloc(label.size() + 1, sizeof(char));
            memcpy(name, label.data(), label.size());

            ESP_LOGI(TAG, "Endpoint [%d] tag label: %s", path.mEndpointId, name);

            set_endpoint_name(node, path.mEndpointId, name);

            free(name);

            break;
        }
    }
    else if (path.mClusterId == PowerSource::Id)
    {
        ESP_LOGI(TAG, "Processing PowerSource response for EndpointId [%d]...", path.mEndpointId);
        if (path.mAttributeId == PowerSource::Attributes::FeatureMap::Id)
        {
            uint32_t feature_map;
            chip::app::DataModel::Decode(*data, feature_map);

            bool is_wired = feature_map & (uint32_t)PowerSource::Feature::kWired;
            if (is_wired)
            {
                ESP_LOGI(TAG, "PowerSource: WIRED");
            }
            else if ((feature_map & (uint32_t)PowerSource::Feature::kBattery))
            {
                ESP_LOGI(TAG, "PowerSource: BATTERY");
            }

            matter_node_t *node = find_node(&g_node_manager, remote_node_id);

            if (path.mEndpointId == 0x00)
            {
                set_node_power_source(node, is_wired ? POWER_SOURCE_WIRED : POWER_SOURCE_BATTERY);
            }
            else
            {
                set_endpoint_power_source(node, path.mEndpointId, is_wired ? POWER_SOURCE_WIRED : POWER_SOURCE_BATTERY);
            }
        }
        else if (path.mAttributeId == PowerSource::Attributes::BatPercentRemaining::Id)
        {
            // Nullable, and reported in half percent, so 200 is a full battery.
            //
            chip::app::DataModel::Nullable<uint8_t> half_percent;

            if (chip::app::DataModel::Decode(*data, half_percent) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to decode BatPercentRemaining");
                return;
            }

            bool has_value = !half_percent.IsNull();
            uint8_t percent = has_value ? (uint8_t)(half_percent.Value() / 2) : 0;

            if (has_value)
            {
                ESP_LOGI(TAG, "Battery: %u%%", percent);
            }
            else
            {
                ESP_LOGI(TAG, "Battery percentage is unknown");
            }

            set_battery_percent(&g_node_manager, remote_node_id, path.mEndpointId, has_value, percent);
        }
        else if (path.mAttributeId == PowerSource::Attributes::BatVoltage::Id)
        {
            chip::app::DataModel::Nullable<uint32_t> voltage_mv;

            if (chip::app::DataModel::Decode(*data, voltage_mv) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to decode BatVoltage");
                return;
            }

            bool has_value = !voltage_mv.IsNull();

            if (has_value)
            {
                ESP_LOGI(TAG, "Battery: %lumV", voltage_mv.Value());
            }
            else
            {
                ESP_LOGI(TAG, "Battery voltage is unknown");
            }

            set_battery_voltage(&g_node_manager, remote_node_id, path.mEndpointId, has_value, has_value ? voltage_mv.Value() : 0);
        }
    }
    else if (path.mClusterId == BasicInformation::Id)
    {
        ESP_LOGI(TAG, "Processing BasicInformation response...");

        matter_node_t *node = find_node(&g_node_manager, remote_node_id);

        if (node == NULL)
        {
            // The read outlived the node -- it was removed while the report was in flight.
            ESP_LOGE(TAG, "No node %llu to store Basic Information against", remote_node_id);
            return;
        }

        if (path.mAttributeId == BasicInformation::Attributes::VendorName::Id)
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;

                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    free(node->vendor_name);
                    node->vendor_name = (char *)calloc(value.size() + 1, sizeof(char));
                    memcpy(node->vendor_name, value.data(), value.size());

                    ESP_LOGI(TAG, "Vendor Name: %s", node->vendor_name);
                }
            }
        }
        else if (path.mAttributeId == BasicInformation::Attributes::ProductName::Id)
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;

                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    free(node->product_name);
                    node->product_name = (char *)calloc(value.size() + 1, sizeof(char));
                    memcpy(node->product_name, value.data(), value.size());

                    ESP_LOGI(TAG, "Product Name: %s", node->product_name);
                }
            }
        }
        else if (path.mAttributeId == BasicInformation::Attributes::NodeLabel::Id) // This Cluster is only on Endpoint 0x00
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;

                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    char *node_label = (char *)calloc(value.size() + 1, sizeof(char));
                    memcpy(node_label, value.data(), value.size());

                    set_node_label(node, node_label);

                    free(node_label);
                }
            }
        }
    }
    else if (path.mClusterId == BridgedDeviceBasicInformation::Id && path.mAttributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id)
    {
        ESP_LOGI(TAG, "Processing BridgedDeviceBasicInformation->NodeLabel attribute response...");

        if (data->GetType() == chip::TLV::kTLVType_UTF8String)
        {
            chip::CharSpan value;

            if (data->Get(value) == CHIP_NO_ERROR)
            {
                char *node_label = (char *)calloc(value.size() + 1, sizeof(char));
                memcpy(node_label, value.data(), value.size());

                ESP_LOGI(TAG, "Bridged Node Label: %s", node_label);

                matter_node_t *node = find_node(&g_node_manager, remote_node_id);

                set_endpoint_name(node, path.mEndpointId, node_label);

                free(node_label);
            }
        }
    }
    else if (path.mClusterId == ThreadNetworkDiagnostics::Id && path.mAttributeId == ThreadNetworkDiagnostics::Attributes::ExtAddress::Id)
    {
        ESP_LOGI(TAG, "Processing ThreadNetworkDiagnostics->ExtAddress attribute response...");

        uint64_t extAddress = 0;
        CHIP_ERROR decode_err = chip::app::DataModel::Decode(*data, extAddress);

        if (decode_err != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "Could not decode ExtAddress: %" CHIP_ERROR_FORMAT, decode_err.Format());
        }
        else
        {
            matter_node_t *node = find_node(&g_node_manager, remote_node_id);

            // set_node_ext_address() dereferences without checking, unlike most of the other
            // node_manager setters, and a report can arrive for a node we have not recorded.
            if (node)
            {
                set_node_ext_address(node, extAddress);
            }
            else
            {
                ESP_LOGW(TAG, "ExtAddress for unknown node %016llx; dropping", remote_node_id);
            }
        }
    }
    else if (path.mClusterId == ElectricalPowerMeasurement::Id)
    {
        // Voltage, ActiveCurrent and ActivePower are all nullable int64s, in mV, mA and mW.
        //
        chip::app::DataModel::Nullable<int64_t> value;

        if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "Failed to decode ElectricalPowerMeasurement attribute 0x%08lX", (unsigned long)path.mAttributeId);
            return;
        }

        bool has_value = !value.IsNull();
        int64_t reading = has_value ? value.Value() : 0;

        bool is_home_meter = (g_home_manager.electrical_meter_node_id == remote_node_id &&
                              g_home_manager.electrical_meter_endpoint_id == path.mEndpointId);

        // A heat pump is an Electrical Sensor as well, reporting the power it draws. That is kept
        // whether or not the heat pump has been picked for the home, so the Heat Pump section has a
        // figure as soon as one is: a subscription only reports a value again when it changes.
        //
        if (path.mAttributeId == ElectricalPowerMeasurement::Attributes::ActivePower::Id &&
            node_has_device_type(find_node(&g_node_manager, remote_node_id), DEVICE_TYPE_HEAT_PUMP))
        {
            heat_pump_endpoint_t *heat_pump = heat_pump_get_endpoint(&g_heat_pump_manager, remote_node_id, path.mEndpointId);

            if (heat_pump)
            {
                heat_pump->has_active_power = has_value;
                heat_pump->active_power_mw = reading;
            }

            if (!is_home_meter && g_home_manager.heat_pump_node_id == remote_node_id && g_home_manager.heat_pump_endpoint_id == path.mEndpointId)
            {
                broadcast_home_state();
            }
        }

        // Beyond that, only the endpoint the user picked as the home's electrical meter is of
        // interest: history records the home's quantities, not each device's, so an unbound
        // endpoint has nowhere to go.
        //
        if (!is_home_meter)
        {
            return;
        }

        switch (path.mAttributeId)
        {
        case ElectricalPowerMeasurement::Attributes::Voltage::Id:
            ESP_LOGI(TAG, "Electrical meter voltage: %lld mV", reading);
            g_home_manager.has_electrical_voltage = has_value;
            g_home_manager.electrical_voltage_mv = reading;
            break;

        case ElectricalPowerMeasurement::Attributes::ActiveCurrent::Id:
            ESP_LOGI(TAG, "Electrical meter current: %lld mA", reading);
            g_home_manager.has_electrical_current = has_value;
            g_home_manager.electrical_current_ma = reading;
            break;

        case ElectricalPowerMeasurement::Attributes::ActivePower::Id:
            ESP_LOGI(TAG, "Electrical meter power: %lld mW", reading);
            g_home_manager.has_electrical_power = has_value;
            g_home_manager.electrical_power_mw = reading;
            break;

        default:
            ESP_LOGI(TAG, "Unhandled ElectricalPowerMeasurement attribute 0x%08lX", (unsigned long)path.mAttributeId);
            return;
        }

        // The meter feeds the home's electricity figures, so push them rather than waiting for the
        // next GET /api/home.
        //
        broadcast_home_state();
    }
    else if (path.mClusterId == HEAT_METER_CLUSTER_ID)
    {
        // The M-Bus adapter's manufacturer-specific Heat Meter cluster. Every heat meter endpoint
        // is cached for the history logger; only the endpoint the user picked as the home's meter
        // feeds the live home figures.
        //
        bool is_home_meter = (g_home_manager.heat_meter_node_id == remote_node_id &&
                              g_home_manager.heat_meter_endpoint_id == path.mEndpointId);

        // Unlike ElectricalPowerMeasurement the four attributes are different types, so each one is
        // decoded into the type the cluster declares for it rather than a shared int64.
        //
        switch (path.mAttributeId)
        {
        case HM_ATTR_FLOW_ID:
        {
            chip::app::DataModel::Nullable<int32_t> value;

            if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to decode Heat Meter flow");
                return;
            }

            // The cluster reports litres per hour, which is also the unit the history record
            // uses, so the reading crosses unscaled.
            if (!is_home_meter)
            {
                break;
            }

            g_home_manager.has_heat_meter_flow = !value.IsNull();
            g_home_manager.heat_meter_flow = value.IsNull() ? 0 : (uint16_t)value.Value();

            if (value.IsNull())
            {
                ESP_LOGI(TAG, "Heat meter flow: null");
            }
            else
            {
                ESP_LOGI(TAG, "Heat meter flow: %ld l/h", (long)value.Value());
            }
            break;
        }

        case HM_ATTR_FLOW_TEMP_ID:
        case HM_ATTR_RETURN_TEMP_ID:
        {
            chip::app::DataModel::Nullable<int32_t> value;

            if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to decode Heat Meter temperature 0x%08lX", (unsigned long)path.mAttributeId);
                return;
            }

            bool has_value = !value.IsNull();
            int32_t reading = has_value ? value.Value() : 0;

            if (!is_home_meter)
            {
                break;
            }

            // Null is logged as such: the adapter publishes null until it has read the meter, and a
            // bare 0 is indistinguishable from a real reading.
            const char *which = path.mAttributeId == HM_ATTR_FLOW_TEMP_ID ? "flow" : "return";

            if (has_value)
            {
                ESP_LOGI(TAG, "Heat meter %s temperature: %ld (0.01 degC)", which, (long)reading);
            }
            else
            {
                ESP_LOGI(TAG, "Heat meter %s temperature: null", which);
            }

            if (path.mAttributeId == HM_ATTR_FLOW_TEMP_ID)
            {
                g_home_manager.has_heat_meter_flow_temperature = has_value;
                g_home_manager.heat_meter_flow_temperature = reading;
            }
            else
            {
                g_home_manager.has_heat_meter_return_temperature = has_value;
                g_home_manager.heat_meter_return_temperature = reading;
            }

            break;
        }

        case HM_ATTR_POWER_ID:
        {
            chip::app::DataModel::Nullable<int64_t> value;

            if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to decode Heat Meter power");
                return;
            }

            if (!is_home_meter)
            {
                break;
            }

            g_home_manager.has_heat_meter_power = !value.IsNull();
            g_home_manager.heat_meter_power_mw = value.IsNull() ? 0 : value.Value();

            if (value.IsNull())
            {
                ESP_LOGI(TAG, "Heat meter power: null");
            }
            else
            {
                ESP_LOGI(TAG, "Heat meter power: %lld mW", g_home_manager.heat_meter_power_mw);
            }
            break;
        }

        default:
            ESP_LOGI(TAG, "Unhandled Heat Meter attribute 0x%08lX", (unsigned long)path.mAttributeId);
            return;
        }

        // The meter feeds the home's heat source figures, so recalculate and push rather than waiting
        // for the next GET /api/home.
        //
        if (is_home_meter)
        {
            broadcast_home_state();
        }
    }
    else if (path.mClusterId == Thermostat::Id || path.mClusterId == WaterHeaterManagement::Id || path.mClusterId == WaterHeaterMode::Id)
    {
        // Only heat pump nodes are subscribed to these: a heating loop is a Thermostat, and the hot
        // water tank a Water Heater, which is a Thermostat plus Water Heater Management and Water
        // Heater Mode. Kept for
        // every such node rather than only the one picked for the home, for the same reason as the
        // heat pump's power above.
        //
        heat_pump_endpoint_t *heat_pump = heat_pump_get_endpoint(&g_heat_pump_manager, remote_node_id, path.mEndpointId);

        if (heat_pump == NULL)
        {
            return;
        }

        if (path.mClusterId == Thermostat::Id)
        {
            switch (path.mAttributeId)
            {
            case Thermostat::Attributes::LocalTemperature::Id:
            {
                // Null when the loop has no room thermostat fitted.
                chip::app::DataModel::Nullable<int16_t> value;

                if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to decode Thermostat LocalTemperature");
                    return;
                }

                heat_pump->has_local_temperature = !value.IsNull();
                heat_pump->local_temperature = value.IsNull() ? 0 : value.Value();
                break;
            }

            case Thermostat::Attributes::OccupiedHeatingSetpoint::Id:
            {
                int16_t value;

                if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to decode Thermostat OccupiedHeatingSetpoint");
                    return;
                }

                heat_pump->has_heating_setpoint = true;
                heat_pump->heating_setpoint = value;
                break;
            }

            case Thermostat::Attributes::SystemMode::Id:
            {
                uint8_t value;

                if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to decode Thermostat SystemMode");
                    return;
                }

                heat_pump->has_system_mode = true;
                heat_pump->system_mode = value;
                break;
            }

            case Thermostat::Attributes::ThermostatRunningState::Id:
            {
                uint16_t value;

                if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to decode Thermostat ThermostatRunningState");
                    return;
                }

                heat_pump->has_running_state = true;
                heat_pump->running_state = value;
                break;
            }

            default:
                ESP_LOGI(TAG, "Unhandled Thermostat attribute 0x%08lX", (unsigned long)path.mAttributeId);
                return;
            }
        }
        else if (path.mClusterId == WaterHeaterMode::Id)
        {
            switch (path.mAttributeId)
            {
            case WaterHeaterMode::Attributes::SupportedModes::Id:
            {
                // The mode values are the device's own, so this is what turns CurrentMode into
                // something to show. Only the value and label are kept; the mode tags are not.
                chip::app::DataModel::DecodableList<WaterHeaterMode::Structs::ModeOptionStruct::DecodableType> value;

                if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to decode WaterHeaterMode SupportedModes");
                    return;
                }

                heat_pump->supported_mode_count = 0;

                auto iter = value.begin();
                while (iter.Next() && heat_pump->supported_mode_count < HEAT_PUMP_MAX_MODES)
                {
                    auto &item = iter.GetValue();

                    heat_pump_mode_t *mode = &heat_pump->supported_modes[heat_pump->supported_mode_count];

                    size_t length = item.label.size() < HEAT_PUMP_MODE_LABEL_LENGTH ? item.label.size() : HEAT_PUMP_MODE_LABEL_LENGTH;

                    mode->mode = item.mode;
                    memcpy(mode->label, item.label.data(), length);
                    mode->label[length] = '\0';

                    ESP_LOGI(TAG, "Water heater mode %u: %s", mode->mode, mode->label);

                    heat_pump->supported_mode_count++;
                }
                break;
            }

            case WaterHeaterMode::Attributes::CurrentMode::Id:
            {
                uint8_t value;

                if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to decode WaterHeaterMode CurrentMode");
                    return;
                }

                heat_pump->has_water_heater_mode = true;
                heat_pump->water_heater_mode = value;
                break;
            }

            default:
                ESP_LOGI(TAG, "Unhandled WaterHeaterMode attribute 0x%08lX", (unsigned long)path.mAttributeId);
                return;
            }
        }
        else
        {
            // HeatDemand is a bitmap8 and BoostState an enum8.
            uint8_t value;

            if (chip::app::DataModel::Decode(*data, value) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to decode WaterHeaterManagement attribute 0x%08lX", (unsigned long)path.mAttributeId);
                return;
            }

            switch (path.mAttributeId)
            {
            case WaterHeaterManagement::Attributes::HeatDemand::Id:
                heat_pump->has_heat_demand = true;
                heat_pump->heat_demand = value;
                break;

            case WaterHeaterManagement::Attributes::BoostState::Id:
                heat_pump->has_boost_state = true;
                heat_pump->boost_state = value;
                break;

            default:
                ESP_LOGI(TAG, "Unhandled WaterHeaterManagement attribute 0x%08lX", (unsigned long)path.mAttributeId);
                return;
            }
        }

        ESP_LOGI(TAG, "Heat pump endpoint %u: cluster 0x%04lX attribute 0x%04lX updated", path.mEndpointId,
                 (unsigned long)path.mClusterId, (unsigned long)path.mAttributeId);

        if (g_home_manager.heat_pump_node_id == remote_node_id)
        {
            update_heat_pump_running();
            broadcast_home_state();
        }
    }
    else if (path.mClusterId == TemperatureMeasurement::Id && path.mAttributeId == TemperatureMeasurement::Attributes::MeasuredValue::Id)
    {
        ESP_LOGI(TAG, "Processing TemperatureMeasurement->MeasuredValue attribute response...");

        // Nullable: a sensor reports null when it has no reading, e.g. a probe that isn't connected.
        //
        chip::app::DataModel::Nullable<int16_t> measured_value;

        if (chip::app::DataModel::Decode(*data, measured_value) != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "Failed to decode MeasuredValue");
            return;
        }

        bool has_value = !measured_value.IsNull();

        if (has_value && (measured_value.Value() < MIN_PLAUSIBLE_TEMPERATURE || measured_value.Value() > MAX_PLAUSIBLE_TEMPERATURE))
        {
            ESP_LOGW(TAG, "Ignoring implausible Temperature Value: %d", measured_value.Value());
            has_value = false;
        }

        // A probe that has stopped reporting clears its last value rather than leaving it to feed
        // the calculations.
        //
        int16_t temperature = has_value ? measured_value.Value() : 0;

        if (has_value)
        {
            ESP_LOGI(TAG, "Temperature Value: %d", temperature);
        }
        else
        {
            ESP_LOGI(TAG, "Temperature Value is unknown");
        }

        set_endpoint_measured_value(&g_node_manager, remote_node_id, path.mEndpointId, has_value, temperature);

        bool hasMatched = false;

        // If this TemperatureMeasurement is set in the home, update it's value.
        //
        if (g_home_manager.outdoor_temp_node_id == remote_node_id && g_home_manager.outdoor_temp_endpoint_id == path.mEndpointId)
        {
            ESP_LOGI(TAG, "Device is assigned to the outdoor temperature sensor");

            g_home_manager.has_outdoor_temperature = has_value;
            g_home_manager.outdoor_temperature = temperature;

            // A change in outside temperature impacts all the rooms.
            //
            update_all_rooms_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

            hasMatched = true;
        }

        if (hasMatched)
        {
            broadcast_home_state();
            return;
        }

        // Find the appropriate radiator for this node/endpoint combination.
        //
        radiator_t *radiator = g_radiator_manager.radiator_list;

        while (radiator)
        {
            if (radiator->flow_temp_node_id == remote_node_id || radiator->return_temp_node_id == remote_node_id)
            {
                ESP_LOGI(TAG, "Device is assigned to radiator %u", radiator->radiator_id);

                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "channel", "radiator");
                cJSON_AddNumberToObject(root, "radiatorId", radiator->radiator_id);

                if (radiator->flow_temp_endpoint_id == path.mEndpointId)
                {
                    ESP_LOGI(TAG, "Reading Flow Temperature value");
                    radiator->has_flow_temperature = has_value;
                    radiator->flow_temperature = temperature;
                    add_number_or_null(root, "flowTemp", has_value, temperature);
                }
                else if (radiator->return_temp_endpoint_id == path.mEndpointId)
                {
                    ESP_LOGI(TAG, "Reading Return Temperature value");
                    radiator->has_return_temperature = has_value;
                    radiator->return_temperature = temperature;
                    add_number_or_null(root, "returnTemp", has_value, temperature);
                }

                update_radiator_outputs(&g_node_manager, &g_home_manager, &g_radiator_manager, &g_room_manager, _mqtt_client, radiator);

                // Added after the recalculation, not with the fields above, or the push would carry
                // the output from the previous reading. The Layout schematic shows watts per radiator
                // and only ever sees this channel, so a stale figure would sit there until a reload.
                //
                add_number_or_null(root, "currentOutput", radiator->has_heat_output, radiator->heat_output);

                ws_broadcast_json(root);

                cJSON_Delete(root);

                // The radiator's output feeds totalRadiatorOutput and the measured heat loss totals.
                broadcast_home_state();

                hasMatched = true;

                break;
            }

            radiator = radiator->next;
        }

        // Find the appropriate room for this node/endpoint combination.
        //
        room_t *room = g_room_manager.room_list;

        while (room)
        {
            if (room->room_temperature_node_id == remote_node_id && room->room_temperature_endpoint_id == path.mEndpointId)
            {
                ESP_LOGI(TAG, "Device is assigned to room %u", room->room_id);

                update_room_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client, room);

                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "channel", "room");
                cJSON_AddNumberToObject(root, "roomId", room->room_id);
                add_number_or_null(root, "temperature", room->has_current_temperature, room->current_temperature);

                ws_broadcast_json(root);

                cJSON_Delete(root);

                // The room's heat loss feeds the home totals.
                broadcast_home_state();

                hasMatched = true;

                break;
            }

            room = room->next;
        }

        // One of the heat pump's own sensors, which the Heat Pump section shows. Anything matched
        // above has pushed the home already.
        //
        if (!hasMatched && g_home_manager.heat_pump_node_id == remote_node_id)
        {
            broadcast_home_state();
        }
    }
    else
    {
        ESP_LOGI(TAG, "Unhandled attribute_data_cb update");
    }
}

// A PASE failure aborts commissioning on its own and unregisters the pairing delegate, so the
// commissioning failure callback never runs. Report it here instead.
static void on_pase_complete_callback(CHIP_ERROR error)
{
    if (error != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "PASE session establishment failed: %s", ErrorStr(error));
        signal_commissioning_complete(false, error);
    }
}

static void on_commissioning_success_callback(ScopedNodeId peer_id)
{
    ESP_LOGI(TAG, "commissioning_success_callback invoked!");

    uint64_t nodeId = peer_id.GetNodeId();
    char nodeIdStr[22];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "%" PRIu64, nodeId);

    bool is_icd_device = false;

    auto &icd_client_storage = esp_matter::controller::matter_controller_client::get_instance().get_icd_client_storage();
    auto iter = icd_client_storage.IterateICDClientInfo();

    if (iter != nullptr)
    {
        // The iterator comes out of a pool that only has one slot
        // (CHIP_CONFIG_MAX_ICD_CLIENTS_INFO_STORAGE_CONCURRENT_ITERATORS), so it has to be
        // released on every path -- including the early break below. Leaking it wedges the pool
        // and every later ProcessCheckInPayload fails with CHIP_ERROR_NO_MEMORY, silently
        // dropping ICD check-ins.
        app::DefaultICDClientStorage::ICDClientInfoIteratorWrapper wrapper(iter);
        app::ICDClientInfo info;
        while (iter->Next(info))
        {
            if (info.peer_node.GetNodeId() == nodeId)
            {
                ESP_LOGI(TAG, "Device is ICD");
                is_icd_device = true;
                break;
            }
        }
    }

    add_node(&g_node_manager, nodeId, is_icd_device);

    save_nodes_to_nvs(&g_node_manager);

    // Answer the waiting POST /api/nodes now the node is in the list. Reading its Basic
    // Information below only fills in the name and endpoints, which the client doesn't
    // have to wait for.
    signal_commissioning_complete(true, CHIP_NO_ERROR);

    // Query some of the node properties.
    //
    uint16_t endpointId = 0x0000;

    auto *args = new std::tuple<uint64_t, uint16_t>(nodeId, endpointId);

    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg)
                                                  {
        auto *args = reinterpret_cast<std::tuple<uint64_t, uint16_t> *>(arg);
                                                    
        // We want to read a few attributes from the Basic Information cluster.
        //
        ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
        attr_paths.Alloc(5);

        if (!attr_paths.Get())
        {
            ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
            delete args;
            return;
        }

        attr_paths[0] = AttributePathParams(std::get<1>(*args), BasicInformation::Id, BasicInformation::Attributes::VendorName::Id);
        attr_paths[1] = AttributePathParams(std::get<1>(*args), BasicInformation::Id, BasicInformation::Attributes::ProductName::Id);
        attr_paths[2] = AttributePathParams(std::get<1>(*args), BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id);
        attr_paths[3] = AttributePathParams(std::get<1>(*args), Descriptor::Id, Descriptor::Attributes::PartsList::Id);
        attr_paths[4] = AttributePathParams(std::get<1>(*args), ThreadNetworkDiagnostics::Id, ThreadNetworkDiagnostics::Attributes::ExtAddress::Id);

        ScopedMemoryBufferWithSize<EventPathParams> event_paths;
        event_paths.Alloc(0);
    
        esp_matter::controller::read_command *read_attr_command = chip::Platform::New<read_command>(std::get<0>(*args),
                                                                                                    std::move(attr_paths),
                                                                                                    std::move(event_paths),
                                                                                                    attribute_data_cb,
                                                                                                    attribute_data_read_done,
                                                                                                    nullptr);

        delete args;
        read_attr_command->send_command(); }, reinterpret_cast<intptr_t>(args));
}

static void on_commissioning_failure_callback(ScopedNodeId peer_id,
                                              CHIP_ERROR error,
                                              chip::Controller::CommissioningStage stage,
                                              std::optional<chip::Credentials::AttestationVerificationResult> addtional_err_info)
{
    ESP_LOGE(TAG, "Commissioning failed at stage %d: %s", static_cast<int>(stage), ErrorStr(error));

    signal_commissioning_complete(false, error);
}

static void on_unpair_complete_callback(NodeId removed_node, CHIP_ERROR error)
{
    if (error == CHIP_NO_ERROR)
    {
        ESP_LOGI(TAG, "Unpairing successful for NodeId: %llu", removed_node);
    }
    else
    {
        ESP_LOGE(TAG, "Unpairing failed for NodeId: %llu, error: %s", removed_node, ErrorStr(error));
    }
}

static void on_icd_checkin_callback(const chip::app::ICDClientInfo &clientInfo)
{
    // A check-in from an ICD device means it has no subscriptions.
    //
    ESP_LOGI(TAG, "on_icd_checkin_callback invoked from node %llu!", clientInfo.peer_node.GetNodeId());

    bool create_new_subscription = false;

    mark_node_has_no_subscription(&g_node_manager, clientInfo.peer_node.GetNodeId(), 0, &create_new_subscription);

    broadcast_home_if_home_sensor(clientInfo.peer_node.GetNodeId());

    save_nodes_to_nvs(&g_node_manager);

    if (create_new_subscription)
    {
        ESP_LOGI(TAG, "Queueing subscription after ICD CheckIn received...");

        enqueue_subscription(clientInfo.peer_node.GetNodeId());
    }
}

#pragma endregion

#pragma region WebServer

#define STACK_SIZE 200

static int char_to_int(char ch)
{
    if ('A' <= ch && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    else if ('a' <= ch && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    else if ('0' <= ch && ch <= '9')
    {
        return ch - '0';
    }
    return -1;
}

static bool convert_hex_str_to_bytes(const char *hex_str, uint8_t *bytes, uint8_t &bytes_len)
{
    if (!hex_str)
    {
        return false;
    }
    size_t hex_str_len = strlen(hex_str);
    if (hex_str_len == 0 || hex_str_len % 2 != 0 || hex_str_len / 2 > bytes_len)
    {
        return false;
    }
    bytes_len = hex_str_len / 2;
    for (size_t i = 0; i < bytes_len; ++i)
    {
        int byte_h = char_to_int(hex_str[2 * i]);
        int byte_l = char_to_int(hex_str[2 * i + 1]);
        if (byte_h < 0 || byte_l < 0)
        {
            return false;
        }
        bytes[i] = (byte_h << 4) + byte_l;
    }
    return true;
}

static void ws_async_send(void *arg)
{
    // We own the payload: httpd_queue_work runs this on the server task some time after it was
    // queued, so the caller can't free it. It came from cJSON_PrintUnformatted.
    char *payload = (char *)arg;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t *)payload;
    ws_pkt.len = strlen(payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;

    // Every open tab gets the frame, rather than only whichever one connected last. The client list
    // carries the plain HTTP sockets too, and httpd_ws_get_fd_info reports a fd as a websocket only
    // once its handshake has completed, so it is both the filter and the readiness check.
    //
    size_t fds = CONFIG_LWIP_MAX_SOCKETS;
    int client_fds[CONFIG_LWIP_MAX_SOCKETS];

    if (httpd_get_client_list(server, &fds, client_fds) == ESP_OK)
    {
        for (size_t i = 0; i < fds; i++)
        {
            if (httpd_ws_get_fd_info(server, client_fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
            {
                continue;
            }

            esp_err_t err = httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);

            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send websocket frame to fd %d: %s; closing it", client_fds[i], esp_err_to_name(err));

                // A tab that went away without a close frame (sleep, network drop) leaves its socket
                // in the client list, and every later broadcast would fail on it again. The browser
                // reconnects if it is in fact still there.
                //
                httpd_sess_trigger_close(server, client_fds[i]);
            }
        }
    }

    cJSON_free(payload);
}

// Queues a JSON object for broadcast to every websocket client. The caller keeps ownership of root.
//
static void ws_broadcast_json(cJSON *root)
{
    // Matter attribute reports can arrive before Ethernet is up and start_webserver() has run.
    //
    if (server == NULL)
    {
        return;
    }

    char *payload = cJSON_PrintUnformatted(root);

    if (payload == NULL)
    {
        return;
    }

    // ws_async_send frees the payload, unless it never gets queued.
    if (httpd_queue_work(server, ws_async_send, payload) != ESP_OK)
    {
        cJSON_free(payload);
    }
}

static esp_err_t ws_get_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    // This has to be returned, not just logged. When the peer vanishes without a CLOSE frame, recv()
    // fails with ENOTCONN, but httpd compares that negative return against a size_t and misses it, so
    // it hands us a zero "frame" that then fails the mask check. Returning ESP_OK here left the dead
    // socket open, and select() kept reporting it readable -- a tight loop that flooded the log.
    // A non-OK return is what makes httpd close the session.
    //
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (ws_pkt.len)
    {
        uint8_t *buf = NULL;

        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = (uint8_t *)calloc(ws_pkt.len + 1, sizeof(uint8_t));

        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        // Nothing the client sends is acted on -- the browser only ever opens the socket to listen --
        // so the frame is read to drain it and then dropped.
        free(buf);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            return ret;
        }
    }

    return ESP_OK;
}

// Reads a request body and parses it as JSON. Every handler wants the same thing and most of the
// older ones get it subtly wrong -- httpd_req_recv() does not NUL-terminate, and a buffer sized
// exactly content_len leaves cJSON_Parse() reading past the end.
cJSON *read_json_body(httpd_req_t *req)
{
    char content[req->content_len + 1];
    int received = httpd_req_recv(req, content, req->content_len);

    if (received <= 0)
    {
        ESP_LOGE(TAG, "Failed to read the request body");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Could not read request body", HTTPD_RESP_USE_STRLEN);
        return NULL;
    }

    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return NULL;
    }

    return root;
}

// Commissions a device onto the fabric. Shared by POST /api/nodes and the companion app's
// POST /api/companion/nodes -- see node_api.h.
esp_err_t commission_node(const char *setup_code, commission_outcome_t *outcome)
{
    memset(outcome, 0, sizeof(*outcome));

    uint64_t node_id = get_next_node_id(&g_node_manager);

    if (node_id == 0)
    {
        ESP_LOGE(TAG, "Failed to get a valid node ID for commissioning");
        outcome->http_status = 500;
        snprintf(outcome->message, sizeof(outcome->message), "Failed to get a valid node ID");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Will use %llu as node ID", node_id);

    outcome->node_id = node_id;

    if (s_commissioning_lock == NULL || s_commissioning_done == NULL)
    {
        ESP_LOGE(TAG, "Commissioning synchronisation was never set up");
        outcome->http_status = 500;
        snprintf(outcome->message, sizeof(outcome->message), "Commissioning is unavailable");
        return ESP_OK;
    }

    // The commissioner only runs one pairing process at a time, and there is a single slot for
    // its result, so turn a second request away rather than letting it wait behind the first.
    if (xSemaphoreTake(s_commissioning_lock, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "A commissioning request is already in progress");
        outcome->http_status = 409;
        snprintf(outcome->message, sizeof(outcome->message), "A commissioning request is already in progress");
        return ESP_OK;
    }

    heating_monitor::controller::pairing_command_callbacks_t callbacks = {
        .pase_callback = on_pase_complete_callback,
        .commissioning_success_callback = on_commissioning_success_callback,
        .commissioning_failure_callback = on_commissioning_failure_callback};

    heating_monitor::controller::pairing_command::get_instance().set_callbacks(callbacks);

    // Commissioning is always OnNetwork: the companion app drives this through MatterSupport,
    // so iOS Home has already joined the device to Thread and opened a commissioning window
    // before we are asked to pair. The device is reachable over the border router by the time
    // we get here, and we never need BLE or the operational dataset ourselves.
    ESP_LOGI(TAG, "Using OnNetwork discovery");

    // Arm the wait before starting so a callback can't land before we're listening -- PairDevice
    // can fail synchronously. Drain the semaphore first in case a previous attempt was signalled
    // after its handler had already given up on it.
    xSemaphoreTake(s_commissioning_done, 0);
    s_commissioning_waiting = true;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t pairing_err = heating_monitor::controller::pairing_code(node_id, setup_code);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (pairing_err != ESP_OK)
    {
        s_commissioning_waiting = false;
        xSemaphoreGive(s_commissioning_lock);

        ESP_LOGE(TAG, "Failed to start commissioning: %s", esp_err_to_name(pairing_err));

        // ESP_ERR_INVALID_STATE means the commissioner still has a pairing delegate registered
        // from an attempt that never finished.
        outcome->http_status = pairing_err == ESP_ERR_INVALID_STATE ? 409 : 500;
        snprintf(outcome->message, sizeof(outcome->message), "Failed to start commissioning");
        return ESP_OK;
    }

    // Block until commissioning finishes. This parks the HTTP server's only task, so nothing
    // else is served -- web UI included -- until we answer.
    ESP_LOGI(TAG, "Waiting up to %d seconds for commissioning to complete...", COMMISSIONING_TIMEOUT_MS / 1000);

    bool signalled = xSemaphoreTake(s_commissioning_done, pdMS_TO_TICKS(COMMISSIONING_TIMEOUT_MS)) == pdTRUE;
    bool succeeded = signalled && s_commissioning_succeeded;
    CHIP_ERROR commissioning_error = signalled ? s_commissioning_error : CHIP_NO_ERROR;

    if (!signalled)
    {
        // Give up on this attempt and cancel it, so the pairing delegate is released. Without
        // this, an attempt that never calls back leaves every later request rejected with
        // "There is already a pairing process" until the device reboots.
        s_commissioning_waiting = false;

        auto *commissioner = matter_controller_client::get_instance().get_commissioner();

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        CHIP_ERROR stop_err = commissioner->StopPairing(node_id);

        if (stop_err != CHIP_NO_ERROR)
        {
            // Nothing is going to clear the delegate for us now, so do it by hand.
            ESP_LOGE(TAG, "StopPairing failed: %s", ErrorStr(stop_err));
            commissioner->RegisterPairingDelegate(nullptr);
        }
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }

    xSemaphoreGive(s_commissioning_lock);

    if (!signalled)
    {
        ESP_LOGE(TAG, "Commissioning of node %llu timed out", node_id);
        outcome->http_status = 504;
        snprintf(outcome->message, sizeof(outcome->message), "The device didn't finish commissioning in time");
        return ESP_OK;
    }

    if (!succeeded)
    {
        ESP_LOGE(TAG, "Commissioning of node %llu failed: %s", node_id, ErrorStr(commissioning_error));
        outcome->http_status = 502;
        snprintf(outcome->message, sizeof(outcome->message), "Commissioning failed: %s", ErrorStr(commissioning_error));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Commissioning of node %llu completed", node_id);

    outcome->http_status = 201;
    return ESP_OK;
}

esp_err_t send_commission_outcome(httpd_req_t *req, const commission_outcome_t *outcome)
{
    if (outcome->http_status != 201)
    {
        // httpd_resp_set_status() keeps the pointer rather than copying, so these have to be
        // literals with static storage, not a snprintf'd buffer on this stack frame.
        switch (outcome->http_status)
        {
        case 409:
            httpd_resp_set_status(req, "409 Conflict");
            break;
        case 502:
            httpd_resp_set_status(req, "502 Bad Gateway");
            break;
        case 504:
            httpd_resp_set_status(req, "504 Gateway Timeout");
            break;
        default:
            httpd_resp_set_status(req, "500 Internal Server Error");
            break;
        }

        httpd_resp_send(req, outcome->message, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "nodeId", outcome->node_id);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "201 Created");

    char *json = cJSON_PrintUnformatted(response);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(response);

    return ESP_OK;
}

static esp_err_t nodes_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Commissioning a node");

    cJSON *root = read_json_body(req);

    if (root == NULL)
    {
        return ESP_OK;
    }

    const cJSON *setupCodeJSON = cJSON_GetObjectItemCaseSensitive(root, "setupCode");

    if (!cJSON_IsString(setupCodeJSON) || setupCodeJSON->valuestring == NULL)
    {
        ESP_LOGE(TAG, "Request is missing a setupCode");
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "setupCode is required", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setup Code: %s", setupCodeJSON->valuestring);

    commission_outcome_t outcome;
    commission_node(setupCodeJSON->valuestring, &outcome);

    cJSON_Delete(root);

    return send_commission_outcome(req, &outcome);
}

// The node list GET /api/nodes serves. This one feeds our own web UI and is free to carry
// whatever the UI needs; the companion app's array is built separately in companion_api.cpp
// against its published contract. Do not merge the two.
static cJSON *build_nodes_json(void)
{
    ESP_LOGI(TAG, "There are %u node(s)", g_node_manager.node_count);

    cJSON *root = cJSON_CreateArray();

    matter_node_t *node = g_node_manager.node_list;

    while (node)
    {
        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "nodeId", node->node_id);
        cJSON_AddBoolToObject(jNode, "isIcd", node->is_icd);

        if (node->vendor_name)
        {
            cJSON_AddStringToObject(jNode, "vendorName", node->vendor_name);
        }
        else
        {
            cJSON_AddItemToObject(jNode, "vendorName", cJSON_CreateNull());
        }

        if (node->product_name)
        {
            cJSON_AddStringToObject(jNode, "productName", node->product_name);
        }
        else
        {
            cJSON_AddItemToObject(jNode, "productName", cJSON_CreateNull());
        }

        if (node->name)
        {
            cJSON_AddStringToObject(jNode, "nodeName", node->name);
        }
        else
        {
            cJSON_AddItemToObject(jNode, "nodeName", cJSON_CreateNull());
        }

        cJSON_AddNumberToObject(jNode, "powerSource", node->power_source);

        if (node->has_battery_percent)
        {
            cJSON_AddNumberToObject(jNode, "batteryPercent", node->battery_percent);
        }
        else
        {
            cJSON_AddItemToObject(jNode, "batteryPercent", cJSON_CreateNull());
        }

        if (node->has_battery_voltage)
        {
            cJSON_AddNumberToObject(jNode, "batteryVoltage", node->battery_voltage_mv);
        }
        else
        {
            cJSON_AddItemToObject(jNode, "batteryVoltage", cJSON_CreateNull());
        }

        cJSON_AddNumberToObject(jNode, "extAddress", node->ext_address);

        cJSON_AddBoolToObject(jNode, "hasSubscription", node->has_subscription);

        // Transient, so null means "nothing heard since this controller booted" rather than "never
        // heard from". The UI has to say which, so it cannot be flattened to a 0.
        //
        if (node->last_seen != 0)
        {
            cJSON_AddNumberToObject(jNode, "lastSeen", node->last_seen);
        }
        else
        {
            cJSON_AddItemToObject(jNode, "lastSeen", cJSON_CreateNull());
        }

        cJSON *endpoint_array = cJSON_CreateArray();

        for (uint16_t j = 0; j < node->endpoints_count; j++)
        {
            cJSON *endpointJSON = cJSON_CreateObject();

            endpoint_entry_t endpoint = node->endpoints[j];

            cJSON_AddNumberToObject(endpointJSON, "endpointId", endpoint.endpoint_id);

            if (endpoint.name)
            {
                cJSON_AddStringToObject(endpointJSON, "endpointName", endpoint.name);
            }
            else
            {
                cJSON_AddItemToObject(endpointJSON, "endpointName", cJSON_CreateNull());
            }

            cJSON_AddNumberToObject(endpointJSON, "powerSource", endpoint.power_source);

            if (endpoint.has_battery_percent)
            {
                cJSON_AddNumberToObject(endpointJSON, "batteryPercent", endpoint.battery_percent);
            }
            else
            {
                cJSON_AddItemToObject(endpointJSON, "batteryPercent", cJSON_CreateNull());
            }

            if (endpoint.has_battery_voltage)
            {
                cJSON_AddNumberToObject(endpointJSON, "batteryVoltage", endpoint.battery_voltage_mv);
            }
            else
            {
                cJSON_AddItemToObject(endpointJSON, "batteryVoltage", cJSON_CreateNull());
            }

            add_number_or_null(endpointJSON, "measuredValue", endpoint.has_measured_value, endpoint.measured_value);

            cJSON *device_type_array = cJSON_CreateArray();

            for (uint16_t k = 0; k < endpoint.device_type_count; k++)
            {
                uint32_t device_type_id = endpoint.device_type_ids[k];

                cJSON *number = cJSON_CreateNumber((double)device_type_id);
                cJSON_AddItemToArray(device_type_array, number);
            }

            cJSON_AddItemToObject(endpointJSON, "deviceTypes", device_type_array);
            cJSON_AddItemToArray(endpoint_array, endpointJSON);
        }

        cJSON_AddItemToObject(jNode, "endpoints", endpoint_array);
        cJSON_AddItemToArray(root, jNode);

        node = node->next;
    }

    return root;
}

static esp_err_t nodes_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all nodes ...");

    cJSON *root = build_nodes_json();

    // TODO Add caching!!
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t node_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting node ...");

    char templatePath[] = "/api/nodes/:nodeId";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t node_id = 0;

    if (bindings.hasBinding("nodeId"))
    {
        node_id = strtoull(bindings.get("nodeId"), NULL, 10);
    }

    matter_node_t *node = find_node(&g_node_manager, node_id);

    cJSON *root = cJSON_CreateObject();

    ESP_LOGI(TAG, "Node ID: %llu", node->node_id);

    cJSON_AddNumberToObject(root, "nodeId", node->node_id);

    if (node->name)
    {
        cJSON_AddStringToObject(root, "name", node->name);
    }
    else
    {
        cJSON_AddItemToObject(root, "name", cJSON_CreateNull());
    }

    if (node->label)
    {
        cJSON_AddStringToObject(root, "label", node->label);
    }
    else
    {
        cJSON_AddItemToObject(root, "label", cJSON_CreateNull());
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 Accepted");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

// Shared by DELETE /api/nodes/:nodeId and DELETE /api/companion/nodes/:nodeId -- see node_api.h.
esp_err_t unpair_node(uint64_t node_id)
{
    ESP_LOGI(TAG, "Unpairing node %llu...", node_id);

    esp_matter::controller::pairing_command_callbacks_t callbacks = {
        .unpair_complete_callback = on_unpair_complete_callback};

    esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::controller::unpair_device(node_id);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unpairing failed");
        return err;
    }

    remove_node(&g_node_manager, node_id);
    heat_pump_forget_node(&g_heat_pump_manager, node_id);

    return ESP_OK;
}

// Shared by PUT /api/nodes/:nodeId/update and PUT /api/companion/nodes/:nodeId/update -- see
// node_api.h.
esp_err_t rename_node(uint64_t node_id, const char *name)
{
    matter_node_t *node = find_node(&g_node_manager, node_id);

    if (node == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    set_node_name(node, name);

    return save_nodes_to_nvs(&g_node_manager);
}

static esp_err_t node_delete_handler(httpd_req_t *req)
{
    char templatePath[] = "/api/nodes/:nodeId";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t node_id = 0;

    if (bindings.hasBinding("nodeId"))
    {
        node_id = strtoull(bindings.get("nodeId"), NULL, 10);
    }

    if (unpair_node(node_id) != ESP_OK)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Could not remove the device", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // An empty body still has to be sent: httpd never answers a request the handler didn't
    // respond to, so returning here without this left the client waiting for its own timeout.
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t node_put_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);

    esp_err_t err = ESP_OK;

    char templatePath[] = "/api/nodes/:nodeId/:action";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t node_id = 0;

    if (bindings.hasBinding("nodeId"))
    {
        node_id = strtoull(bindings.get("nodeId"), NULL, 10);
    }

    ESP_LOGI(TAG, "PUT node %llu", node_id);

    matter_node_t *node = find_node(&g_node_manager, node_id);

    if (node == NULL)
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Failed", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // get() is what reports a missing segment, not hasBinding(), which only inspects the pattern
    // and so answers yes for a URI that stopped at the node id.
    const char *action = bindings.get("action");

    if (action != NULL)
    {
        ESP_LOGI(TAG, "Action: %s", action);

        // Select based on the action
        //
        if (strcmp("identify", action) == 0)
        {
            ESP_LOGI(TAG, "Sending identify command");

            // TODO Schedule this!
            //
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            esp_err_t err = heating_monitor::controller::identify_command::get_instance().send_identify_command(node_id);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        else if (strcmp("interview", action) == 0)
        {
            ESP_LOGI(TAG, "Sending interview command");

            clear_node_details(&g_node_manager, node_id);

            uint16_t endpointId = 0x0000; // Root

            // We want to read a few attributes from the Basic Information cluster.
            //
            ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
            attr_paths.Alloc(5);

            if (!attr_paths.Get())
            {
                ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
                return ESP_FAIL;
            }

            attr_paths[0] = AttributePathParams(endpointId, BasicInformation::Id, BasicInformation::Attributes::VendorName::Id);
            attr_paths[1] = AttributePathParams(endpointId, BasicInformation::Id, BasicInformation::Attributes::ProductName::Id);
            attr_paths[2] = AttributePathParams(endpointId, BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id);
            attr_paths[3] = AttributePathParams(endpointId, Descriptor::Id, Descriptor::Attributes::PartsList::Id);
            attr_paths[4] = AttributePathParams(endpointId, ThreadNetworkDiagnostics::Id, ThreadNetworkDiagnostics::Attributes::ExtAddress::Id);

            ScopedMemoryBufferWithSize<EventPathParams> event_paths;
            event_paths.Alloc(0);

            esp_matter::controller::read_command *read_attr_command = chip::Platform::New<read_command>(node_id,
                                                                                                        std::move(attr_paths),
                                                                                                        std::move(event_paths),
                                                                                                        attribute_data_cb,
                                                                                                        attribute_data_read_done,
                                                                                                        nullptr);

            chip::DeviceLayer::PlatformMgr().LockChipStack();
            err = read_attr_command->send_command();
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        else if (strcmp("update", action) == 0)
        {
            cJSON *root = read_json_body(req);

            if (root == NULL)
            {
                return ESP_OK;
            }

            const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");

            if (!cJSON_IsString(nameJSON) || nameJSON->valuestring == NULL)
            {
                cJSON_Delete(root);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "name is required", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }

            err = rename_node(node_id, nameJSON->valuestring);

            cJSON_Delete(root);
        }
        else if (strcmp("subscribe", action) == 0)
        {
            ESP_LOGI(TAG, "Manually queueing subscription for node 0x%016llX", node_id);

            enqueue_subscription(node_id);
        }
        else if (strcmp("commissioning-window", action) == 0)
        {
            // Opens the device to a second controller (multi-admin). Like POST /api/nodes, this
            // parks the httpd task until the device answers, so the UI gets the setup code or a
            // real error rather than a code for a window that never opened.
            heating_monitor::controller::commissioning_window_result_t result;
            CHIP_ERROR chip_err = CHIP_NO_ERROR;

            esp_err_t window_err = heating_monitor::controller::open_commissioning_window(node_id, &result, &chip_err);

            if (window_err != ESP_OK)
            {
                char message[128];

                if (window_err == ESP_ERR_INVALID_STATE)
                {
                    httpd_resp_set_status(req, "409 Conflict");
                    snprintf(message, sizeof(message), "A commissioning window request is already in progress");
                }
                else if (window_err == ESP_ERR_TIMEOUT)
                {
                    httpd_resp_set_status(req, "504 Gateway Timeout");
                    snprintf(message, sizeof(message), "The device didn't respond in time");
                }
                else if (chip_err != CHIP_NO_ERROR)
                {
                    httpd_resp_set_status(req, "502 Bad Gateway");
                    snprintf(message, sizeof(message), "Failed to open commissioning window: %s", ErrorStr(chip_err));
                }
                else
                {
                    httpd_resp_set_status(req, "500 Internal Server Error");
                    snprintf(message, sizeof(message), "Failed to open commissioning window: %s", esp_err_to_name(window_err));
                }

                httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }

            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "manualCode", result.manual_code);
            cJSON_AddStringToObject(response, "qrCode", result.qr_code);
            cJSON_AddNumberToObject(response, "timeout", result.timeout_s);

            char *json = cJSON_PrintUnformatted(response);

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, json);

            cJSON_free(json);
            cJSON_Delete(response);

            return ESP_OK;
        }

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Action failed");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Failed", HTTPD_RESP_USE_STRLEN);
        }
        else
        {
            httpd_resp_set_status(req, "200 OK");
            httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);
        }
    }

    return ESP_OK;
}

static esp_err_t radiators_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Adding a radiator");

    char content[req->content_len];

    esp_err_t err = httpd_req_recv(req, content, req->content_len);

    if (err <= 0)
    {
        ESP_LOGE(TAG, "Failed to receive data %d", err);
        httpd_resp_set_status(req, "500");
        httpd_resp_send(req, "INTERNAL SERVER ERROR", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Successfully parsed JSON");

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *mqttNameJSON = cJSON_GetObjectItemCaseSensitive(root, "mqttName");
    const cJSON *typeJSON = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *outputJSON = cJSON_GetObjectItemCaseSensitive(root, "output");
    const cJSON *flowSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowSensorNodeId");
    const cJSON *flowSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowSensorEndpointId");
    const cJSON *returnSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnSensorNodeId");
    const cJSON *returnSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnSensorEndpointId");

    radiator_t *new_radiator = add_radiator(&g_radiator_manager,
                                            nameJSON->valuestring,
                                            mqttNameJSON->valuestring,
                                            (uint8_t)typeJSON->valueint,
                                            (uint16_t)outputJSON->valueint,
                                            (uint64_t)flowSensorNodeIdJSON->valueint,
                                            (uint16_t)flowSensorEndpointIdJSON->valueint,
                                            (uint64_t)returnSensorNodeIdJSON->valueint,
                                            (uint16_t)returnSensorEndpointIdJSON->valueint);

    save_radiators_to_nvs(&g_radiator_manager);

    new_radiator->has_flow_temperature = get_endpoint_measured_value(&g_node_manager, flowSensorNodeIdJSON->valueint, flowSensorEndpointIdJSON->valueint, &new_radiator->flow_temperature);
    new_radiator->has_return_temperature = get_endpoint_measured_value(&g_node_manager, returnSensorNodeIdJSON->valueint, returnSensorEndpointIdJSON->valueint, &new_radiator->return_temperature);

    broadcast_home_state();

    ESP_LOGI(TAG, "Radiator saved");

    if (is_mqtt_connected)
    {
        ha_discovery_announce_radiator(_mqtt_client, new_radiator);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "radiatorId", new_radiator->radiator_id);
    char *response_payload = cJSON_PrintUnformatted(response);

    // Return success!
    //
    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, response_payload, HTTPD_RESP_USE_STRLEN);

    cJSON_free(response_payload);
    cJSON_Delete(response);

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t radiators_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all radiators ...");

    cJSON *root = cJSON_CreateArray();

    radiator_t *radiator = g_radiator_manager.radiator_list;

    while (radiator)
    {
        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "radiatorId", radiator->radiator_id);
        cJSON_AddStringToObject(jNode, "name", radiator->name);
        cJSON_AddNumberToObject(jNode, "type", radiator->type);
        cJSON_AddNumberToObject(jNode, "output", radiator->output_dt_50);

        add_number_or_null(jNode, "flowTemp", radiator->has_flow_temperature, radiator->flow_temperature);
        add_number_or_null(jNode, "returnTemp", radiator->has_return_temperature, radiator->return_temperature);
        add_number_or_null(jNode, "meanWaterTemperature", radiator->has_mean_water_temperature, radiator->mean_water_temperature);
        add_number_or_null(jNode, "currentOutput", radiator->has_heat_output, radiator->heat_output);

        cJSON_AddItemToArray(root, jNode);

        radiator = radiator->next;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t radiator_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting radiator ...");

    char templatePath[] = "/api/radiators/:radiatorId";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t radiatorId = 0;

    if (bindings.hasBinding("radiatorId"))
    {
        radiatorId = strtoull(bindings.get("radiatorId"), NULL, 10);
    }

    radiator_t *radiator = find_radiator(&g_radiator_manager, radiatorId);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "radiatorId", radiator->radiator_id);
    cJSON_AddStringToObject(root, "name", radiator->name);
    cJSON_AddStringToObject(root, "mqttName", radiator->mqtt_name);
    cJSON_AddNumberToObject(root, "type", radiator->type);
    cJSON_AddNumberToObject(root, "output", radiator->output_dt_50);

    cJSON_AddNumberToObject(root, "flowSensorNodeId", radiator->flow_temp_node_id);
    cJSON_AddNumberToObject(root, "flowSensorEndpointId", radiator->flow_temp_endpoint_id);
    cJSON_AddNumberToObject(root, "returnSensorNodeId", radiator->return_temp_node_id);
    cJSON_AddNumberToObject(root, "returnSensorEndpointId", radiator->return_temp_endpoint_id);

    add_number_or_null(root, "flowTemp", radiator->has_flow_temperature, radiator->flow_temperature);
    add_number_or_null(root, "returnTemp", radiator->has_return_temperature, radiator->return_temperature);
    add_number_or_null(root, "meanWaterTemperature", radiator->has_mean_water_temperature, radiator->mean_water_temperature);
    add_number_or_null(root, "currentOutput", radiator->has_heat_output, radiator->heat_output);

    // The room this radiator is in. radiator_t::room_id is never assigned, so room->radiators[]
    // is the only real link -- the same walk update_radiator_outputs() does. Null when the
    // radiator has not been put in a room.
    room_t *owning_room = NULL;

    for (room_t *room = g_room_manager.room_list; room != NULL && owning_room == NULL; room = room->next)
    {
        for (uint8_t r = 0; r < room->radiator_count; r++)
        {
            if (room->radiators[r] == radiator->radiator_id)
            {
                owning_room = room;
                break;
            }
        }
    }

    if (owning_room)
    {
        cJSON_AddNumberToObject(root, "roomId", owning_room->room_id);
        cJSON_AddStringToObject(root, "roomName", owning_room->name);
        add_number_or_null(root, "roomTemperature", owning_room->has_current_temperature, owning_room->current_temperature);
    }
    else
    {
        cJSON_AddNullToObject(root, "roomId");
        cJSON_AddNullToObject(root, "roomName");
        cJSON_AddNullToObject(root, "roomTemperature");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 Accepted");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t radiators_put_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Updating a radiator...");

    char templatePath[] = "/api/radiators/:radiatorId";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint8_t radiator_id = 0;

    if (bindings.hasBinding("radiatorId"))
    {
        radiator_id = strtoull(bindings.get("radiatorId"), NULL, 10);
    }

    char content[req->content_len];
    esp_err_t err = httpd_req_recv(req, content, req->content_len);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *mqttNameJSON = cJSON_GetObjectItemCaseSensitive(root, "mqttName");
    const cJSON *typeJSON = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *outputJSON = cJSON_GetObjectItemCaseSensitive(root, "output");
    const cJSON *flowSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowSensorNodeId");
    const cJSON *flowSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowSensorEndpointId");
    const cJSON *returnSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnSensorNodeId");
    const cJSON *returnSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnSensorEndpointId");

    // The HA device is keyed on the MQTT name, so a rename has to remove the one under the old
    // name. Copied now, before update_radiator() frees it.
    std::string previous_mqtt_name;
    if (radiator_t *existing = find_radiator(&g_radiator_manager, radiator_id))
    {
        previous_mqtt_name = existing->mqtt_name ? existing->mqtt_name : "";
    }

    radiator_t *updated_radiator = update_radiator(&g_radiator_manager,
                                                   radiator_id,
                                                   nameJSON->valuestring,
                                                   mqttNameJSON->valuestring,
                                                   (uint8_t)typeJSON->valueint,
                                                   (uint16_t)outputJSON->valueint,
                                                   (uint64_t)flowSensorNodeIdJSON->valueint,
                                                   (uint16_t)flowSensorEndpointIdJSON->valueint,
                                                   (uint64_t)returnSensorNodeIdJSON->valueint,
                                                   (uint16_t)returnSensorEndpointIdJSON->valueint);

    save_radiators_to_nvs(&g_radiator_manager);

    updated_radiator->has_flow_temperature = get_endpoint_measured_value(&g_node_manager, flowSensorNodeIdJSON->valueint, flowSensorEndpointIdJSON->valueint, &updated_radiator->flow_temperature);
    updated_radiator->has_return_temperature = get_endpoint_measured_value(&g_node_manager, returnSensorNodeIdJSON->valueint, returnSensorEndpointIdJSON->valueint, &updated_radiator->return_temperature);

    if (is_mqtt_connected && updated_radiator)
    {
        if (previous_mqtt_name != updated_radiator->mqtt_name)
        {
            ha_discovery_remove_radiator(_mqtt_client, radiator_id, previous_mqtt_name.c_str());
        }

        ha_discovery_announce_radiator(_mqtt_client, updated_radiator);
    }

    httpd_resp_set_status(req, "201 NoContent");
    httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t radiators_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Deleting radiator...");

    size_t size = strlen(req->uri);

    char *pch = strrchr(req->uri, '/');
    int index_of_last_slash = pch - req->uri + 1;

    int length_of_nodeId = size - index_of_last_slash;

    char id_str[length_of_nodeId + 1];

    memcpy(id_str, &req->uri[index_of_last_slash], length_of_nodeId);

    id_str[length_of_nodeId] = '\0';
    ESP_LOGI(TAG, "Deleting radiator %s", id_str);

    uint8_t id = (uint8_t)strtoul(id_str, NULL, 10);

    char mqtt_name[50];
    esp_err_t err = remove_radiator(&g_radiator_manager, id, &mqtt_name[0]);

    if (err == ESP_OK)
    {
        if (is_mqtt_connected)
        {
            ha_discovery_remove_radiator(_mqtt_client, id, mqtt_name);
        }

        broadcast_home_state();

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Failed to remove radiator", HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

static esp_err_t rooms_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Adding a room...");

    char content[req->content_len + 1];

    int received = httpd_req_recv(req, content, req->content_len);

    if (received <= 0)
    {
        ESP_LOGE(TAG, "Failed to receive data %d", received);
        httpd_resp_set_status(req, "500");
        httpd_resp_send(req, "INTERNAL SERVER ERROR", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    content[received] = '\0';

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *mqttNameJSON = cJSON_GetObjectItemCaseSensitive(root, "mqttName");
    const cJSON *targetTemperatureJSON = cJSON_GetObjectItemCaseSensitive(root, "targetTemperature");
    const cJSON *heatLossPerDegreeJSON = cJSON_GetObjectItemCaseSensitive(root, "heatLossPerDegree");
    const cJSON *temperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorNodeId");
    const cJSON *temperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorEndpointId");
    const cJSON *radiatorIdsJSON = cJSON_GetObjectItemCaseSensitive(root, "radiatorIds");

    if (!cJSON_IsString(nameJSON) ||
        !cJSON_IsNumber(targetTemperatureJSON) ||
        !cJSON_IsNumber(temperatureSensorNodeIdJSON) ||
        !cJSON_IsNumber(temperatureSensorEndpointIdJSON))
    {
        ESP_LOGE(TAG, "Room JSON is missing a required field");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing required field", HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    // Both of these are optional. The MQTT name is only used to build the room's
    // state topic and the heat loss is normally filled in later, via a room update.
    //
    char *mqtt_name = cJSON_IsString(mqttNameJSON) ? mqttNameJSON->valuestring : (char *)"";
    uint8_t heat_loss_per_degree = cJSON_IsNumber(heatLossPerDegreeJSON) ? (uint8_t)heatLossPerDegreeJSON->valueint : 0;

    room_t *new_room = add_room(&g_room_manager,
                                nameJSON->valuestring,
                                mqtt_name,
                                (uint16_t)targetTemperatureJSON->valueint,
                                heat_loss_per_degree,
                                (uint64_t)temperatureSensorNodeIdJSON->valueint,
                                (uint16_t)temperatureSensorEndpointIdJSON->valueint);

    if (new_room == NULL)
    {
        ESP_LOGE(TAG, "Failed to add the room");
        httpd_resp_set_status(req, "500");
        httpd_resp_send(req, "INTERNAL SERVER ERROR", HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // The radiators are created before the room, so the caller can hand us the
    // IDs it got back from /api/radiators and have them attached right away.
    //
    if (cJSON_IsArray(radiatorIdsJSON))
    {
        uint8_t radiator_count = cJSON_GetArraySize(radiatorIdsJSON);
        uint8_t *radiator_ids = (uint8_t *)calloc(radiator_count, sizeof(uint8_t));

        cJSON *iterator = NULL;

        uint8_t i = 0;

        cJSON_ArrayForEach(iterator, radiatorIdsJSON)
        {
            ESP_LOGI(TAG, "Add radiatorId: %u to room %u", (uint8_t)iterator->valueint, new_room->room_id);
            radiator_ids[i++] = (uint8_t)iterator->valueint;
        }

        set_room_radiators(new_room, radiator_count, radiator_ids);

        free(radiator_ids);
    }

    save_rooms_to_nvs(&g_room_manager);

    // Before update_room_heat_loss(), so the device exists by the time its state is published.
    if (is_mqtt_connected)
    {
        ha_discovery_announce_room(_mqtt_client, new_room);
    }

    update_room_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client, new_room);

    broadcast_home_state();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "roomId", new_room->room_id);
    char *response_payload = cJSON_PrintUnformatted(response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, response_payload, HTTPD_RESP_USE_STRLEN);

    cJSON_free(response_payload);
    cJSON_Delete(response);

    cJSON_Delete(root);

    return ESP_OK;
}

// Orders two rooms by name for the listing below: case-insensitively ascending, falling back to
// room_id so rooms sharing a name keep a stable order rather than swapping between requests.
static int compare_rooms_by_name(const void *lhs, const void *rhs)
{
    const room_t *a = *(const room_t *const *)lhs;
    const room_t *b = *(const room_t *const *)rhs;

    int by_name = strcasecmp(a->name ? a->name : "", b->name ? b->name : "");

    if (by_name != 0)
    {
        return by_name;
    }

    return (int)a->room_id - (int)b->room_id;
}

static esp_err_t rooms_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all rooms ...");

    cJSON *root = cJSON_CreateArray();

    // Sorted here rather than by keeping room_list ordered: the list's order is also its NVS
    // order and what the history snapshot walks, and neither wants re-ordering every time a room
    // is renamed. room_count is a uint8_t, so this is at most 255 pointers.
    room_t *rooms[g_room_manager.room_count];
    uint8_t room_count = 0;

    for (room_t *r = g_room_manager.room_list; r != NULL && room_count < g_room_manager.room_count; r = r->next)
    {
        rooms[room_count++] = r;
    }

    qsort(rooms, room_count, sizeof(rooms[0]), compare_rooms_by_name);

    for (uint8_t i = 0; i < room_count; i++)
    {
        room_t *room = rooms[i];

        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "roomId", room->room_id);
        cJSON_AddStringToObject(jNode, "name", room->name);

        cJSON_AddNumberToObject(jNode, "targetTemperature", room->target_temperature);
        add_number_or_null(jNode, "currentTemperature", room->has_current_temperature, room->current_temperature);

        cJSON_AddNumberToObject(jNode, "predictedHeatLoss", room->predicted_heat_loss_per_degree);
        add_number_or_null(jNode, "measuredHeatLoss", room->has_measured_heat_loss, room->measured_heat_loss_per_degree);
        add_number_or_null(jNode, "heatLossDifference", room->has_measured_heat_loss, room->heat_loss_difference);

        uint16_t total_radiator_output = 0;

        // The room's radiators are carried inline so a caller that needs the whole system at once --
        // the Layout schematic -- gets it in one request. room->radiators[] is the only link there
        // is: radiator_t::room_id is never assigned, and GET /api/radiators does not report it. The
        // fields match the ones room_get_handler emits, so both endpoints describe a radiator the
        // same way.
        //
        cJSON *radiators;
        cJSON_AddItemToObject(jNode, "radiators", radiators = cJSON_CreateArray());

        for (uint8_t r = 0; r < room->radiator_count; r++)
        {
            radiator_t *radiator = find_radiator(&g_radiator_manager, room->radiators[r]);

            if (radiator)
            {
                if (radiator->has_heat_output)
                {
                    total_radiator_output += radiator->heat_output;
                }

                cJSON *radiatorNode = cJSON_CreateObject();

                cJSON_AddNumberToObject(radiatorNode, "radiatorId", radiator->radiator_id);
                cJSON_AddStringToObject(radiatorNode, "name", radiator->name);
                cJSON_AddNumberToObject(radiatorNode, "type", radiator->type);
                add_number_or_null(radiatorNode, "flowTemp", radiator->has_flow_temperature, radiator->flow_temperature);
                add_number_or_null(radiatorNode, "returnTemp", radiator->has_return_temperature, radiator->return_temperature);
                add_number_or_null(radiatorNode, "currentOutput", radiator->has_heat_output, radiator->heat_output);

                cJSON_AddItemToArray(radiators, radiatorNode);
            }
        }

        cJSON_AddNumberToObject(jNode, "heatInput", total_radiator_output);

        cJSON_AddItemToArray(root, jNode);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t room_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting room...");

    size_t size = strlen(req->uri);

    char *pch = strrchr(req->uri, '/');
    int index_of_last_slash = pch - req->uri + 1;

    int length_of_nodeId = size - index_of_last_slash;

    char node_id_str[length_of_nodeId + 1];

    memcpy(node_id_str, &req->uri[index_of_last_slash], length_of_nodeId);

    node_id_str[length_of_nodeId] = '\0';

    ESP_LOGI(TAG, "Fetching room %s", node_id_str);

    uint8_t room_id = (uint8_t)strtoul(node_id_str, NULL, 10);

    room_t *room = g_room_manager.room_list;

    while (room)
    {
        if (room->room_id == room_id)
        {
            break;
        }

        room = room->next;
    }

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "roomId", room->room_id);
    cJSON_AddStringToObject(root, "name", room->name);
    cJSON_AddStringToObject(root, "mqttName", room->mqtt_name ? room->mqtt_name : "");

    int16_t current_temperature = 0;
    bool has_current_temperature = get_endpoint_measured_value(&g_node_manager, room->room_temperature_node_id, room->room_temperature_endpoint_id, &current_temperature);
    add_number_or_null(root, "currentTemperature", has_current_temperature, current_temperature);

    cJSON_AddNumberToObject(root, "targetTemperature", room->target_temperature);
    cJSON_AddNumberToObject(root, "temperatureSensorNodeId", room->room_temperature_node_id);
    cJSON_AddNumberToObject(root, "temperatureSensorEndpointId", room->room_temperature_endpoint_id);
    cJSON_AddNumberToObject(root, "heatLossPerDegree", room->predicted_heat_loss_per_degree);

    add_number_or_null(root, "predictedHeatLossAtTargetTemperature", room->has_predicted_heat_loss, room->predicted_heat_loss_at_target_temperature);
    add_number_or_null(root, "measuredHeatLossAtTargetTemperature", room->has_measured_heat_loss, room->measured_heat_loss_at_target_temperature);

    cJSON *radiators;
    cJSON_AddItemToObject(root, "radiators", radiators = cJSON_CreateArray());

    ESP_LOGI(TAG, "There are %u radiators", room->radiator_count);

    for (uint8_t r = 0; r < room->radiator_count; r++)
    {
        uint8_t radiator_id = room->radiators[r];

        radiator_t *radiator = find_radiator(&g_radiator_manager, radiator_id);

        if (radiator)
        {
            cJSON *radiatorNode = cJSON_CreateObject();

            cJSON_AddNumberToObject(radiatorNode, "radiatorId", radiator->radiator_id);
            cJSON_AddStringToObject(radiatorNode, "name", radiator->name);
            cJSON_AddNumberToObject(radiatorNode, "type", radiator->type);
            add_number_or_null(radiatorNode, "flowTemp", radiator->has_flow_temperature, radiator->flow_temperature);
            add_number_or_null(radiatorNode, "returnTemp", radiator->has_return_temperature, radiator->return_temperature);
            add_number_or_null(radiatorNode, "currentOutput", radiator->has_heat_output, radiator->heat_output);

            cJSON_AddItemToArray(radiators, radiatorNode);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t room_put_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Updating a room...");

    size_t size = strlen(req->uri);

    char *pch = strrchr(req->uri, '/');
    int index_of_last_slash = pch - req->uri + 1;

    int length_of_nodeId = size - index_of_last_slash;

    char node_id_str[length_of_nodeId + 1];

    memcpy(node_id_str, &req->uri[index_of_last_slash], length_of_nodeId);

    node_id_str[length_of_nodeId] = '\0';

    uint8_t room_id = (uint8_t)strtoul(node_id_str, NULL, 10);

    char content[req->content_len];
    esp_err_t err = httpd_req_recv(req, content, req->content_len);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *targetTemperatureJSON = cJSON_GetObjectItemCaseSensitive(root, "targetTemperature");
    const cJSON *heatLossPerDegreeJSON = cJSON_GetObjectItemCaseSensitive(root, "heatLossPerDegree");
    const cJSON *radiatorIdsJSON = cJSON_GetObjectItemCaseSensitive(root, "radiatorIds");
    const cJSON *temperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorNodeId");
    const cJSON *temperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorEndpointId");

    uint8_t radiator_count = cJSON_GetArraySize(radiatorIdsJSON);
    uint8_t *radiator_ids = (uint8_t *)calloc(radiator_count, sizeof(u_int8_t));

    cJSON *iterator = NULL;

    uint8_t i = 0;

    cJSON_ArrayForEach(iterator, radiatorIdsJSON)
    {
        ESP_LOGI(TAG, "Add radiatorId: %u to room %u", (uint8_t)iterator->valueint, room_id);
        radiator_ids[i++] = (uint8_t)iterator->valueint;
    }

    // Optional, so an older client that does not send it leaves the MQTT name alone.
    const cJSON *mqttNameJSON = cJSON_GetObjectItemCaseSensitive(root, "mqttName");
    const char *mqtt_name = cJSON_IsString(mqttNameJSON) ? mqttNameJSON->valuestring : NULL;

    // The HA device is keyed on the MQTT name, so a rename has to remove the one under the old
    // name. Copied now, before update_room() frees it.
    std::string previous_mqtt_name;
    if (room_t *existing = find_room(&g_room_manager, room_id))
    {
        previous_mqtt_name = existing->mqtt_name ? existing->mqtt_name : "";
    }

    room_t *updated_room = update_room(&g_room_manager,
                                       room_id, nameJSON->valuestring,
                                       mqtt_name,
                                       (int16_t)targetTemperatureJSON->valueint,
                                       (uint8_t)heatLossPerDegreeJSON->valueint,
                                       radiator_count,
                                       radiator_ids,
                                       (uint64_t)temperatureSensorNodeIdJSON->valueint,
                                       (uint8_t)temperatureSensorEndpointIdJSON->valueint);

    save_rooms_to_nvs(&g_room_manager);

    updated_room->has_current_temperature = get_endpoint_measured_value(&g_node_manager, temperatureSensorNodeIdJSON->valueint, temperatureSensorEndpointIdJSON->valueint, &updated_room->current_temperature);

    // Before update_room_heat_loss(), so the device exists by the time its state is published.
    if (is_mqtt_connected && updated_room)
    {
        if (previous_mqtt_name != updated_room->mqtt_name)
        {
            ha_discovery_remove_room(_mqtt_client, room_id, previous_mqtt_name.c_str());
        }

        ha_discovery_announce_room(_mqtt_client, updated_room);
    }

    update_room_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client, updated_room);

    free(radiator_ids);
    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, "Updated", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t rooms_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Deleting room...");

    size_t size = strlen(req->uri);

    char *pch = strrchr(req->uri, '/');
    int index_of_last_slash = pch - req->uri + 1;

    int length_of_id = size - index_of_last_slash;

    char node_id_str[length_of_id + 1];

    memcpy(node_id_str, &req->uri[index_of_last_slash], length_of_id);
    node_id_str[length_of_id] = '\0';

    uint8_t room_id = (uint8_t)strtoul(node_id_str, NULL, 10);

    // Copied before remove_room() frees it: the HA device is keyed on it.
    std::string mqtt_name;
    if (room_t *existing = find_room(&g_room_manager, room_id))
    {
        mqtt_name = existing->mqtt_name ? existing->mqtt_name : "";
    }

    if (remove_room(&g_room_manager, room_id) == ESP_OK && is_mqtt_connected)
    {
        ha_discovery_remove_room(_mqtt_client, room_id, mqtt_name.c_str());
    }

    broadcast_home_state();

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req)
{
    radiator_manager_reset_and_reload(&g_radiator_manager);
    room_manager_reset_and_reload(&g_room_manager);

    if (is_mqtt_connected)
    {
        ha_discovery_announce_rooms_and_radiators(_mqtt_client, &g_room_manager, &g_radiator_manager);
    }

    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t sensors_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all sensors ...");

    cJSON *root = cJSON_CreateArray();

    matter_node_t *node = g_node_manager.node_list;

    while (node)
    {
        for (uint16_t j = 0; j < node->endpoints_count; j++)
        {
            endpoint_entry_t endpoint = node->endpoints[j];

            for (uint16_t k = 0; k < endpoint.device_type_count; k++)
            {
                cJSON *jSensor = cJSON_CreateObject();
                cJSON_AddNumberToObject(jSensor, "nodeId", node->node_id);
                cJSON_AddNumberToObject(jSensor, "deviceTypeId", endpoint.device_type_ids[k]);

                if (node->name)
                {
                    cJSON_AddStringToObject(jSensor, "nodeName", node->name);
                }
                else
                {
                    cJSON_AddItemToObject(jSensor, "nodeName", cJSON_CreateNull());
                }

                cJSON_AddNumberToObject(jSensor, "endpointId", endpoint.endpoint_id);

                if (endpoint.name)
                {
                    cJSON_AddStringToObject(jSensor, "endpointName", endpoint.name);
                }
                else
                {
                    cJSON_AddItemToObject(jSensor, "endpointName", cJSON_CreateNull());
                }

                cJSON_AddItemToArray(root, jSensor);
            }
        }

        node = node->next;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

// The home snapshot the UI renders. Shared by GET /api/home and the "home" websocket broadcast so
// the two can never drift. The caller owns the returned object, and is responsible for having called
// update_home() first.
//
// The subscription state of one of the home's sensors, for the pills on the Home screen: "none"
// (nothing configured), "missing" (the configured node no longer exists), "subscribed", "pending" or
// "unsubscribed". One string, so the UI never has to combine flags. Subscriptions are per node, so
// the endpoint doesn't come into it.
//
static const char *home_sensor_subscription_state(uint64_t node_id)
{
    if (node_id == 0)
    {
        return "none";
    }

    matter_node_t *node = find_node(&g_node_manager, node_id);

    if (node == NULL)
    {
        return "missing";
    }

    if (node->has_subscription)
    {
        return "subscribed";
    }

    return node->is_subscription_pending ? "pending" : "unsubscribed";
}

static void add_number_or_null(cJSON *object, const char *name, bool has_value, double value)
{
    if (has_value)
    {
        cJSON_AddNumberToObject(object, name, value);
    }
    else
    {
        cJSON_AddNullToObject(object, name);
    }
}

static bool endpoint_has_device_type(const endpoint_entry_t *endpoint, uint32_t device_type_id)
{
    for (uint8_t i = 0; i < endpoint->device_type_count; i++)
    {
        if (endpoint->device_type_ids[i] == device_type_id)
        {
            return true;
        }
    }

    return false;
}

// The Home page's Heat Pump section: the power the heat pump draws, plus whatever sits on the same
// node -- its temperature sensors, and a zone for each heating loop (Thermostat) and hot water
// tank (Water Heater). Null while no heat pump is picked or the node is gone. Everything is in the
// units the clusters report: mW and 0.01 degC, with enums and bitmaps left as numbers for the UI.
//
static void add_heat_pump_json(cJSON *root)
{
    matter_node_t *node = find_node(&g_node_manager, g_home_manager.heat_pump_node_id);

    if (g_home_manager.heat_pump_node_id == 0 || node == NULL)
    {
        cJSON_AddNullToObject(root, "heatPump");
        return;
    }

    cJSON *heat_pump = cJSON_AddObjectToObject(root, "heatPump");

    heat_pump_endpoint_t *self = heat_pump_find_endpoint(&g_heat_pump_manager, node->node_id, g_home_manager.heat_pump_endpoint_id);

    add_number_or_null(heat_pump, "power", self && self->has_active_power, self ? (double)self->active_power_mw : 0);

    cJSON *temperatures = cJSON_AddArrayToObject(heat_pump, "temperatures");
    cJSON *zones = cJSON_AddArrayToObject(heat_pump, "zones");

    for (uint16_t i = 0; i < node->endpoints_count; i++)
    {
        const endpoint_entry_t *endpoint = &node->endpoints[i];

        bool is_water_heater = endpoint_has_device_type(endpoint, DEVICE_TYPE_WATER_HEATER);

        if (endpoint_has_device_type(endpoint, DEVICE_TYPE_TEMPERATURE_SENSOR))
        {
            cJSON *temperature = cJSON_CreateObject();
            cJSON_AddNumberToObject(temperature, "endpointId", endpoint->endpoint_id);

            if (endpoint->name)
            {
                cJSON_AddStringToObject(temperature, "name", endpoint->name);
            }
            else
            {
                cJSON_AddNullToObject(temperature, "name");
            }

            // The reading is dropped with the node's subscription -- see
            // forget_readings_if_unsubscribed().
            add_number_or_null(temperature, "temperature", endpoint->has_measured_value, endpoint->measured_value);

            cJSON_AddItemToArray(temperatures, temperature);
        }
        else if (is_water_heater || endpoint_has_device_type(endpoint, DEVICE_TYPE_THERMOSTAT))
        {
            heat_pump_endpoint_t *state = heat_pump_find_endpoint(&g_heat_pump_manager, node->node_id, endpoint->endpoint_id);

            cJSON *zone = cJSON_CreateObject();
            cJSON_AddNumberToObject(zone, "endpointId", endpoint->endpoint_id);

            if (endpoint->name)
            {
                cJSON_AddStringToObject(zone, "name", endpoint->name);
            }
            else
            {
                cJSON_AddNullToObject(zone, "name");
            }

            cJSON_AddBoolToObject(zone, "waterHeater", is_water_heater);

            add_number_or_null(zone, "localTemperature", state && state->has_local_temperature, state ? state->local_temperature : 0);
            add_number_or_null(zone, "heatingSetpoint", state && state->has_heating_setpoint, state ? state->heating_setpoint : 0);
            add_number_or_null(zone, "systemMode", state && state->has_system_mode, state ? state->system_mode : 0);
            add_number_or_null(zone, "runningState", state && state->has_running_state, state ? state->running_state : 0);

            if (is_water_heater)
            {
                add_number_or_null(zone, "heatDemand", state && state->has_heat_demand, state ? state->heat_demand : 0);
                add_number_or_null(zone, "boostState", state && state->has_boost_state, state ? state->boost_state : 0);

                // The number is only any use to the UI as a fallback, for a mode the device reports
                // but does not list in SupportedModes.
                add_number_or_null(zone, "waterHeaterMode", state && state->has_water_heater_mode, state ? state->water_heater_mode : 0);

                const char *mode_label = heat_pump_water_heater_mode_label(state);

                if (mode_label)
                {
                    cJSON_AddStringToObject(zone, "waterHeaterModeLabel", mode_label);
                }
                else
                {
                    cJSON_AddNullToObject(zone, "waterHeaterModeLabel");
                }
            }

            cJSON_AddItemToArray(zones, zone);
        }
    }
}

static cJSON *build_home_json(void)
{
    cJSON *root = cJSON_CreateObject();

    // Null rather than the struct's initial 0 until the sensor has reported over a live
    // subscription -- see forget_outdoor_temperature_if_unsubscribed().
    //
    if (g_home_manager.has_outdoor_temperature)
    {
        cJSON_AddNumberToObject(root, "outdoorTemperature", g_home_manager.outdoor_temperature);
    }
    else
    {
        cJSON_AddNullToObject(root, "outdoorTemperature");
    }

    cJSON_AddNumberToObject(root, "outdoorTemperatureSensorNodeId", g_home_manager.outdoor_temp_node_id);
    cJSON_AddNumberToObject(root, "outdoorTemperatureSensorEndpointId", g_home_manager.outdoor_temp_endpoint_id);
    cJSON_AddStringToObject(root, "outdoorTemperatureSensorSubscription", home_sensor_subscription_state(g_home_manager.outdoor_temp_node_id));

    cJSON_AddNumberToObject(root, "electricalMeterNodeId", g_home_manager.electrical_meter_node_id);
    cJSON_AddNumberToObject(root, "electricalMeterEndpointId", g_home_manager.electrical_meter_endpoint_id);
    cJSON_AddStringToObject(root, "electricalMeterSubscription", home_sensor_subscription_state(g_home_manager.electrical_meter_node_id));

    cJSON_AddNumberToObject(root, "heatMeterNodeId", g_home_manager.heat_meter_node_id);
    cJSON_AddNumberToObject(root, "heatMeterEndpointId", g_home_manager.heat_meter_endpoint_id);
    cJSON_AddStringToObject(root, "heatMeterSubscription", home_sensor_subscription_state(g_home_manager.heat_meter_node_id));

    cJSON_AddNumberToObject(root, "heatPumpNodeId", g_home_manager.heat_pump_node_id);
    cJSON_AddNumberToObject(root, "heatPumpEndpointId", g_home_manager.heat_pump_endpoint_id);
    cJSON_AddStringToObject(root, "heatPumpSubscription", home_sensor_subscription_state(g_home_manager.heat_pump_node_id));
    add_heat_pump_json(root);
    
    cJSON_AddNumberToObject(root, "loggingIntervalSeconds", history_logger_interval());
    cJSON_AddNumberToObject(root, "loggingIntervalActiveSeconds", history_logger_active_interval());

    // Nulls where the meter hasn't reported, so the UI can show a dash rather than a plausible zero.
    //
    if (g_home_manager.has_electrical_voltage)
    {
        cJSON_AddNumberToObject(root, "electricalVoltage", g_home_manager.electrical_voltage_mv);
    }
    else
    {
        cJSON_AddNullToObject(root, "electricalVoltage");
    }

    if (g_home_manager.has_electrical_current)
    {
        cJSON_AddNumberToObject(root, "electricalCurrent", g_home_manager.electrical_current_ma);
    }
    else
    {
        cJSON_AddNullToObject(root, "electricalCurrent");
    }

    if (g_home_manager.has_electrical_power)
    {
        cJSON_AddNumberToObject(root, "electricalPower", g_home_manager.electrical_power_mw);
    }
    else
    {
        cJSON_AddNullToObject(root, "electricalPower");
    }

    // Sent in l/h, the unit the Heat Meter cluster reports. The standard FlowMeasurement view of
    // the same reading (heat_source_flow_rate) is a uint16 of 0.1 m^3/h -- 100 l/h per count -- so
    // the meter's own figure is the more precise of the two.
    //
    if (g_home_manager.has_heat_meter_flow)
    {
        cJSON_AddNumberToObject(root, "heatMeterFlowRate", g_home_manager.heat_meter_flow);
    }
    else
    {
        cJSON_AddNullToObject(root, "heatMeterFlowRate");
    }

    // The rest go out as the cluster reports them, the same units the UI's formatters take for the
    // standard clusters: temperatures in 0.01 degC (Temperature) and power in mW (ElectricalPower).
    //
    if (g_home_manager.has_heat_meter_flow_temperature)
    {
        cJSON_AddNumberToObject(root, "heatMeterFlowTemperature", g_home_manager.heat_meter_flow_temperature);
    }
    else
    {
        cJSON_AddNullToObject(root, "heatMeterFlowTemperature");
    }

    if (g_home_manager.has_heat_meter_return_temperature)
    {
        cJSON_AddNumberToObject(root, "heatMeterReturnTemperature", g_home_manager.heat_meter_return_temperature);
    }
    else
    {
        cJSON_AddNullToObject(root, "heatMeterReturnTemperature");
    }

    if (g_home_manager.has_heat_meter_power)
    {
        cJSON_AddNumberToObject(root, "heatMeterPower", g_home_manager.heat_meter_power_mw);
    }
    else
    {
        cJSON_AddNullToObject(root, "heatMeterPower");
    }

    // Derived by update_home(). Absent rather than zero while the heat source is off or the
    // meters have not both reported, so the UI dashes instead of claiming a COP of 0.00.
    //
    if (g_home_manager.has_cop)
    {
        cJSON_AddNumberToObject(root, "cop", g_home_manager.cop_x100);
    }
    else
    {
        cJSON_AddNullToObject(root, "cop");
    }

    // The average of the rooms' temperatures, derived by update_home(); null while no room
    // has a reading. dhwRunning comes from the picked heat pump -- see
    // update_heat_pump_running() -- and is null while none is picked or it has not reported.
    //
    if (g_home_manager.has_average_internal_temperature)
    {
        cJSON_AddNumberToObject(root, "averageInternalTemperature", g_home_manager.average_internal_temperature);
    }
    else
    {
        cJSON_AddNullToObject(root, "averageInternalTemperature");
    }

    if (g_home_manager.has_dhw_running)
    {
        cJSON_AddBoolToObject(root, "dhwRunning", g_home_manager.dhw_running);
    }
    else
    {
        cJSON_AddNullToObject(root, "dhwRunning");
    }

    cJSON_AddNumberToObject(root, "totalPredictedHeatLoss", g_home_manager.total_predicted_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "totalMeasuredHeatLoss", g_home_manager.total_measured_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "predictedHeatLossAtCurrentTemperature", g_home_manager.total_predicted_heat_loss_at_current_temperature);
    cJSON_AddNumberToObject(root, "measuredHeatLossAtCurrentTemperature", g_home_manager.total_measured_heat_loss_at_current_temperature);
    cJSON_AddNumberToObject(root, "radiatorCount", g_home_manager.radiator_count);
    cJSON_AddNumberToObject(root, "totalRadiatorOutput", g_home_manager.total_radiator_output);

    return root;
}

// Recalculates the home totals and pushes the same snapshot GET /api/home returns, so an open Home
// tab tracks the readings instead of showing whatever was true when it was loaded.
//
static void broadcast_home_state(void)
{
    if (server == NULL)
    {
        return;
    }

    update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

    cJSON *root = build_home_json();
    cJSON_AddStringToObject(root, "channel", "home");

    ws_broadcast_json(root);

    cJSON_Delete(root);
}

// The Home screen shows the subscription state of the home's own sensors, so a change to one of them
// is pushed. Other nodes (radiator sensors) are filtered out, because broadcast_home_state also
// recalculates the home and publishes it to MQTT.
//
// True while the node has a live subscription. A reading from a node without one is the last value
// it happened to send, which could be hours old.
//
static bool node_is_subscribed(uint64_t node_id)
{
    if (node_id == 0)
    {
        return false;
    }

    matter_node_t *node = find_node(&g_node_manager, node_id);

    return node != NULL && node->has_subscription;
}

// Called when a node's subscription ends or fails to establish. If that node is the outdoor
// sensor, its reading stops being reported: the UI shows a dash, MQTT publishes null and the
// history records the sentinel, rather than all three carrying the last value forward as if it
// were current. The priming report of the next subscription sets it again.
//
static void forget_outdoor_temperature_if_unsubscribed(uint64_t node_id)
{
    if (node_id == 0 || node_id != g_home_manager.outdoor_temp_node_id || node_is_subscribed(node_id))
    {
        return;
    }

    if (g_home_manager.has_outdoor_temperature)
    {
        ESP_LOGI(TAG, "Outdoor temperature sensor lost its subscription; reading is no longer current");

        g_home_manager.has_outdoor_temperature = false;

        // Republishes the home state to MQTT without the stale reading.
        update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);
    }
}

// The same for a heat pump: its readings are dropped with its subscription, so the Heat Pump section
// dashes rather than showing a mode or setpoint from before the node went quiet. Its temperature
// sensors are covered by add_heat_pump_json(), which checks the subscription itself.
//
static void forget_heat_pump_if_unsubscribed(uint64_t node_id)
{
    if (node_is_subscribed(node_id))
    {
        return;
    }

    heat_pump_forget_node(&g_heat_pump_manager, node_id);
    update_heat_pump_running();
}

// Works out whether the picked heat pump is space heating or producing hot water, for the history
// record and the bar the charts draw from it. Both come from the Heat bit of
// ThermostatRunningState: on a Water Heater endpoint it means hot water, on any other Thermostat
// endpoint it means heating. Several endpoints of one kind are ORed, so two heating loops read as
// heating while either runs.
//
// A state is absent until an endpoint of its kind has reported, and again once the node's readings
// are dropped with its subscription, so history records the sentinel rather than a stale mode.
//
static void update_heat_pump_running(void)
{
    bool has_heating = false;
    bool heating = false;
    bool has_dhw = false;
    bool dhw = false;

    matter_node_t *node = find_node(&g_node_manager, g_home_manager.heat_pump_node_id);

    if (g_home_manager.heat_pump_node_id != 0 && node != NULL)
    {
        for (uint16_t i = 0; i < node->endpoints_count; i++)
        {
            const endpoint_entry_t *endpoint = &node->endpoints[i];

            bool is_water_heater = endpoint_has_device_type(endpoint, DEVICE_TYPE_WATER_HEATER);

            if (!is_water_heater && !endpoint_has_device_type(endpoint, DEVICE_TYPE_THERMOSTAT))
            {
                continue;
            }

            heat_pump_endpoint_t *state = heat_pump_find_endpoint(&g_heat_pump_manager, node->node_id, endpoint->endpoint_id);

            if (state == NULL || !state->has_running_state)
            {
                continue;
            }

            // RelayStateBitmap bit 0, Heat.
            bool running = (state->running_state & 0x0001) != 0;

            if (is_water_heater)
            {
                has_dhw = true;
                dhw = dhw || running;
            }
            else
            {
                has_heating = true;
                heating = heating || running;
            }
        }
    }

    bool was_heating = g_home_manager.has_heating_running && g_home_manager.heating_running;

    g_home_manager.dhw_running = dhw;
    g_home_manager.has_dhw_running = has_dhw;
    g_home_manager.heating_running = heating;
    g_home_manager.has_heating_running = has_heating;

    // A radiator's output is zero unless the heat pump is heating, so every one of them has a new
    // figure the moment that starts or stops, without any temperature having moved.
    if ((has_heating && heating) != was_heating)
    {
        for (radiator_t *radiator = g_radiator_manager.radiator_list; radiator != NULL; radiator = radiator->next)
        {
            update_radiator_outputs(&g_node_manager, &g_home_manager, &g_radiator_manager, &g_room_manager, _mqtt_client, radiator);
            broadcast_radiator_state(radiator);
        }
    }
}

// Pushes a radiator's readings and its output to the web UI. The Layout schematic shows watts per
// radiator and only ever sees this channel.
static void broadcast_radiator_state(const radiator_t *radiator)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "channel", "radiator");
    cJSON_AddNumberToObject(root, "radiatorId", radiator->radiator_id);
    add_number_or_null(root, "flowTemp", radiator->has_flow_temperature, radiator->flow_temperature);
    add_number_or_null(root, "returnTemp", radiator->has_return_temperature, radiator->return_temperature);
    add_number_or_null(root, "currentOutput", radiator->has_heat_output, radiator->heat_output);

    ws_broadcast_json(root);

    cJSON_Delete(root);
}

// And for everything else a node supplies. Its endpoints stop having a reading, and so does whatever
// was fed from them: a room's temperature, a radiator's flow and return, the home's heat meter and
// electrical meter. History records the sentinel, MQTT publishes null and the UI dashes, rather
// than all three repeating the last value for as long as the node stays away. The priming report
// of the next subscription sets them again.
//
static void forget_readings_if_unsubscribed(uint64_t node_id)
{
    if (node_id == 0 || node_is_subscribed(node_id))
    {
        return;
    }

    ESP_LOGI(TAG, "Node 0x%016llX lost its subscription; its readings are no longer current", node_id);

    clear_node_measured_values(&g_node_manager, node_id);

    bool changed = false;

    if (node_id == g_home_manager.heat_meter_node_id)
    {
        g_home_manager.has_heat_meter_flow = false;
        g_home_manager.has_heat_meter_flow_temperature = false;
        g_home_manager.has_heat_meter_return_temperature = false;
        g_home_manager.has_heat_meter_power = false;
        changed = true;
    }

    if (node_id == g_home_manager.electrical_meter_node_id)
    {
        g_home_manager.has_electrical_voltage = false;
        g_home_manager.has_electrical_current = false;
        g_home_manager.has_electrical_power = false;
        changed = true;
    }

    for (radiator_t *radiator = g_radiator_manager.radiator_list; radiator != NULL; radiator = radiator->next)
    {
        bool lost_flow = radiator->flow_temp_node_id == node_id;
        bool lost_return = radiator->return_temp_node_id == node_id;

        if (!lost_flow && !lost_return)
        {
            continue;
        }

        if (lost_flow)
        {
            radiator->has_flow_temperature = false;
            radiator->flow_temperature = 0;
        }

        if (lost_return)
        {
            radiator->has_return_temperature = false;
            radiator->return_temperature = 0;
        }

        // Drops the radiator's output with them and republishes its state.
        update_radiator_outputs(&g_node_manager, &g_home_manager, &g_radiator_manager, &g_room_manager, _mqtt_client, radiator);

        broadcast_radiator_state(radiator);

        changed = true;
    }

    for (room_t *room = g_room_manager.room_list; room != NULL; room = room->next)
    {
        if (room->room_temperature_node_id == node_id)
        {
            changed = true;
            break;
        }
    }

    if (!changed)
    {
        return;
    }

    // Every room re-reads its temperature from its endpoint, so the ones this node fed lose theirs,
    // and it ends in update_home(): the history snapshot, COP and the home's MQTT state.
    update_all_rooms_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

    for (room_t *room = g_room_manager.room_list; room != NULL; room = room->next)
    {
        if (room->room_temperature_node_id != node_id)
        {
            continue;
        }

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "channel", "room");
        cJSON_AddNumberToObject(root, "roomId", room->room_id);
        add_number_or_null(root, "temperature", room->has_current_temperature, room->current_temperature);

        ws_broadcast_json(root);

        cJSON_Delete(root);
    }

    broadcast_home_state();
}

static void broadcast_home_if_home_sensor(uint64_t node_id)
{
    if (node_id == 0)
    {
        return;
    }

    if (node_id == g_home_manager.outdoor_temp_node_id ||
        node_id == g_home_manager.heat_meter_node_id ||
        node_id == g_home_manager.heat_pump_node_id ||
        node_id == g_home_manager.electrical_meter_node_id)
    {
        broadcast_home_state();
    }
}

static esp_err_t home_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting home...");

    update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

    cJSON *root = build_home_json();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t home_put_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Updating home...");

    char content[req->content_len];
    esp_err_t err = httpd_req_recv(req, content, req->content_len);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *outdoorTemperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "outdoorTemperatureSensorNodeId");
    const cJSON *outdoorTemperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "outdoorTemperatureSensorEndpointId");
    const cJSON *flowTemperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowTemperatureSensorNodeId");
    const cJSON *flowTemperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowTemperatureSensorEndpointId");
    const cJSON *returnTemperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnTemperatureSensorNodeId");
    const cJSON *returnTemperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnTemperatureSensorEndpointId");
    const cJSON *flowRateSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowRateSensorNodeId");
    const cJSON *flowRateSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowRateSensorEndpointId");
    const cJSON *electricalMeterNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "electricalMeterNodeId");
    const cJSON *electricalMeterEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "electricalMeterEndpointId");
    const cJSON *heatMeterNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "heatMeterNodeId");
    const cJSON *heatMeterEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "heatMeterEndpointId");

    // Every field is read straight into the manager below, so a missing or non-numeric one would be
    // dereferenced as a null cJSON. The web app always sends the full object; anything else is a bug
    // on the client and is worth reporting rather than crashing on.
    //
    const cJSON *requiredFields[] = {
        outdoorTemperatureSensorNodeIdJSON,
        outdoorTemperatureSensorEndpointIdJSON,
        flowTemperatureSensorNodeIdJSON,
        flowTemperatureSensorEndpointIdJSON,
        returnTemperatureSensorNodeIdJSON,
        returnTemperatureSensorEndpointIdJSON,
        flowRateSensorNodeIdJSON,
        flowRateSensorEndpointIdJSON,
        electricalMeterNodeIdJSON,
        electricalMeterEndpointIdJSON,
        heatMeterNodeIdJSON,
        heatMeterEndpointIdJSON,
    };

    for (size_t i = 0; i < sizeof(requiredFields) / sizeof(requiredFields[0]); i++)
    {
        if (!cJSON_IsNumber(requiredFields[i]))
        {
            ESP_LOGE(TAG, "Home update is missing a required numeric field (index %u)", (unsigned)i);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Missing or invalid field", HTTPD_RESP_USE_STRLEN);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
    }

    g_home_manager.outdoor_temp_node_id = (uint64_t)outdoorTemperatureSensorNodeIdJSON->valueint;
    g_home_manager.outdoor_temp_endpoint_id = (uint16_t)outdoorTemperatureSensorEndpointIdJSON->valueint;

    g_home_manager.electrical_meter_node_id = (uint64_t)electricalMeterNodeIdJSON->valueint;
    g_home_manager.electrical_meter_endpoint_id = (uint16_t)electricalMeterEndpointIdJSON->valueint;

    g_home_manager.heat_meter_node_id = (uint64_t)heatMeterNodeIdJSON->valueint;
    g_home_manager.heat_meter_endpoint_id = (uint16_t)heatMeterEndpointIdJSON->valueint;

    // Optional, like the logging interval below: a client that omits it leaves the selection alone.
    //
    const cJSON *heatPumpNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "heatPumpNodeId");
    const cJSON *heatPumpEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "heatPumpEndpointId");

    if (cJSON_IsNumber(heatPumpNodeIdJSON) && cJSON_IsNumber(heatPumpEndpointIdJSON))
    {
        g_home_manager.heat_pump_node_id = (uint64_t)heatPumpNodeIdJSON->valueint;
        g_home_manager.heat_pump_endpoint_id = (uint16_t)heatPumpEndpointIdJSON->valueint;

        update_heat_pump_running();
    }

    // Optional: a client that omits it leaves the logger alone rather than resetting it.
    //
    const cJSON *loggingIntervalJSON = cJSON_GetObjectItemCaseSensitive(root, "loggingIntervalSeconds");

    if (cJSON_IsNumber(loggingIntervalJSON))
    {
        history_logger_set_interval((uint16_t)loggingIntervalJSON->valueint);
    }

    g_home_manager.logging_interval_s = history_logger_interval();

    save_home_to_nvs(&g_home_manager);

    // Copy the outdoor temperature from the sensor to the home manager so that it's available
    // immediately. An unbound sensor (node 0), one that has not reported, or one whose subscription
    // has been lost has no reading to copy, and leaves it absent.
    //
    g_home_manager.has_outdoor_temperature = get_endpoint_measured_value(&g_node_manager, g_home_manager.outdoor_temp_node_id, g_home_manager.outdoor_temp_endpoint_id, &g_home_manager.outdoor_temperature);

    broadcast_home_state();

    httpd_resp_set_status(req, "201 Ok");
    httpd_resp_send(req, "ADDED", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t icd_counter_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Resetting the ICD Counter Offset ...");

    char templatePath[] = "/api/icd_counter/:nodeId";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t node_id = 0;

    if (bindings.hasBinding("nodeId"))
    {
        node_id = strtoull(bindings.get("nodeId"), NULL, 10);
    }

    ESP_LOGI(TAG, "Delete node %llu", node_id);

    auto &icd_client_storage = matter_controller_client::get_instance().get_icd_client_storage();
    auto iter = icd_client_storage.IterateICDClientInfo();
    if (iter == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    app::DefaultICDClientStorage::ICDClientInfoIteratorWrapper wrapper(iter);
    app::ICDClientInfo info;
    while (iter->Next(info)) 
    {
        ESP_LOGI(TAG, "Checking the ICD record for node %llu...", info.peer_node.GetNodeId());

        // Find the matching record
        if(node_id == info.peer_node.GetNodeId())
        {
            ESP_LOGI(TAG, "Found the ICD record for node %llu. Resetting offset...", node_id);
            info.offset = 0;
            icd_client_storage.StoreEntry(info);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "201 OK");
    httpd_resp_send(req, "Deleted", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t write_embedded(httpd_req_t *req, const char *content_type, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t write_index_html(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve root");

    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");

    return write_embedded(req, "text/html", index_html_start, index_html_end);
}

static esp_err_t write_app_js(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve js");

    extern const uint8_t app_js_start[] asm("_binary_app_js_start");
    extern const uint8_t app_js_end[] asm("_binary_app_js_end");

    return write_embedded(req, "application/javascript", app_js_start, app_js_end);
}

static esp_err_t write_app_css(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve css");

    extern const uint8_t app_css_start[] asm("_binary_app_css_start");
    extern const uint8_t app_css_end[] asm("_binary_app_css_end");

    return write_embedded(req, "text/css", app_css_start, app_css_end);
}

static esp_err_t wildcard_get_handler(httpd_req_t *req)
{
    if (strcmp(req->uri, "/app.js") == 0)
    {
        return write_app_js(req);
    }

    if (strcmp(req->uri, "/app.css") == 0)
    {
        return write_app_css(req);
    }

    // An /api path that got this far matched no handler, and serving it index.html makes that
    // impossible to see from the client: a JSON client gets 200 text/html and can only report
    // that it couldn't read the response. Say what actually happened, and echo the URI -- the
    // companion app shows a non-2xx body to the user, so a stray "//api/..." from a base address
    // with a trailing slash names itself.
    if (strncmp(req->uri, "/api/", 5) == 0 || strncmp(req->uri, "//api/", 6) == 0)
    {
        char message[CONFIG_HTTPD_MAX_URI_LEN + 32];
        snprintf(message, sizeof(message), "No such endpoint: %s", req->uri);

        ESP_LOGW(TAG, "Unmatched API request: %s", req->uri);

        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);

        return ESP_OK;
    }

    // Anything else is either "/" or one of the SPA's client-side routes
    // (/rooms, /devices, ...), all of which are served by index.html.
    //
    // Logged because this is where a request with an unexpected path lands, and index.html is a
    // 200: a JSON client that ends up here can only report that it could not read the response,
    // with nothing on the wire to say why. The URI is the whole diagnosis.
    ESP_LOGI(TAG, "Serving the web app for %s", req->uri);

    return write_index_html(req);
}

// Logs who connected, so "the app can't reach us" and "the app is talking to something else"
// stop looking the same from here. A client that never appears in this log never reached us.
static esp_err_t on_socket_opened(httpd_handle_t hd, int sockfd)
{
    struct sockaddr_in6 addr;
    socklen_t addr_len = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0)
    {
        char ip[INET6_ADDRSTRLEN] = {0};

        // Ethernet clients arrive as IPv4-mapped IPv6, so skip the ::ffff: prefix when there is one.
        inet_ntop(AF_INET6, &addr.sin6_addr, ip, sizeof(ip));

        const char *mapped = strncmp(ip, "::ffff:", 7) == 0 ? ip + 7 : ip;

        ESP_LOGI(TAG, "HTTP client connected from %s (fd %d)", mapped, sockfd);
    }
    else
    {
        ESP_LOGI(TAG, "HTTP client connected (fd %d)", sockfd);
    }

    return ESP_OK;
}

// Sends `root` as the response body and frees it.
static esp_err_t send_json_response(httpd_req_t *req, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

// Copies a string field of a request body into a fixed buffer. A field that is absent leaves
// `out` as it was, so a PUT only has to carry what it changes. False if it is there but is not
// a string or does not fit.
static bool copy_json_string(const cJSON *root, const char *name, char *out, size_t len)
{
    const cJSON *item = cJSON_GetObjectItem(root, name);

    if (item == NULL)
    {
        return true;
    }

    if (!cJSON_IsString(item) || strlen(item->valuestring) >= len)
    {
        return false;
    }

    strlcpy(out, item->valuestring, len);

    return true;
}

#pragma region emoncms API

// The write API key never leaves the device: emoncms_uploader_get_config() hands it back blanked,
// and the response says only whether one is stored.
static esp_err_t emoncms_config_get_handler(httpd_req_t *req)
{
    emoncms_config_t config;
    bool key_set = false;

    emoncms_uploader_get_config(&config, &key_set);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "enabled", config.enabled);
    cJSON_AddStringToObject(root, "base_url", config.base_url);
    cJSON_AddBoolToObject(root, "key_set", key_set);
    cJSON_AddStringToObject(root, "node", config.node);
    cJSON_AddNumberToObject(root, "interval_s", config.interval_s);

    return send_json_response(req, root);
}

static esp_err_t emoncms_config_put_handler(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);

    if (root == NULL)
    {
        return ESP_OK;
    }

    // Start from what is saved, with the key blank -- which is what "keep the existing one" looks
    // like to emoncms_uploader_set_config().
    emoncms_config_t config;
    emoncms_uploader_get_config(&config, NULL);

    bool valid = copy_json_string(root, "base_url", config.base_url, sizeof(config.base_url)) &&
                 copy_json_string(root, "api_key", config.api_key, sizeof(config.api_key)) &&
                 copy_json_string(root, "node", config.node, sizeof(config.node));

    const cJSON *enabled = cJSON_GetObjectItem(root, "enabled");

    if (cJSON_IsBool(enabled))
    {
        config.enabled = cJSON_IsTrue(enabled);
    }

    const cJSON *interval = cJSON_GetObjectItem(root, "interval_s");

    if (cJSON_IsNumber(interval))
    {
        // Out of range is left for set_config to reject, rather than wrapped into a uint16_t.
        config.interval_s = interval->valuedouble >= 0 && interval->valuedouble <= UINT16_MAX ? (uint16_t)interval->valuedouble : 0;
    }

    cJSON_Delete(root);

    esp_err_t err = valid ? emoncms_uploader_set_config(&config) : ESP_ERR_INVALID_ARG;

    if (err == ESP_ERR_INVALID_ARG)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "The URL must start with http:// or https://, the node must not be empty and the interval "
                                "must be between 10 and 3600 seconds.");
        return ESP_OK;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save the emoncms configuration, err:%d", err);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Could not save the configuration.");
        return ESP_OK;
    }

    return emoncms_config_get_handler(req);
}

static void add_emoncms_result(cJSON *parent, const emoncms_result_t *result)
{
    cJSON_AddBoolToObject(parent, "success", result->ok);
    cJSON_AddNumberToObject(parent, "http_status", result->http_status);
    cJSON_AddStringToObject(parent, "response", result->response);
}

static void add_timestamp_or_null(cJSON *parent, const char *name, uint32_t ts)
{
    if (ts)
    {
        cJSON_AddNumberToObject(parent, name, ts);
    }
    else
    {
        cJSON_AddNullToObject(parent, name);
    }
}

static esp_err_t emoncms_status_get_handler(httpd_req_t *req)
{
    emoncms_status_t status;
    emoncms_uploader_get_status(&status);

    cJSON *root = cJSON_CreateObject();

    add_timestamp_or_null(root, "last_success", status.last_success_ts);

    if (status.last_error_ts)
    {
        cJSON *error = cJSON_AddObjectToObject(root, "last_error");
        cJSON_AddNumberToObject(error, "time", status.last_error_ts);
        add_emoncms_result(error, &status.last_error);
    }
    else
    {
        cJSON_AddNullToObject(root, "last_error");
    }

    cJSON_AddNumberToObject(root, "posts_ok", status.posts_ok);
    cJSON_AddNumberToObject(root, "posts_failed", status.posts_failed);
    cJSON_AddNumberToObject(root, "dropped", status.dropped);
    cJSON_AddNumberToObject(root, "buffered", status.buffered);
    cJSON_AddNumberToObject(root, "buffer_capacity", status.buffer_capacity);

    // Every key a payload can carry, so the page can show which ones the last payload lacked.
    cJSON *inputs = cJSON_AddArrayToObject(root, "inputs");

    for (size_t i = 0; i < emoncms_uploader_input_count(); i++)
    {
        cJSON_AddItemToArray(inputs, cJSON_CreateString(emoncms_uploader_input_key(i)));
    }

    add_timestamp_or_null(root, "last_payload_time", status.last_payload_ts);

    cJSON *payload = status.last_payload[0] ? cJSON_Parse(status.last_payload) : NULL;

    if (payload)
    {
        cJSON_AddItemToObject(root, "last_payload", payload);
    }
    else
    {
        cJSON_AddNullToObject(root, "last_payload");
    }

    return send_json_response(req, root);
}

// Parks the httpd task until emoncms answers or the request times out, like POST /api/nodes does
// for commissioning. It is a button, not something a page polls.
static esp_err_t emoncms_test_post_handler(httpd_req_t *req)
{
    emoncms_result_t result;
    esp_err_t err = emoncms_uploader_test(&result);

    if (err != ESP_OK)
    {
        // Nothing was sent, and `response` says why.
        httpd_resp_set_status(req, err == ESP_ERR_TIMEOUT ? "504 Gateway Timeout" : "409 Conflict");
    }

    cJSON *root = cJSON_CreateObject();
    add_emoncms_result(root, &result);

    return send_json_response(req, root);
}

#pragma endregion

#pragma region MQTT settings API

static void restart_mqtt_service(void);

// The password never leaves the device; the response says only whether one is stored.
static esp_err_t mqtt_config_get_handler(httpd_req_t *req)
{
    mqtt_settings_t settings;
    mqtt_settings_load(&settings);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "url", settings.url);
    cJSON_AddStringToObject(root, "username", settings.username);
    cJSON_AddBoolToObject(root, "password_set", settings.password[0] != '\0');
    cJSON_AddBoolToObject(root, "connected", is_mqtt_connected);

    return send_json_response(req, root);
}

static esp_err_t mqtt_config_put_handler(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);

    if (root == NULL)
    {
        return ESP_OK;
    }

    // Start from what is saved, with the password blank -- which is what "keep the existing one"
    // looks like to mqtt_settings_save().
    mqtt_settings_t settings;
    mqtt_settings_load(&settings);
    memset(settings.password, 0, sizeof(settings.password));

    bool valid = copy_json_string(root, "url", settings.url, sizeof(settings.url)) &&
                 copy_json_string(root, "username", settings.username, sizeof(settings.username)) &&
                 copy_json_string(root, "password", settings.password, sizeof(settings.password));

    cJSON_Delete(root);

    esp_err_t err = valid ? mqtt_settings_save(&settings) : ESP_ERR_INVALID_ARG;

    if (err == ESP_ERR_INVALID_ARG)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "The URL must be empty or start with mqtt://, and must not contain a username or password.");
        return ESP_OK;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save the MQTT settings, err:%d", err);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Could not save the settings.");
        return ESP_OK;
    }

    restart_mqtt_service();

    return mqtt_config_get_handler(req);
}

#pragma endregion

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_get_handler,
    .user_ctx = NULL,
    .is_websocket = true};

httpd_handle_t start_webserver(void)
{
    ESP_LOGI(TAG, "Configuring webserver...");

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // POST /api/nodes blocks on these until commissioning finishes.
    s_commissioning_lock = xSemaphoreCreateMutex();
    s_commissioning_done = xSemaphoreCreateBinary();

    if (s_commissioning_lock == NULL || s_commissioning_done == NULL)
    {
        ESP_LOGE(TAG, "Failed to create the commissioning semaphores");
    }

    // 38 are registered: 27 here, 2 for history, 1 for status, 5 for the companion API, 2 for MCP
    // and the wildcard. Registration past the cap fails with only a log line, so count them
    // before adding more.
    config.max_uri_handlers = 44;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 20480;
    config.open_fn = on_socket_opened;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    const httpd_uri_t home_get_uri = {
        .uri = "/api/home",
        .method = HTTP_GET,
        .handler = home_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t home_put_uri = {
        .uri = "/api/home",
        .method = HTTP_PUT,
        .handler = home_put_handler,
        .user_ctx = NULL};

    const httpd_uri_t nodes_post_uri = {
        .uri = "/api/nodes",
        .method = HTTP_POST,
        .handler = nodes_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t nodes_get_uri = {
        .uri = "/api/nodes",
        .method = HTTP_GET,
        .handler = nodes_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t node_get_uri = {
        .uri = "/api/nodes/*",
        .method = HTTP_GET,
        .handler = node_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t nodes_delete_uri = {
        .uri = "/api/nodes/*",
        .method = HTTP_DELETE,
        .handler = node_delete_handler,
        .user_ctx = NULL};

    const httpd_uri_t nodes_put_uri = {
        .uri = "/api/nodes/*",
        .method = HTTP_PUT,
        .handler = node_put_handler,
        .user_ctx = NULL};

    const httpd_uri_t radiators_post_uri = {
        .uri = "/api/radiators",
        .method = HTTP_POST,
        .handler = radiators_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t radiators_get_uri = {
        .uri = "/api/radiators",
        .method = HTTP_GET,
        .handler = radiators_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t radiator_get_uri = {
        .uri = "/api/radiators/*",
        .method = HTTP_GET,
        .handler = radiator_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t radiators_put_uri = {
        .uri = "/api/radiators/*",
        .method = HTTP_PUT,
        .handler = radiators_put_handler,
        .user_ctx = NULL};

    const httpd_uri_t radiators_delete_uri = {
        .uri = "/api/radiators/*",
        .method = HTTP_DELETE,
        .handler = radiators_delete_handler,
        .user_ctx = NULL};

    const httpd_uri_t rooms_post_uri = {
        .uri = "/api/rooms",
        .method = HTTP_POST,
        .handler = rooms_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t rooms_get_uri = {
        .uri = "/api/rooms",
        .method = HTTP_GET,
        .handler = rooms_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t room_get_uri = {
        .uri = "/api/rooms/*",
        .method = HTTP_GET,
        .handler = room_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t room_put_uri = {
        .uri = "/api/rooms/*",
        .method = HTTP_PUT,
        .handler = room_put_handler,
        .user_ctx = NULL};

    const httpd_uri_t rooms_delete_uri = {
        .uri = "/api/rooms/*",
        .method = HTTP_DELETE,
        .handler = rooms_delete_handler,
        .user_ctx = NULL};

    const httpd_uri_t reset_post_uri = {
        .uri = "/api/reset",
        .method = HTTP_POST,
        .handler = reset_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t sensors_get_uri = {
        .uri = "/api/sensors",
        .method = HTTP_GET,
        .handler = sensors_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t icd_counter_delete_uri = {
        .uri = "/api/icd_counter/*",
        .method = HTTP_DELETE,
        .handler = icd_counter_delete_handler,
        .user_ctx = NULL};

    const httpd_uri_t emoncms_config_get_uri = {
        .uri = "/api/emoncms/config",
        .method = HTTP_GET,
        .handler = emoncms_config_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t emoncms_config_put_uri = {
        .uri = "/api/emoncms/config",
        .method = HTTP_PUT,
        .handler = emoncms_config_put_handler,
        .user_ctx = NULL};

    const httpd_uri_t emoncms_status_get_uri = {
        .uri = "/api/emoncms/status",
        .method = HTTP_GET,
        .handler = emoncms_status_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t emoncms_test_post_uri = {
        .uri = "/api/emoncms/test",
        .method = HTTP_POST,
        .handler = emoncms_test_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t mqtt_config_get_uri = {
        .uri = "/api/mqtt/config",
        .method = HTTP_GET,
        .handler = mqtt_config_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t mqtt_config_put_uri = {
        .uri = "/api/mqtt/config",
        .method = HTTP_PUT,
        .handler = mqtt_config_put_handler,
        .user_ctx = NULL};

    const httpd_uri_t wildcard_get_uri = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = wildcard_get_handler,
        .user_ctx = NULL};

    esp_err_t ret = httpd_start(&server, &config);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &home_get_uri);
        httpd_register_uri_handler(server, &home_put_uri);
        httpd_register_uri_handler(server, &nodes_post_uri);
        httpd_register_uri_handler(server, &nodes_get_uri);
        httpd_register_uri_handler(server, &node_get_uri);
        httpd_register_uri_handler(server, &nodes_delete_uri);
        httpd_register_uri_handler(server, &nodes_put_uri);
        httpd_register_uri_handler(server, &radiators_post_uri);
        httpd_register_uri_handler(server, &radiators_get_uri);
        httpd_register_uri_handler(server, &radiator_get_uri);
        httpd_register_uri_handler(server, &radiators_put_uri);
        httpd_register_uri_handler(server, &radiators_delete_uri);
        httpd_register_uri_handler(server, &rooms_post_uri);
        httpd_register_uri_handler(server, &rooms_get_uri);
        httpd_register_uri_handler(server, &room_get_uri);
        httpd_register_uri_handler(server, &room_put_uri);
        httpd_register_uri_handler(server, &rooms_delete_uri);
        httpd_register_uri_handler(server, &sensors_get_uri);
        httpd_register_uri_handler(server, &reset_post_uri);
        httpd_register_uri_handler(server, &icd_counter_delete_uri);
        httpd_register_uri_handler(server, &emoncms_config_get_uri);
        httpd_register_uri_handler(server, &emoncms_config_put_uri);
        httpd_register_uri_handler(server, &emoncms_status_get_uri);
        httpd_register_uri_handler(server, &emoncms_test_post_uri);
        httpd_register_uri_handler(server, &mqtt_config_get_uri);
        httpd_register_uri_handler(server, &mqtt_config_put_uri);

        // Registered before the wildcard, which matches "/*" and would otherwise swallow them.
        history_api_register(server);
        status_api_register(server);
        companion_api_register(server, &g_node_manager);
        mcp_server_register(server);

        httpd_register_uri_handler(server, &wildcard_get_uri);

        ESP_LOGI(TAG, "WebService is up and running!");

        return server;
    }

    ESP_LOGI(TAG, "Error starting server %u!", ret);

    return NULL;
}

#pragma endregion

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        is_mqtt_connected = true;

        // Republished on every connect as well as being retained at the broker. Home Assistant
        // replays retained discovery when its MQTT integration restarts, and this covers the case
        // where the broker itself lost the retained copy. Rooms and radiators follow on the next
        // update_home() -- see ha_discovery_announce_all().
        ha_discovery_publish_online(_mqtt_client);
        ha_discovery_announce_all(_mqtt_client);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        is_mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        is_mqtt_connected = false;
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// Connects to the broker saved in NVS (see mqtt/mqtt_settings.h), or does nothing when there is
// none. The client is created once and reconfigured after that, never destroyed: its handle is
// passed around and published on from other tasks.
static void start_mqtt_service(void)
{
    mqtt_settings_t settings;
    mqtt_settings_load(&settings);

    if (settings.url[0] == '\0')
    {
        ESP_LOGI(TAG, "No MQTT broker configured");
        return;
    }

    // esp-mqtt copies these, so they only have to last until the client is configured. An unset
    // field has to be NULL rather than empty; on a client that already exists NULL means "leave
    // as it was", so removing a username or password takes a restart to reach the broker.
    const char *mqtt_url = settings.url;
    const char *mqtt_username = settings.username[0] ? settings.username : NULL;
    const char *mqtt_password = settings.password[0] ? settings.password : NULL;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = mqtt_url}},
        .credentials = {.username = mqtt_username, .authentication = {.password = mqtt_password}},
        // The broker publishes this if the device stops responding, which is what makes the
        // entities go unavailable in Home Assistant rather than sitting on their last value.
        .session = {
            .last_will = {
                .topic = HA_AVAILABILITY_TOPIC,
                .msg = "offline",
                .msg_len = 0,
                .qos = 0,
                .retain = 1}},
        // esp-mqtt defaults to 1024 bytes for both directions, and the discovery payload is around
        // 3.5 KB -- it would simply fail to publish. Only the outbound side needs the headroom;
        // nothing subscribed here receives anything large.
        .buffer = {
            .size = 1024,
            .out_size = 6144}};

    if (_mqtt_client == NULL)
    {
        _mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

        esp_mqtt_client_register_event(_mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    }
    else
    {
        esp_mqtt_set_config(_mqtt_client, &mqtt_cfg);
    }

    esp_mqtt_client_start(_mqtt_client);
}

// Applies settings that have just been saved. Called from the HTTP task, which is only running
// once the network is up.
static void restart_mqtt_service(void)
{
    if (_mqtt_client != NULL)
    {
        esp_mqtt_client_stop(_mqtt_client);
        is_mqtt_connected = false;
    }

    start_mqtt_service();
}

// CHIP owns the primary mDNS hostname (CONFIG_USE_MINIMAL_MDNS=n means we share the
// ESP-IDF mdns component with it), so publish ours as a delegated hostname rather than
// calling mdns_hostname_set, which would fight it.
void start_mdns_service()
{
    static bool mdns_registered = false;

    if (mdns_registered)
    {
        return;
    }

    esp_err_t err = mdns_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mDNS init failed: %d", err);
        return;
    }

    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");

    if (eth_netif == NULL)
    {
        ESP_LOGE(TAG, "Ethernet netif not found; skipping mDNS registration");
        return;
    }

    esp_netif_ip_info_t ip_info;

    if (esp_netif_get_ip_info(eth_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0)
    {
        ESP_LOGW(TAG, "Ethernet netif has no IPv4 address yet; skipping mDNS registration");
        return;
    }

    mdns_ip_addr_t addr = {};
    addr.addr.type = ESP_IPADDR_TYPE_V4;
    addr.addr.u_addr.ip4 = ip_info.ip;
    addr.next = NULL;

    err = mdns_delegate_hostname_add(MDNS_HOSTNAME, &addr);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mdns_delegate_hostname_add failed: %d", err);
        return;
    }

    err = mdns_service_add_for_host(NULL, "_http", "_tcp", MDNS_HOSTNAME, MDNS_HTTP_PORT, NULL, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mdns_service_add_for_host failed: %d", err);
        return;
    }

    mdns_registered = true;

    ESP_LOGI(TAG, "mDNS: %s.local -> " IPSTR " with _http._tcp:%d", MDNS_HOSTNAME, IP2STR(&ip_info.ip), MDNS_HTTP_PORT);
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "kInterfaceIpAddressChanged");
        break;
    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        ESP_LOGI(TAG, "kESPSystemEvent");

        if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
            event->Platform.ESPSystemEvent.Id == IP_EVENT_ETH_GOT_IP)
        {
            if (server == NULL)
            {
                server = start_webserver();
                start_mqtt_service();
            }

            // The history logger needs a real wall clock: its filenames and its slot
            // arithmetic are both dated. Until this syncs, time(NULL) is in 1970 and the
            // sampler skips every tick.
            time_sync_start();

            // Disabled while testing CONFIG_USE_MINIMAL_MDNS. CHIP's minimal mDNS binds UDP
            // 5353 itself, and mdns_init() inside here would fight it for the port. Cost of
            // leaving this off: no heating-monitor.local and no _http._tcp advert for the web
            // UI — reach the UI by IP instead. Re-enable this and set USE_MINIMAL_MDNS back to
            // n to return to the IDF mDNS stack.
            // start_mdns_service();
        }
        else if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                 event->Platform.ESPSystemEvent.Id == IP_EVENT_GOT_IP6)
        {
            if (!has_subscribed_on_startup)
            {
                // chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t ctx)
                //                                               { subscribe_all_temperature_measurements(&g_node_manager); }, 0);
                has_subscribed_on_startup = true;
            }
        }
        break;
    case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished:
        ESP_LOGI(TAG, "kSecureSessionEstablished");
        ESP_LOGI(TAG, "Session established with %llu", event->SecureSessionEstablished.PeerNodeId);
        break;
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        ESP_LOGI(TAG, "kServerReady");
        break;
    default:
        break;
    }
}

// Every UDP packet CHIP receives is posted to its event queue from the lwIP thread, and a full
// queue aborts (ScheduleLambda returns a CriticalFailure). CHIP's ESP32 platform only installs
// its mDNS filter from the Wi-Fi IPv6 handler, so on Ethernet every multicast mDNS packet on the
// LAN was queued and a busy network could fill the queue while the CHIP task was tied up in CASE
// crypto. Install the same filter ourselves: it drops mDNS packets that mention neither "matter"
// nor our hostname. It has to be set before the netif is up, as lwIP reads it from its own thread.
static void install_mdns_queue_filter()
{
#ifdef CONFIG_ENABLE_ENDPOINT_QUEUE_FILTER
    static chip::Inet::ESP32EndpointQueueFilter s_endpoint_queue_filter;

    // Same MAC CHIP's minimal mDNS derives the hostname from (the Ethernet MAC on this board).
    uint8_t mac[chip::DeviceLayer::ConfigurationManager::kPrimaryMACAddressLength];
    chip::MutableByteSpan mac_span(mac);

    if (chip::DeviceLayer::ConfigurationMgr().GetPrimaryMACAddress(mac_span) != CHIP_NO_ERROR || mac_span.size() != 6)
    {
        ESP_LOGE(TAG, "Failed to read the primary MAC; mDNS queue filter not installed");
        return;
    }

    char hostname[13]; // 12 hex digits plus the terminator snprintf writes
    for (size_t i = 0; i < 6; i++)
    {
        snprintf(&hostname[2 * i], 3, "%02X", mac_span.data()[i]);
    }

    if (s_endpoint_queue_filter.SetMdnsHostName(chip::CharSpan(hostname, 12)) != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to set the mDNS queue filter hostname");
        return;
    }

    chip::Inet::UDPEndPointImpl::SetQueueFilter(&s_endpoint_queue_filter);
    ESP_LOGI(TAG, "mDNS queue filter installed for host %.12s", hostname);
#endif
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "app_main()");

    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGE(TAG, "nvs_flash_init error");
    }
    ESP_ERROR_CHECK(err);

    node_manager_init(&g_node_manager);
    radiator_manager_init(&g_radiator_manager);
    room_manager_init(&g_room_manager);
    home_manager_init(&g_home_manager);
    heat_pump_manager_init(&g_heat_pump_manager);

    /* Matter start */

    ESP_LOGI(TAG, "Starting Matter...");

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    // Bring up the W5500. This is a controller build (CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=n),
    // so there is no Network Commissioning cluster to instantiate the driver for us. It has to
    // run after esp_matter::start() so that esp_netif_init() has happened and CHIP's IP_EVENT
    // handler is registered before DHCP completes.
    install_mdns_queue_filter();

    CHIP_ERROR eth_err = chip::DeviceLayer::NetworkCommissioning::ESPEthernetDriver::GetInstance().Init(nullptr);
    ABORT_APP_ON_FAILURE(eth_err == CHIP_NO_ERROR, ESP_LOGE(TAG, "Failed to start Ethernet"));

    ESP_LOGI(TAG, "Setup controller client and commissioner...");

    auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();
    controller_instance.set_icd_client_callback(on_icd_checkin_callback, nullptr);

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_matter::controller::matter_controller_client::get_instance().init(112233, 1, 5580);
    esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    // Starts the queue and the worker that drains it. Everything that wants a subscription calls
    // enqueue_subscription(); the worker serialises them and works out the attribute paths from
    // each node's device type list.
    err = subscription_manager_init(&g_node_manager);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start the subscription manager, err:%d", err));

    // History logging to the SD card. Neither call is fatal: with no card fitted the mount
    // fails, the logger's timers still run but every tick is a no-op, and the live dashboard
    // is unaffected.
    sd_card_init();

    err = history_logger_init(&g_home_manager);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start the history logger, err:%d", err);
    }

    // Not fatal either: without it nothing is posted to emoncms, and everything else carries on.
    err = emoncms_uploader_init(&g_home_manager);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start the emoncms uploader, err:%d", err);
    }

    //heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);

    //list_registered_icd();
}
