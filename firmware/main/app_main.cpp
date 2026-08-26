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

#include <string>

#include <esp_matter.h>
#include <lib/dnssd/Types.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>

#include "cJSON.h"

#include "app_main.h"
#include "managers/node_manager.h"
#include "managers/subscription_manager.h"
#include "managers/calculations_manager.h"
#include "managers/radiator_manager.h"
#include "managers/room_manager.h"
#include "managers/home_manager.h"
#include "managers/pairing_manager.h"
#include "commands/pairing_command.h"
#include "commands/identify_command.h"

#include "app/InteractionModelEngine.h"

#include <setup_payload/ManualSetupPayloadParser.h>
#include <setup_payload/QRCodeSetupPayloadParser.h>

#include "utilities/TokenIterator.h"
#include "utilities/UrlTokenBindings.h"

#include "mqtt_client.h"

#include "mdns.h"

static const char *TAG = "app_main";

// Published over mDNS once Ethernet has an address, so the web UI is reachable at
// http://heating-monitor.local without having to look up the DHCP lease.
#define MDNS_HOSTNAME "heating-monitor"
#define MDNS_HTTP_PORT 80

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
pairing_manager_t g_pairing_manager = {0};

esp_mqtt_client_handle_t _mqtt_client;
static bool is_mqtt_connected = false;

static bool has_subscribed_on_startup = false;
static httpd_handle_t server;
static int ws_socket;

static void ws_async_send(void *arg);
static void log_client_token(httpd_req_t *req, const char *what);

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
                                                                attr_paths.Alloc(4);

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
}

void node_subscription_terminated_cb(uint64_t remote_node_id, uint32_t subscription_id)
{
    ESP_LOGI(TAG, "Subscription terminated, node 0x%016llX, subscription id 0x%08X", remote_node_id, subscription_id);

    // Indicate we have no subscription!
    //
    bool create_new_subscription = false;

    mark_node_has_no_subscription(&g_node_manager, remote_node_id, subscription_id, &create_new_subscription);

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

    matter_node_t *node = find_node(&g_node_manager, node_id);

    // A sleepy device is not worth chasing - it will get a subscription when it next checks in.
    //
    if (node && !node->is_icd)
    {
        enqueue_subscription(node_id);
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

                    matter_node_t *node = find_node(&g_node_manager, remote_node_id);

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
    else if (path.mClusterId == ThreadNetworkDiagnostics::Id && path.mAttributeId == ThreadNetworkDiagnostics::Attributes::NeighborTable::Id)
    {
        ESP_LOGI(TAG, "Processing ThreadNetworkDiagnostics->NeighborTable attribute response...");

        // Read the list of neighbours and send them via websockets.
        //
        chip::TLV::TLVType containerType;

        if (data->EnterContainer(containerType) != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "Failed to enter TLV container");
            return;
        }

        while (data->Next() == CHIP_NO_ERROR)
        {
            chip::TLV::TLVType listContainerType;

            if (data->EnterContainer(listContainerType) != CHIP_NO_ERROR)
            {
                ESP_LOGE(TAG, "Failed to enter TLV container");
                return;
            }

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "channel", "network");

            matter_node_t *node = find_node(&g_node_manager, remote_node_id);

            cJSON_AddNumberToObject(root, "extAddress", node->ext_address);

            while (data->Next() == CHIP_NO_ERROR)
            {
                chip::TLV::Tag tag = data->GetTag();

                int tagNumber = TLV::TagNumFromTag(tag);

                ESP_LOGI(TAG, "Processing Tag ID: %d", tagNumber);

                switch (tagNumber)
                {
                case 0: // ExtAddress
                    uint64_t ext_address;
                    chip::app::DataModel::Decode(*data, ext_address);

                    cJSON_AddNumberToObject(root, "neighborExtAddress", ext_address);
                    break;
                case 5:
                    uint8_t lqi;
                    chip::app::DataModel::Decode(*data, lqi);

                    cJSON_AddNumberToObject(root, "lqi", lqi);
                    break;

                case 6: // AverageRSSI
                    int8_t average_rssi;
                    chip::app::DataModel::Decode(*data, average_rssi);

                    cJSON_AddNumberToObject(root, "averageRssi", average_rssi);
                    break;

                case 11: // FullThreadDevice
                    bool full_thread_device;
                    chip::app::DataModel::Decode(*data, full_thread_device);

                    cJSON_AddBoolToObject(root, "fullThreadDevice", full_thread_device);
                    break;

                case 13: // IsChild
                    bool is_child;
                    chip::app::DataModel::Decode(*data, is_child);

                    cJSON_AddBoolToObject(root, "isChild", is_child);
                    break;
                }
            }

            data->ExitContainer(containerType);

            // All the tags have been process so sent it.
            char *payload = cJSON_PrintUnformatted(root);

            httpd_queue_work(server, ws_async_send, payload);

            cJSON_free(payload);
            cJSON_Delete(root);
        }

        data->ExitContainer(containerType);
    }
    else if (path.mClusterId == FlowMeasurement::Id && path.mAttributeId == FlowMeasurement::Attributes::MeasuredValue::Id)
    {
        ESP_LOGI(TAG, "Processing FlowMeasurement->MeasuredValue attribute response...");

        uint16_t flow;
        chip::app::DataModel::Decode(*data, flow);

        ESP_LOGI(TAG, "Flow Value: %d", flow);

        set_endpoint_measured_value(&g_node_manager, remote_node_id, path.mEndpointId, flow);

        // If this FlowMeasurement is set in the home, update it's value.
        //
        if (g_home_manager.heat_source_flow_rate_node_id == remote_node_id && g_home_manager.heat_source_flow_rate_endpoint_id == path.mEndpointId)
        {
            g_home_manager.heat_source_flow_rate = flow;
            update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);
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

        // Only the endpoint the user picked as the home's electrical meter is of interest.
        //
        if (g_home_manager.electrical_meter_node_id != remote_node_id || g_home_manager.electrical_meter_endpoint_id != path.mEndpointId)
        {
            return;
        }

        bool has_value = !value.IsNull();
        int64_t reading = has_value ? value.Value() : 0;

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
            break;
        }
    }
    else if (path.mClusterId == TemperatureMeasurement::Id && path.mAttributeId == TemperatureMeasurement::Attributes::MeasuredValue::Id)
    {
        ESP_LOGI(TAG, "Processing TemperatureMeasurement->MeasuredValue attribute response...");

        int16_t temperature;
        chip::app::DataModel::Decode(*data, temperature);

        ESP_LOGI(TAG, "Temperature Value: %d", temperature);

        set_endpoint_measured_value(&g_node_manager, remote_node_id, path.mEndpointId, temperature);

        bool hasMatched = false;

        // If this TemperatureMeasurement is set in the home, update it's value.
        //
        if (g_home_manager.outdoor_temp_node_id == remote_node_id && g_home_manager.outdoor_temp_endpoint_id == path.mEndpointId)
        {
            ESP_LOGI(TAG, "Device is assigned to the outdoor temperature sensor");

            g_home_manager.outdoor_temperature = temperature;

            // A change in outside temperature impacts all the rooms.
            //
            update_all_rooms_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

            hasMatched = true;
        }
        else if (g_home_manager.heat_source_flow_temp_node_id == remote_node_id && g_home_manager.heat_source_flow_temp_endpoint_id == path.mEndpointId)
        {
            ESP_LOGI(TAG, "Device is assigned to the heat source flow temperature sensor");

            g_home_manager.heat_source_flow_temperature = temperature;

            hasMatched = true;
        }
        else if (g_home_manager.heat_source_return_temp_node_id == remote_node_id && g_home_manager.heat_source_return_temp_endpoint_id == path.mEndpointId)
        {
            ESP_LOGI(TAG, "Device is assigned to the heat source return temperature sensor");

            g_home_manager.heat_source_return_temperature = temperature;

            hasMatched = true;
        }

        if (hasMatched)
        {
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
                    radiator->flow_temperature = temperature;
                    cJSON_AddNumberToObject(root, "flowTemp", temperature);
                }
                else if (radiator->return_temp_endpoint_id == path.mEndpointId)
                {
                    ESP_LOGI(TAG, "Reading Return Temperature value");
                    radiator->return_temperature = temperature;
                    cJSON_AddNumberToObject(root, "returnTemp", temperature);
                }

                update_radiator_outputs(&g_node_manager, &g_home_manager, &g_radiator_manager, &g_room_manager, _mqtt_client, radiator);

                char *payload = cJSON_PrintUnformatted(root);

                // This will free payload once it's done.
                httpd_queue_work(server, ws_async_send, payload);

                cJSON_free(payload);
                cJSON_Delete(root);

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
                cJSON_AddNumberToObject(root, "temperature", room->current_temperature);

                char *payload = cJSON_PrintUnformatted(root);

                // This will free payload once it's done.
                httpd_queue_work(server, ws_async_send, payload);

                cJSON_free(payload);
                cJSON_Delete(root);

                hasMatched = true;

                break;
            }

            room = room->next;
        }
    }
    else
    {
        ESP_LOGI(TAG, "Unhandled attribute_data_cb update");
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
    ESP_LOGI(TAG, "on_commissioning_failure_callback invoked!");
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
    ESP_LOGI(TAG, "Sending message over websocket...");

    // Grab a copy of the payload.
    //
    char *payload = (char *)calloc(1, strlen((char *)arg) + 1);
    memcpy(payload, arg, strlen((char *)arg));

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t *)payload;
    ws_pkt.len = strlen(payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;

    esp_err_t err = httpd_ws_send_frame_async(server, ws_socket, &ws_pkt);

    ESP_LOGI(TAG, "Send result: %u", err);

    free(payload);
}

static esp_err_t ws_get_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    ws_socket = httpd_req_to_sockfd(req);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

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

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t nodes_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Commissioning a node");

    uint64_t node_id = get_next_node_id(&g_node_manager);

    if (node_id == 0)
    {
        ESP_LOGE(TAG, "Failed to get a valid node ID for commissioning");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to get a valid node ID", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Will use %llu as node ID", node_id);

    log_client_token(req, "POST /api/nodes");

    /* Read the data from the request into a buffer */
    char content[req->content_len + 1];
    int received = httpd_req_recv(req, content, req->content_len);

    if (received <= 0)
    {
        ESP_LOGE(TAG, "Failed to read the request body");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Could not read request body", HTTPD_RESP_USE_STRLEN);
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

    const cJSON *setupCodeJSON = cJSON_GetObjectItemCaseSensitive(root, "setupCode");

    if (!cJSON_IsString(setupCodeJSON) || setupCodeJSON->valuestring == NULL)
    {
        ESP_LOGE(TAG, "Request is missing a setupCode");
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "setupCode is required", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setup Code: %s", setupCodeJSON->valuestring);

    char *setupCode = setupCodeJSON->valuestring;

    heating_monitor::controller::pairing_command_callbacks_t callbacks = {
        .commissioning_success_callback = on_commissioning_success_callback,
        .commissioning_failure_callback = on_commissioning_failure_callback};

    heating_monitor::controller::pairing_command::get_instance().set_callbacks(callbacks);

    // Commissioning is always OnNetwork: the companion app drives this through MatterSupport,
    // so iOS Home has already joined the device to Thread and opened a commissioning window
    // before we are asked to pair. The device is reachable over the border router by the time
    // we get here, and we never need BLE or the operational dataset ourselves.
    ESP_LOGI(TAG, "Using OnNetwork discovery");

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    heating_monitor::controller::pairing_code(node_id, setupCode);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    cJSON_Delete(root);

    // Commissioning runs asynchronously, so all we can hand back is the node id we reserved
    // for it. The client polls GET /api/nodes to find out whether it actually turned up.
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "nodeId", node_id);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "202 Accepted");

    char *json = cJSON_PrintUnformatted(response);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(response);

    return ESP_OK;
}

static esp_err_t nodes_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all nodes ...");

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

            cJSON_AddNumberToObject(endpointJSON, "measuredValue", endpoint.measured_value);

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

    // TODO Add caching!!
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "200 Accepted");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
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

static esp_err_t node_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Unpairing node...");

    log_client_token(req, "DELETE /api/nodes");

    char templatePath[] = "/api/nodes/:nodeId";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t node_id = 0;

    if (bindings.hasBinding("nodeId"))
    {
        node_id = strtoull(bindings.get("nodeId"), NULL, 10);
    }

    esp_matter::controller::pairing_command_callbacks_t callbacks = {
        .unpair_complete_callback = on_unpair_complete_callback};

    esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::controller::unpair_device(node_id);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unpairing failed");
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    else
    {
        remove_node(&g_node_manager, node_id);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "202 Accepted");
    }

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

    if (bindings.hasBinding("action"))
    {
        ESP_LOGI(TAG, "Action: %s", bindings.get("action"));

        // Select based on the action
        //
        if (strcmp("identify", bindings.get("action")) == 0)
        {
            ESP_LOGI(TAG, "Sending identify command");

            // TODO Schedule this!
            //
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            esp_err_t err = heating_monitor::controller::identify_command::get_instance().send_identify_command(node_id);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        else if (strcmp("interview", bindings.get("action")) == 0)
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
        else if (strcmp("update", bindings.get("action")) == 0)
        {
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

            set_node_name(node, nameJSON->valuestring);

            save_nodes_to_nvs(&g_node_manager);

            cJSON_Delete(root);
        }
        else if (strcmp("subscribe", bindings.get("action")) == 0)
        {
            ESP_LOGI(TAG, "Manually queueing subscription for node 0x%016llX", node_id);

            enqueue_subscription(node_id);
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

    get_endpoint_measured_value(&g_node_manager, flowSensorNodeIdJSON->valueint, flowSensorEndpointIdJSON->valueint, &new_radiator->flow_temperature);
    get_endpoint_measured_value(&g_node_manager, returnSensorNodeIdJSON->valueint, returnSensorEndpointIdJSON->valueint, &new_radiator->return_temperature);

    update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

    ESP_LOGI(TAG, "Radiator saved");

    if (is_mqtt_connected && mqttNameJSON)
    {
        // Announce this radiator via MQTT.
        //
        cJSON *root = cJSON_CreateObject();

        char device_name[32];
        snprintf(device_name, sizeof(device_name), "%s Radiator", nameJSON->valuestring);

        ESP_LOGI(TAG, "Announcing device `%s` via MQTT", device_name);

        cJSON *device = cJSON_CreateObject();
        cJSON_AddStringToObject(device, "name", device_name);
        cJSON_AddItemToObject(root, "device", device);

        cJSON *origin = cJSON_CreateObject();
        cJSON_AddStringToObject(device, "name", "Heating Monitor");
        cJSON_AddStringToObject(device, "url", "https://github.com/tomasmcguinness/matter-esp32-heating-monitor");
        cJSON_AddItemToObject(root, "origin", origin);

        cJSON *flow_sensor_component = cJSON_CreateObject();
        cJSON_AddStringToObject(flow_sensor_component, "p", "sensor");
        cJSON_AddStringToObject(flow_sensor_component, "device_class", "temperature");
        cJSON_AddStringToObject(flow_sensor_component, "unit_of_measurement", "°C");
        cJSON_AddStringToObject(flow_sensor_component, "value_template", "{{ value_json.flow_temperature}}");

        char flow_unique_id[50];
        snprintf(flow_unique_id, sizeof(flow_unique_id), "%s_radiator_flow_temperature", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(flow_sensor_component, "unique_id", flow_unique_id);

        char flow_default_entity_id[60];
        snprintf(flow_default_entity_id, sizeof(flow_default_entity_id), "sensor.%s_radiator_flow_temperature", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(flow_sensor_component, "default_entity_id", flow_default_entity_id);

        cJSON *return_sensor_component = cJSON_CreateObject();
        cJSON_AddStringToObject(return_sensor_component, "p", "sensor");
        cJSON_AddStringToObject(return_sensor_component, "device_class", "temperature");
        cJSON_AddStringToObject(return_sensor_component, "unit_of_measurement", "°C");
        cJSON_AddStringToObject(return_sensor_component, "value_template", "{{ value_json.return_temperature}}");

        char return_unique_id[50];
        snprintf(return_unique_id, sizeof(return_unique_id), "%s_radiator_return_temperature", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(return_sensor_component, "unique_id", return_unique_id);

        char return_default_entity_id[60];
        snprintf(return_default_entity_id, sizeof(return_default_entity_id), "sensor.%s_radiator_return_temperature", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(return_sensor_component, "default_entity_id", return_default_entity_id);

        cJSON *output_sensor_component = cJSON_CreateObject();
        cJSON_AddStringToObject(output_sensor_component, "p", "sensor");
        cJSON_AddStringToObject(output_sensor_component, "device_class", "temperature");
        cJSON_AddStringToObject(output_sensor_component, "unit_of_measurement", "°C");
        cJSON_AddStringToObject(output_sensor_component, "value_template", "{{ value_json.output}}");

        char output_unique_id[50];
        snprintf(output_unique_id, sizeof(output_unique_id), "%s_radiator_output_temperature", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(output_sensor_component, "unique_id", output_unique_id);

        char output_default_entity_id[60];
        snprintf(output_default_entity_id, sizeof(output_default_entity_id), "sensor.%s_output", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(output_sensor_component, "default_entity_id", output_default_entity_id);

        cJSON *components = cJSON_CreateObject();

        cJSON_AddItemToObject(components, "flow_temperature", flow_sensor_component);
        cJSON_AddItemToObject(components, "return_temperature", return_sensor_component);
        cJSON_AddItemToObject(components, "output", output_sensor_component);

        cJSON_AddItemToObject(root, "cmps", components);

        char state_topic[60];
        snprintf(state_topic, sizeof(state_topic), "heating_monitor/radiators/%s", mqttNameJSON->valuestring);
        cJSON_AddStringToObject(root, "state_topic", state_topic);

        char *payload = cJSON_PrintUnformatted(root);

        char config_topic[80];
        snprintf(config_topic, sizeof(config_topic), "homeassistant/device/radiators/%s/config", mqttNameJSON->valuestring);
        esp_mqtt_client_publish(_mqtt_client, config_topic, payload, 0, 0, 0);

        ESP_LOGI(TAG, "Announced radiator via MQTT");

        cJSON_free(payload);
        cJSON_Delete(root);
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

        cJSON_AddNumberToObject(jNode, "flowTemp", radiator->flow_temperature);
        cJSON_AddNumberToObject(jNode, "returnTemp", radiator->return_temperature);
        cJSON_AddNumberToObject(jNode, "meanWaterTemperature", radiator->mean_water_temperature);
        cJSON_AddNumberToObject(jNode, "currentOutput", radiator->heat_output);

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

    cJSON_AddNumberToObject(root, "flowTemp", radiator->flow_temperature);
    cJSON_AddNumberToObject(root, "returnTemp", radiator->return_temperature);
    cJSON_AddNumberToObject(root, "currentOutput", radiator->heat_output);

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

    get_endpoint_measured_value(&g_node_manager, flowSensorNodeIdJSON->valueint, flowSensorEndpointIdJSON->valueint, &updated_radiator->flow_temperature);
    get_endpoint_measured_value(&g_node_manager, returnSensorNodeIdJSON->valueint, returnSensorEndpointIdJSON->valueint, &updated_radiator->return_temperature);

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
            char config_topic[80];
            snprintf(config_topic, sizeof(config_topic), "homeassistant/device/radiators/%s/config", mqtt_name);
            esp_mqtt_client_publish(_mqtt_client, config_topic, "", 0, 0, 0);

            ESP_LOGI(TAG, "Announced radiator removal via MQTT");
        }

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
    const cJSON *targetTemperatureJSON = cJSON_GetObjectItemCaseSensitive(root, "targetTemperature");
    const cJSON *heatLossPerDegreeJSON = cJSON_GetObjectItemCaseSensitive(root, "heatLossPerDegree");
    const cJSON *temperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorNodeId");
    const cJSON *temperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorEndpointId");

    room_t *new_room = add_room(&g_room_manager,
                                nameJSON->valuestring,
                                mqttNameJSON->valuestring,
                                (uint16_t)targetTemperatureJSON->valueint,
                                (uint8_t)heatLossPerDegreeJSON->valueint, 
                                (uint64_t)temperatureSensorNodeIdJSON->valueint,
                                (uint16_t)temperatureSensorEndpointIdJSON->valueint);

    save_rooms_to_nvs(&g_room_manager);

    update_room_heat_loss(&g_node_manager, &g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client, new_room);

    // TODO Return the ID in JSON!
    //
    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, "ADDED", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t rooms_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all rooms ...");

    cJSON *root = cJSON_CreateArray();

    room_t *room = g_room_manager.room_list;

    while (room)
    {
        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "roomId", room->room_id);
        cJSON_AddStringToObject(jNode, "name", room->name);

        cJSON_AddNumberToObject(jNode, "targetTemperature", room->target_temperature);
        cJSON_AddNumberToObject(jNode, "currentTemperature", room->current_temperature);

        cJSON_AddNumberToObject(jNode, "predictedHeatLoss", room->predicted_heat_loss_per_degree);
        cJSON_AddNumberToObject(jNode, "measuredHeatLoss", room->measured_heat_loss_per_degree);
        cJSON_AddNumberToObject(jNode, "heatLossDifference", room->heat_loss_difference);

        uint16_t total_radiator_output = 0;

        for (uint8_t r = 0; r < room->radiator_count; r++)
        {
            radiator_t *radiator = find_radiator(&g_radiator_manager, room->radiators[r]);

            if (radiator)
            {
                total_radiator_output += radiator->heat_output;
            }
        }

        cJSON_AddNumberToObject(jNode, "heatInput", total_radiator_output);

        cJSON_AddItemToArray(root, jNode);

        room = room->next;
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

    int16_t current_temperature = 0;
    get_endpoint_measured_value(&g_node_manager, room->room_temperature_node_id, room->room_temperature_endpoint_id, &current_temperature);
    cJSON_AddNumberToObject(root, "currentTemperature", current_temperature);

    cJSON_AddNumberToObject(root, "targetTemperature", room->target_temperature);
    cJSON_AddNumberToObject(root, "temperatureSensorNodeId", room->room_temperature_node_id);
    cJSON_AddNumberToObject(root, "temperatureSensorEndpointId", room->room_temperature_endpoint_id);
    cJSON_AddNumberToObject(root, "heatLossPerDegree", room->predicted_heat_loss_per_degree);

    cJSON_AddNumberToObject(root, "predictedHeatLossAtTargetTemperature", room->predicted_heat_loss_at_target_temperature);
    cJSON_AddNumberToObject(root, "measuredHeatLossAtTargetTemperature", room->measured_heat_loss_at_target_temperature);

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
            cJSON_AddNumberToObject(radiatorNode, "flowTemp", radiator->flow_temperature);
            cJSON_AddNumberToObject(radiatorNode, "returnTemp", radiator->return_temperature);
            cJSON_AddNumberToObject(radiatorNode, "currentOutput", radiator->heat_output);

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

    room_t *updated_room = update_room(&g_room_manager,
                                       room_id, nameJSON->valuestring,
                                       (int16_t)targetTemperatureJSON->valueint,
                                       (uint8_t)heatLossPerDegreeJSON->valueint,
                                       radiator_count,
                                       radiator_ids,
                                       (uint64_t)temperatureSensorNodeIdJSON->valueint,
                                       (uint8_t)temperatureSensorEndpointIdJSON->valueint);

    save_rooms_to_nvs(&g_room_manager);

    get_endpoint_measured_value(&g_node_manager, temperatureSensorNodeIdJSON->valueint, temperatureSensorEndpointIdJSON->valueint, &updated_room->current_temperature);

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

    remove_room(&g_room_manager, room_id);

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req)
{
    radiator_manager_reset_and_reload(&g_radiator_manager);
    room_manager_reset_and_reload(&g_room_manager);

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
                uint32_t device_type_id = endpoint.device_type_ids[k];

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

// Reads the bearer token off the request and reports whether it matches the one this device
// issued. Nothing is rejected yet -- the embedded web UI has no token, so enforcing this
// would lock the browser out. Logging it lets us confirm the companion app is sending the
// right credential before turning enforcement on in a later change.
static void log_client_token(httpd_req_t *req, const char *what)
{
    size_t header_len = httpd_req_get_hdr_value_len(req, "Authorization");

    if (header_len == 0)
    {
        ESP_LOGI(TAG, "%s: no Authorization header", what);
        return;
    }

    char header[header_len + 1];

    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: could not read the Authorization header", what);
        return;
    }

    const char *presented = header;

    if (strncasecmp(presented, "Bearer ", 7) == 0)
    {
        presented += 7;
    }

    if (pairing_token_matches(&g_pairing_manager, presented))
    {
        ESP_LOGI(TAG, "%s: pairing token accepted", what);
    }
    else
    {
        ESP_LOGW(TAG, "%s: pairing token did NOT match (allowing anyway)", what);
    }
}

// Everything the companion app needs to find and talk to this device. The web UI renders
// this verbatim as a QR code on the Settings page, and the app scans it to pair.
static esp_err_t info_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting device info...");

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "name", "Heating Monitor");
    cJSON_AddStringToObject(root, "host", MDNS_HOSTNAME ".local");
    cJSON_AddStringToObject(root, "url", "http://" MDNS_HOSTNAME ".local");
    cJSON_AddStringToObject(root, "id", pairing_get_device_id(&g_pairing_manager));
    cJSON_AddStringToObject(root, "token", pairing_get_token(&g_pairing_manager));

    // mDNS on this board is an IPv4 delegated hostname and resolution is not always quick
    // off a phone, so hand out the raw address as a fallback the app can fall back to.
    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_ip_info_t ip_info;

    if (eth_netif != NULL && esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
    {
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(root, "ip", ip);
    }
    else
    {
        cJSON_AddNullToObject(root, "ip");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t home_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting home...");

    update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "outdoorTemperature", g_home_manager.outdoor_temperature);
    cJSON_AddNumberToObject(root, "outdoorTemperatureSensorNodeId", g_home_manager.outdoor_temp_node_id);
    cJSON_AddNumberToObject(root, "outdoorTemperatureSensorEndpointId", g_home_manager.outdoor_temp_endpoint_id);

    cJSON_AddNumberToObject(root, "heatSourceFlowTempSensorNodeId", g_home_manager.heat_source_flow_temp_node_id);
    cJSON_AddNumberToObject(root, "heatSourceFlowTempSensorEndpointId", g_home_manager.heat_source_flow_temp_endpoint_id);
    cJSON_AddNumberToObject(root, "heatSourceReturnTempSensorNodeId", g_home_manager.heat_source_return_temp_node_id);
    cJSON_AddNumberToObject(root, "heatSourceReturnTempSensorEndpointId", g_home_manager.heat_source_return_temp_endpoint_id);
    cJSON_AddNumberToObject(root, "heatSourceFlowRateSensorNodeId", g_home_manager.heat_source_flow_rate_node_id);
    cJSON_AddNumberToObject(root, "heatSourceFlowRateSensorEndpointId", g_home_manager.heat_source_flow_rate_endpoint_id);
    cJSON_AddNumberToObject(root, "electricalMeterNodeId", g_home_manager.electrical_meter_node_id);
    cJSON_AddNumberToObject(root, "electricalMeterEndpointId", g_home_manager.electrical_meter_endpoint_id);

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

    cJSON_AddNumberToObject(root, "heatSourceFlowTemperature", g_home_manager.heat_source_flow_temperature);
    cJSON_AddNumberToObject(root, "heatSourceReturnTemperature", g_home_manager.heat_source_return_temperature);
    cJSON_AddNumberToObject(root, "heatSourceFlowRate", g_home_manager.heat_source_flow_rate);
    cJSON_AddNumberToObject(root, "heatSourceOutput", g_home_manager.heat_source_output);

    cJSON_AddNumberToObject(root, "totalPredictedHeatLoss", g_home_manager.total_predicted_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "totalMeasuredHeatLoss", g_home_manager.total_measured_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "predictedHeatLossAtCurrentTemperature", g_home_manager.total_predicted_heat_loss_at_current_temperature);
    cJSON_AddNumberToObject(root, "measuredHeatLossAtCurrentTemperature", g_home_manager.total_measured_heat_loss_at_current_temperature);
    cJSON_AddNumberToObject(root, "radiatorCount", g_home_manager.radiator_count);
    cJSON_AddNumberToObject(root, "totalRadiatorOutput", g_home_manager.total_radiator_output);

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

    g_home_manager.outdoor_temp_node_id = (uint64_t)outdoorTemperatureSensorNodeIdJSON->valueint;
    g_home_manager.outdoor_temp_endpoint_id = (uint16_t)outdoorTemperatureSensorEndpointIdJSON->valueint;
    g_home_manager.heat_source_flow_temp_node_id = (uint64_t)flowTemperatureSensorNodeIdJSON->valueint;
    g_home_manager.heat_source_flow_temp_endpoint_id = (uint16_t)flowTemperatureSensorEndpointIdJSON->valueint;
    g_home_manager.heat_source_return_temp_node_id = (uint64_t)returnTemperatureSensorNodeIdJSON->valueint;
    g_home_manager.heat_source_return_temp_endpoint_id = (uint16_t)returnTemperatureSensorEndpointIdJSON->valueint;
    g_home_manager.heat_source_flow_rate_node_id = (uint64_t)flowRateSensorNodeIdJSON->valueint;
    g_home_manager.heat_source_flow_rate_endpoint_id = (uint16_t)flowRateSensorEndpointIdJSON->valueint;

    uint64_t electrical_meter_node_id = (uint64_t)electricalMeterNodeIdJSON->valueint;
    uint16_t electrical_meter_endpoint_id = (uint16_t)electricalMeterEndpointIdJSON->valueint;

    // Readings are only kept for the selected meter, so anything we are holding belongs to the old
    // one. There is nothing to copy across in its place; the subscription will report again shortly.
    //
    if (electrical_meter_node_id != g_home_manager.electrical_meter_node_id || electrical_meter_endpoint_id != g_home_manager.electrical_meter_endpoint_id)
    {
        g_home_manager.has_electrical_voltage = false;
        g_home_manager.electrical_voltage_mv = 0;
        g_home_manager.has_electrical_current = false;
        g_home_manager.electrical_current_ma = 0;
        g_home_manager.has_electrical_power = false;
        g_home_manager.electrical_power_mw = 0;
    }

    g_home_manager.electrical_meter_node_id = electrical_meter_node_id;
    g_home_manager.electrical_meter_endpoint_id = electrical_meter_endpoint_id;

    save_home_to_nvs(&g_home_manager);

    // Copy the outdoor temperature from the sensor to the home manager so that it's available immediately.
    //
    get_endpoint_measured_value(&g_node_manager, g_home_manager.outdoor_temp_node_id, g_home_manager.outdoor_temp_endpoint_id, &g_home_manager.outdoor_temperature);
    get_endpoint_measured_value(&g_node_manager, g_home_manager.heat_source_flow_temp_node_id, g_home_manager.heat_source_flow_temp_endpoint_id, &g_home_manager.heat_source_flow_temperature);
    get_endpoint_measured_value(&g_node_manager, g_home_manager.heat_source_return_temp_node_id, g_home_manager.heat_source_return_temp_endpoint_id, &g_home_manager.heat_source_return_temperature);
    get_endpoint_measured_value_uint16(&g_node_manager, g_home_manager.heat_source_flow_rate_node_id, g_home_manager.heat_source_flow_rate_endpoint_id, &g_home_manager.heat_source_flow_rate);

    update_home(&g_home_manager, &g_room_manager, &g_radiator_manager, _mqtt_client);

    httpd_resp_set_status(req, "201 Ok");
    httpd_resp_send(req, "ADDED", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t network_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Start to map Thread network...");

    matter_node_t *node = g_node_manager.node_list;

    while (node)
    {
        // Request the neightbour table from all wired Thread sensors.
        //
        if (node->ext_address != 0 && !node->is_icd)
        {
            auto *args = new std::tuple<uint64_t>(node->node_id);

            chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg)
                                                          {
            auto *args = reinterpret_cast<std::tuple<uint64_t> *>(arg);
                                                        
            // We want to read a few attributes from the Basic Information cluster.
            //
            ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
            attr_paths.Alloc(1);

            if (!attr_paths.Get())
            {
                ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
                delete args;
                return;
            }

            attr_paths[0] = AttributePathParams(0x0, ThreadNetworkDiagnostics::Id, ThreadNetworkDiagnostics::Attributes::NeighborTable::Id);

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

        node = node->next;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "201 OK");

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

    // Anything else is either "/" or one of the SPA's client-side routes
    // (/rooms, /devices, ...), all of which are served by index.html.
    return write_index_html(req);
}

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

    config.max_uri_handlers = 30;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 20480;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    const httpd_uri_t info_get_uri = {
        .uri = "/api/info",
        .method = HTTP_GET,
        .handler = info_get_handler,
        .user_ctx = NULL};

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

    const httpd_uri_t network_post_uri = {
        .uri = "/api/network",
        .method = HTTP_POST,
        .handler = network_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t icd_counter_delete_uri = {
        .uri = "/api/icd_counter/*",
        .method = HTTP_DELETE,
        .handler = icd_counter_delete_handler,
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
        httpd_register_uri_handler(server, &info_get_uri);
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
        httpd_register_uri_handler(server, &network_post_uri);
        httpd_register_uri_handler(server, &icd_counter_delete_uri);

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

        // announce_mqtt_devices();

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

static void start_mqtt_service(void)
{
    // size_t value_length;

    // nvs_handle_t nvs_handle;

    // int err = nvs_get_str(nvs_handle, "mqtt_url", NULL, &value_length);

    char *mqtt_url = "mqtt://192.168.1.10";
    char *mqtt_username = "homeassistant";
    char *mqtt_password = "e&HDtCAxh_^b:tKz8u,ocAw3QgHbX.P_kuYP";

    // if (err == ESP_OK)
    // {
    //     err = nvs_get_str(NVS_HANDLE, "mqtt_url", mqtt_url, &value_length);
    //     mqtt_url[value_length] = '/0';

    //     err = nvs_get_str(NVS_HANDLE, "mqtt_username", mqtt_username, &value_length);
    //     mqtt_username[value_length] = '/0';

    //     err = nvs_get_str(NVS_HANDLE, "mqtt_password", mqtt_password, &value_length);
    //     mqtt_password[value_length] = '/0';
    // }
    // else
    // {
    //     ESP_LOGI(TAG, "No MQTT credentials present");
    //     return;
    // }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = mqtt_url}},
        .credentials = {.username = mqtt_username, .authentication = {.password = mqtt_password}}};

    _mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(_mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_mqtt_client_start(_mqtt_client);
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
    pairing_manager_init(&g_pairing_manager);

    /* Matter start */

    ESP_LOGI(TAG, "Starting Matter...");

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    // Bring up the W5500. This is a controller build (CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=n),
    // so there is no Network Commissioning cluster to instantiate the driver for us. It has to
    // run after esp_matter::start() so that esp_netif_init() has happened and CHIP's IP_EVENT
    // handler is registered before DHCP completes.
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

    //heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);

    //list_registered_icd();
}
