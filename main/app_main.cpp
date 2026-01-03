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

#include "cJSON.h"

#include "app_main.h"
#include "managers/node_manager.h"
#include "managers/radiator_manager.h"
#include "managers/room_manager.h"
#include "commands/pairing_command.h"
#include "commands/identify_command.h"

#include <setup_payload/ManualSetupPayloadParser.h>

#include "utilities/TokenIterator.h"
#include "utilities/UrlTokenBindings.h"

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

static httpd_handle_t server;
static int ws_socket;

static void ws_async_send(void *arg);

void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data);

#pragma region Command Callbacks

static void process_parts_list_attribute_response(uint64_t node_id,
                                                  const chip::app::ConcreteDataAttributePath &path,
                                                  chip::TLV::TLVReader *data)
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

    int idx = 0;
    while (data->Next() == CHIP_NO_ERROR)
    {
        if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
        {
            uint16_t endpoint_id = 0;

            if (data->Get(endpoint_id) == CHIP_NO_ERROR)
            {
                ESP_LOGI(TAG, "[%d] Endpoint ID: %u", ++idx, endpoint_id);

                add_endpoint(node, endpoint_id);

                auto *args = new std::tuple<uint64_t, uint16_t>(node_id, endpoint_id);

                chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg)
                                                              {
                                                                auto *args = reinterpret_cast<std::tuple<uint64_t, uint16_t> *>(arg);

                                                                uint32_t clusterId = Descriptor::Id;
                                                                uint32_t attributeId = Descriptor::Attributes::DeviceTypeList::Id;

                                                                esp_matter::controller::read_command *read_attr_command = chip::Platform::New<read_command>(std::get<0>(*args),
                                                                                                                                                            std::get<1>(*args),
                                                                                                                                                            clusterId,
                                                                                                                                                            attributeId,
                                                                                                                                                            esp_matter::controller::READ_ATTRIBUTE,
                                                                                                                                                            attribute_data_cb,
                                                                                                                                                            nullptr,
                                                                                                                                                            nullptr);
                                                                read_attr_command->send_command(); },
                                                              reinterpret_cast<intptr_t>(args));
            }
        }
    }

    data->ExitContainer(containerType);

    save_nodes_to_nvs(&g_node_manager);
}

void subscribe_done(uint64_t node_id, uint32_t subscription_id)
{
    ESP_LOGI(TAG, "Successfully subscribed, node %llu, subscription id 0x%08X", node_id, subscription_id);
}

void subscribe_failed(void *ctx)
{
    ESP_LOGE(TAG, "Failed to subscribe (context: %p)", ctx);
}

static void process_device_type_list_attribute_response(uint64_t node_id,
                                                        const chip::app::ConcreteDataAttributePath &path,
                                                        chip::TLV::TLVReader *data)
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
                    ESP_LOGI(TAG, "[%d] DeviceTypeID: %lu", idx, device_type_id);

                    add_device_type(node, path.mEndpointId, device_type_id);

                    // If this Endpoint is a Temperature Sensor, subscribe to the Sensor.
                    if (device_type_id == 770)
                    {
                        uint16_t min_interval = 0;
                        uint16_t max_interval = 15;

                        auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(
                            node_id,
                            path.mEndpointId,
                            TemperatureMeasurement::Id,
                            TemperatureMeasurement::Attributes::MeasuredValue::Id,
                            esp_matter::controller::SUBSCRIBE_ATTRIBUTE,
                            min_interval,
                            max_interval,
                            true,
                            attribute_data_cb,
                            nullptr,
                            subscribe_done,
                            subscribe_failed,
                            true);

                        if (!cmd)
                        {
                            ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
                        }
                        else
                        {
                            esp_err_t err = cmd->send_command();
                            if (err != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Failed to send subscribe command: %s", esp_err_to_name(err));
                            }
                        }
                    }
                }
            }

            idx++;
        }

        data->ExitContainer(listContainerType);
    }

    data->ExitContainer(containerType);

    save_nodes_to_nvs(&g_node_manager);
}

static void attribute_data_read_done(uint64_t remote_node_id, const ScopedMemoryBufferWithSize<AttributePathParams> &attr_path, const ScopedMemoryBufferWithSize<EventPathParams> &event_path)
{
    ESP_LOGI(TAG, "\nRead Info done for Nodeid: %016llx  Endpoint: %u Cluster: " ChipLogFormatMEI " Attribute " ChipLogFormatMEI "\n",
             remote_node_id, attr_path[0].mEndpointId, ChipLogValueMEI(attr_path[0].mClusterId), ChipLogValueMEI(attr_path[0].mAttributeId));
}

void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data)
{
    ChipLogProgress(chipTool, "attribute_data_cb: Nodeid: %016llx Endpoint: %u Cluster: " ChipLogFormatMEI " Attribute " ChipLogFormatMEI " DataVersion: %" PRIu32,
                    remote_node_id, path.mEndpointId, ChipLogValueMEI(path.mClusterId), ChipLogValueMEI(path.mAttributeId),
                    path.mDataVersion.ValueOr(0));

    if (path.mEndpointId == 0x0 && path.mClusterId == Descriptor::Id && path.mAttributeId == Descriptor::Attributes::PartsList::Id)
    {
        ESP_LOGI(TAG, "Processing Descriptor->PartsList attribute response...");
        process_parts_list_attribute_response(remote_node_id, path, data);
    }
    else if (path.mClusterId == Descriptor::Id && path.mAttributeId == Descriptor::Attributes::DeviceTypeList::Id)
    {
        ESP_LOGI(TAG, "Processing Descriptor->DeviceTypeList attribute response...");
        process_device_type_list_attribute_response(remote_node_id, path, data);
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
                    node->vendor_name = (char *)malloc(value.size() + 1);
                    memcpy(node->vendor_name, value.data(), value.size());
                    node->vendor_name[value.size()] = '\0';

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
                    node->product_name = (char *)malloc(value.size() + 1);
                    memcpy(node->product_name, value.data(), value.size());
                    node->product_name[value.size()] = '\0';

                    ESP_LOGI(TAG, "Product Name: %s", node->product_name);
                }
            }
        }
        else if (path.mAttributeId == BasicInformation::Attributes::NodeLabel::Id)
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;

                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    node->node_label = (char *)malloc(value.size() + 1);
                    memcpy(node->node_label, value.data(), value.size());
                    node->node_label[value.size()] = '\0';

                    ESP_LOGI(TAG, "Node Label: %s", node->node_label);
                }
            }
        }
        else if (path.mAttributeId == BasicInformation::Attributes::Location::Id)
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;

                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    node->location = (char *)malloc(value.size() + 1);
                    memcpy(node->location, value.data(), value.size());
                    node->location[value.size()] = '\0';

                    ESP_LOGI(TAG, "Location: %s", node->location);
                }
            }

            ESP_LOGI(TAG, "Finished reading Basic Information cluster attributes.");

            // We're finished with the Basic Information cluster, so now read the Descriptor->PartsList attribute to get the list of endpoints.
            //
            uint16_t endpointId = 0x0000;
            uint32_t clusterId = Descriptor::Id;
            uint32_t attributeId = Descriptor::Attributes::PartsList::Id;

            esp_matter::controller::read_command *read_attr_command = chip::Platform::New<read_command>(remote_node_id,
                                                                                                        endpointId,
                                                                                                        clusterId,
                                                                                                        attributeId,
                                                                                                        esp_matter::controller::READ_ATTRIBUTE,
                                                                                                        attribute_data_cb,
                                                                                                        attribute_data_read_done,
                                                                                                        nullptr);

            read_attr_command->send_command();
        }
    }
    else if (path.mClusterId == TemperatureMeasurement::Id && path.mAttributeId == TemperatureMeasurement::Attributes::MeasuredValue::Id)
    {
        ESP_LOGI(TAG, "Processing TemperatureMeasurement->MeasuredValue attribute response...");

        ESP_LOGI(TAG, "There are %u rooms", g_room_manager.room_count);
        ESP_LOGI(TAG, "There are %u radiators", g_radiator_manager.radiator_count);

        int16_t temperature;
        chip::app::DataModel::Decode(*data, temperature);

        ESP_LOGI(TAG, "Value: %d", temperature);

        // Find the appropriate radiator for this node/endpoint combination.
        //
        radiator_t *radiator = g_radiator_manager.radiator_list;

        while (radiator)
        {
            if (radiator->flow_temp_nodeId == remote_node_id || radiator->return_temp_nodeId == remote_node_id)
            {
                ESP_LOGI(TAG, "Node is assigned to radiator %u", radiator->radiator_id);

                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "channel", "radiator");
                cJSON_AddNumberToObject(root, "radiatorId", radiator->radiator_id);

                if (radiator->flow_temp_endpointId == path.mEndpointId)
                {
                    ESP_LOGI(TAG, "Reading Flow Temperature value");
                    radiator->flow_temperature = temperature;
                    cJSON_AddNumberToObject(root, "flowTemp", temperature);
                }
                else if (radiator->return_temp_endpointId == path.mEndpointId)
                {
                    ESP_LOGI(TAG, "Reading Return Temperature value");
                    radiator->return_temperature = temperature;
                    cJSON_AddNumberToObject(root, "returnTemp", temperature);
                }

                char *payload = cJSON_PrintUnformatted(root);

                httpd_queue_work(server, ws_async_send, payload);

                cJSON_Delete(root);

                break;
            }

            radiator = radiator->next;
        }

        // Find the appropriate room for this node/endpoint combination.
        //
        room_t *room = g_room_manager.room_list;

        while (room)
        {
            if (room->room_temperature_nodeId == remote_node_id && room->room_temperature_endpointId == path.mEndpointId)
            {
                ESP_LOGI(TAG, "Node is assigned to room %u", room->room_id);

                room->room_temperature = temperature;

                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "channel", "room");
                cJSON_AddNumberToObject(root, "roomId", room->room_id);
                cJSON_AddNumberToObject(root, "temperature", temperature);

                char *payload = cJSON_PrintUnformatted(root);

                httpd_queue_work(server, ws_async_send, payload);

                cJSON_Delete(root);

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
    char nodeIdStr[32];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "%" PRIu64, nodeId);

    add_node(&g_node_manager, nodeId);

    uint16_t endpointId = 0x0000;
    uint32_t clusterId = BasicInformation::Id;

    // We want to read a few attributes from the Basic Information cluster.
    //
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc(4);

    if (!attr_paths.Get())
    {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return;
    }

    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0);

    attr_paths[0] = AttributePathParams(endpointId, clusterId, BasicInformation::Attributes::VendorName::Id);
    attr_paths[1] = AttributePathParams(endpointId, clusterId, BasicInformation::Attributes::ProductName::Id);
    attr_paths[2] = AttributePathParams(endpointId, clusterId, BasicInformation::Attributes::NodeLabel::Id);
    attr_paths[3] = AttributePathParams(endpointId, clusterId, BasicInformation::Attributes::Location::Id);

    esp_matter::controller::read_command *read_attr_command = chip::Platform::New<read_command>(nodeId,
                                                                                                std::move(attr_paths),
                                                                                                std::move(event_paths),
                                                                                                attribute_data_cb,
                                                                                                attribute_data_read_done,
                                                                                                nullptr);

    read_attr_command->send_command();
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

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t *)arg;
    ws_pkt.len = strlen((char *)arg);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;

    esp_err_t err = httpd_ws_send_frame_async(server, ws_socket, &ws_pkt);

    ESP_LOGI(TAG, "Send result: %u", err);

    free(arg);
}

static esp_err_t ws_get_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
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
        buf = (uint8_t *)calloc(1, ws_pkt.len + 1);

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

    /* Read the data from the request into a buffer */
    char content[100];
    size_t recv_size = std::min(req->content_len, sizeof(content));

    esp_err_t err = httpd_req_recv(req, content, recv_size);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *inUseJSON = cJSON_GetObjectItemCaseSensitive(root, "inUse");
    const cJSON *setupCodeJSON = cJSON_GetObjectItemCaseSensitive(root, "setupCode");

    ESP_LOGI(TAG, "Setup Code: %s", setupCodeJSON->valuestring);

    char *setupCode = setupCodeJSON->valuestring;

    uint64_t node_id = g_node_manager.node_count + 1000; // TODO make this ever increasing or random?

    heating_monitor::controller::pairing_command_callbacks_t callbacks = {
        .commissioning_success_callback = on_commissioning_success_callback,
        .commissioning_failure_callback = on_commissioning_failure_callback,
    };

    heating_monitor::controller::pairing_command::get_instance().set_callbacks(callbacks);

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    if (cJSON_IsTrue(inUseJSON))
    {
        ESP_LOGI(TAG, "Using OnNetwork discovery");
        heating_monitor::controller::pairing_code(node_id, setupCode);
    }
    else
    {
        ESP_LOGI(TAG, "Using BLE discovery");

        SetupPayload payload;
        ManualSetupPayloadParser(setupCode).populatePayload(payload);

        uint32_t pincode = payload.setUpPINCode;

        // TODO Figure out this whole GetLongValue GetShortValue mess.
        // uint16_t discriminator = payload.discriminator.GetLongValue();
        uint16_t discriminator = 3840;

        ESP_LOGI(TAG, "pincode: %lu", pincode);
        ESP_LOGI(TAG, "discriminator: %u", discriminator);

        uint8_t dataset_tlvs_buf[254];
        uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);

        // TODO Get this from configuration.
        // Assumes thread!
        char *dataset = "0e080000000000000000000300001935060004001fffc002089f651677026f48070708fd9f6516770200000510d3ad39f0967b08debd26d32640a5dc8f03084d79486f6d6534300102ebf8041057aee90914b5d1097de9bb0818dc94690c0402a0f7f8";

        if (!convert_hex_str_to_bytes(dataset, dataset_tlvs_buf, dataset_tlvs_len))
        {
            return ESP_ERR_INVALID_ARG;
        }

        heating_monitor::controller::pairing_ble_thread(node_id, pincode, discriminator, dataset_tlvs_buf, dataset_tlvs_len);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    // This process is asynchronous, so we return 202 Accepted
    //
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, "Commissioning Started", 21);

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

        ESP_LOGI(TAG, "Node ID: %llu", node->node_id);

        cJSON_AddStringToObject(jNode, "nodeId", std::to_string(node->node_id).c_str());

        cJSON *endpointCount = cJSON_CreateNumber((double)node->endpoints_count);
        cJSON_AddItemToObject(jNode, "endpointCount", endpointCount);

        if (node->vendor_name)
        {
            cJSON_AddStringToObject(jNode, "vendorName", node->vendor_name);
        }
        if (node->product_name)
        {
            cJSON_AddStringToObject(jNode, "productName", node->product_name);
        }
        if (node->node_label)
        {
            cJSON_AddStringToObject(jNode, "nodeLabel", node->node_label);
        }
        if (node->location)
        {
            cJSON_AddStringToObject(jNode, "location", node->location);
        }

        cJSON *device_type_array = cJSON_CreateArray();

        for (uint16_t j = 0; j < node->endpoints_count; j++)
        {
            endpoint_entry_t endpoint = node->endpoints[j];

            ESP_LOGI(TAG, "Endpoint ID: %lu", endpoint.endpoint_id);
            ESP_LOGI(TAG, "DeviceTypes: %lu", endpoint.device_type_count);

            for (uint16_t k = 0; k < endpoint.device_type_count; k++)
            {
                uint32_t device_type_id = endpoint.device_type_ids[k];

                ESP_LOGI(TAG, "DeviceTypeId ID: %lu", device_type_id);

                cJSON *number = cJSON_CreateNumber((double)device_type_id);
                cJSON_AddItemToArray(device_type_array, number);
            }
        }

        cJSON_AddItemToObject(jNode, "deviceTypes", device_type_array);

        cJSON_AddItemToArray(root, jNode);

        node = node->next;
    }

    // TODO Add caching!!
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 Accepted");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t node_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Unpairing node...");

    size_t size = strlen(req->uri);

    char *pch = strrchr(req->uri, '/');
    int index_of_last_slash = pch - req->uri + 1;

    int length_of_nodeId = size - index_of_last_slash;

    char node_id_str[length_of_nodeId + 1];

    memcpy(node_id_str, &req->uri[index_of_last_slash], length_of_nodeId);

    node_id_str[length_of_nodeId] = '\0';

    ESP_LOGI(TAG, "Unpairing node  %s", node_id_str);

    uint64_t node_id = strtoull(node_id_str, NULL, 10);

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

    char templatePath[] = "/api/nodes/:nodeId/:action";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    uint64_t node_id = 0;

    if (bindings.hasBinding("nodeId"))
    {
        node_id = strtoull(bindings.get("nodeId"), NULL, 10);
    }

    if (bindings.hasBinding("action"))
    {
        ESP_LOGI(TAG, "Action: %s", bindings.get("action"));
    }

    ESP_LOGI(TAG, "PUT node %llu", node_id);

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = heating_monitor::controller::identify_command::get_instance().send_identify_command(node_id);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Identification failed");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

static esp_err_t radiators_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Adding a radiator");

    char content[150];
    size_t recv_size = std::min(req->content_len, sizeof(content));

    esp_err_t err = httpd_req_recv(req, content, recv_size);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *typeJSON = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *outputJSON = cJSON_GetObjectItemCaseSensitive(root, "output");
    const cJSON *flowSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowSensorNodeId");
    const cJSON *flowSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "flowSensorEndpointId");
    const cJSON *returnSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnSensorNodeId");
    const cJSON *returnSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "returnSensorEndpointId");

    char name[25];
    memcpy(name, nameJSON->valuestring, strlen(nameJSON->valuestring));

    add_radiator(&g_radiator_manager,
                 name,
                 (uint8_t)typeJSON->valueint,
                 (uint16_t)outputJSON->valueint,
                 (uint64_t)flowSensorNodeIdJSON->valueint,
                 (uint16_t)flowSensorEndpointIdJSON->valueint,
                 (uint64_t)returnSensorNodeIdJSON->valueint,
                 (uint16_t)returnSensorEndpointIdJSON->valueint);

    save_radiators_to_nvs(&g_radiator_manager);

    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, "ADDED", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t radiators_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all radiators ...");

    ESP_LOGI(TAG, "There are %u radiators(s)", g_radiator_manager.radiator_count);

    cJSON *root = cJSON_CreateArray();

    radiator_t *radiator = g_radiator_manager.radiator_list;

    while (radiator)
    {
        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "radiatorId", radiator->radiator_id);
        cJSON_AddStringToObject(jNode, "name", radiator->name);
        cJSON_AddNumberToObject(jNode, "type", radiator->type);
        cJSON_AddNumberToObject(jNode, "output", radiator->outputAtDelta50);

        cJSON_AddNumberToObject(jNode, "flowTemp", radiator->flow_temperature);
        cJSON_AddNumberToObject(jNode, "returnTemp", radiator->return_temperature);

        cJSON_AddItemToArray(root, jNode);

        radiator = radiator->next;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
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

    char node_id_str[length_of_nodeId + 1];

    memcpy(node_id_str, &req->uri[index_of_last_slash], length_of_nodeId);

    node_id_str[length_of_nodeId] = '\0';

    ESP_LOGI(TAG, "Deleting radiator %s", node_id_str);

    uint8_t node_id = (uint8_t)strtoul(node_id_str, NULL, 10);

    remove_radiator(&g_radiator_manager, node_id);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t rooms_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Adding a room...");

    char content[150];
    size_t recv_size = std::min(req->content_len, sizeof(content));

    esp_err_t err = httpd_req_recv(req, content, recv_size);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *temperatureSensorNodeIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorNodeId");
    const cJSON *temperatureSensorEndpointIdJSON = cJSON_GetObjectItemCaseSensitive(root, "temperatureSensorEndpointId");

    add_room(&g_room_manager, nameJSON->valuestring, (uint64_t)temperatureSensorNodeIdJSON->valueint, (uint16_t)temperatureSensorEndpointIdJSON->valueint);

    save_rooms_to_nvs(&g_room_manager);

    // TODO Return the new room in JSON!
    //
    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, "ADDED", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t rooms_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all rooms ...");

    ESP_LOGI(TAG, "There are %u rooms(s)", g_room_manager.room_count);

    cJSON *root = cJSON_CreateArray();

    room_t *room = g_room_manager.room_list;

    while (room)
    {
        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "roomId", room->room_id);
        cJSON_AddStringToObject(jNode, "name", room->name);
        cJSON_AddNumberToObject(jNode, "temperature", room->room_temperature);

        cJSON_AddItemToArray(root, jNode);

        room = room->next;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
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
    cJSON_AddNumberToObject(root, "temperature", room->room_temperature);

    cJSON *radiators;
    cJSON_AddItemToObject(root, "radiators", radiators = cJSON_CreateArray());

    ESP_LOGI(TAG, "There are %u radiators", room->radiator_count);

    for (uint8_t r = 0; r < room->radiator_count; r++)
    {
        uint8_t radiator_id = room->radiators[r];

        ESP_LOGI(TAG, "Loading radiator %u", radiator_id);

        radiator_t *radiator = find_radiator(&g_radiator_manager, radiator_id);

        if (radiator)
        {
            cJSON *radiatorNode = cJSON_CreateObject();

            cJSON_AddNumberToObject(radiatorNode, "radiatorId", radiator->radiator_id);
            cJSON_AddStringToObject(radiatorNode, "name", radiator->name);
            cJSON_AddNumberToObject(radiatorNode, "type", radiator->type);
            cJSON_AddNumberToObject(radiatorNode, "output", radiator->outputAtDelta50);
            cJSON_AddNumberToObject(radiatorNode, "flowTemp", radiator->flow_temperature);
            cJSON_AddNumberToObject(radiatorNode, "returnTemp", radiator->return_temperature);

            cJSON_AddItemToArray(radiators, radiatorNode);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
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

    char content[150];
    size_t recv_size = std::min(req->content_len, sizeof(content));

    esp_err_t err = httpd_req_recv(req, content, recv_size);

    cJSON *root = cJSON_Parse(content);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *radiatorIdsJSON = cJSON_GetObjectItemCaseSensitive(root, "radiatorIds");

    uint8_t radiator_count = cJSON_GetArraySize(radiatorIdsJSON);
    uint8_t *radiator_ids = (uint8_t *)calloc(radiator_count, sizeof(u_int8_t));

    cJSON *iterator = NULL;

    uint8_t i = 0;

    cJSON_ArrayForEach(iterator, radiatorIdsJSON)
    {
        ESP_LOGI(TAG, "Add radiatorId: %u to room %u", (uint8_t)iterator->valueint, room_id);
        radiator_ids[i++] = (uint8_t)iterator->valueint;
    }

    set_radiators(&g_room_manager, room_id, radiator_count, radiator_ids);

    httpd_resp_set_status(req, "200 Ok");
    httpd_resp_send(req, "ADDED", HTTPD_RESP_USE_STRLEN);

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

    httpd_resp_set_type(req, "application/json");
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
    ESP_LOGI(TAG, "Getting all temperature sensors ...");

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

                ESP_LOGI(TAG, "DeviceTypeId ID: %lu", device_type_id);

                if (device_type_id == 770)
                {
                    cJSON *jSensor = cJSON_CreateObject();
                    cJSON_AddNumberToObject(jSensor, "nodeId", node->node_id);
                    cJSON_AddNumberToObject(jSensor, "endpointId", endpoint.endpoint_id);
                    cJSON_AddStringToObject(jSensor, "vendorName", node->vendor_name ? node->vendor_name : "Unknown");
                    cJSON_AddStringToObject(jSensor, "productName", node->product_name ? node->product_name : "Unknown");
                    cJSON_AddItemToArray(root, jSensor);
                }
            }
        }

        node = node->next;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t write_index_html(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve root");

    httpd_resp_set_type(req, "text/html");

    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    extern const uint8_t bootstrap_css_start[] asm("_binary_index_html_start");
    extern const uint8_t bootstrap_css_end[] asm("_binary_index_html_end");
    const size_t bootcss_size = ((bootstrap_css_end - 1) - bootstrap_css_start);
    httpd_resp_send(req, (const char *)bootstrap_css_start, bootcss_size);

    return ESP_OK;
}

static esp_err_t write_app_js(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve js");

    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    extern const uint8_t js_file_start[] asm("_binary_app_js_start");
    extern const uint8_t js_file_end[] asm("_binary_app_js_end");
    const size_t js_file_size = ((js_file_end - 1) - js_file_start);
    httpd_resp_send(req, (const char *)js_file_start, js_file_size);

    return ESP_OK;
}

static esp_err_t write_app_css(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve css");

    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    extern const uint8_t css_file_start[] asm("_binary_app_css_start");
    extern const uint8_t css_file_end[] asm("_binary_app_css_end");
    const size_t css_file_size = ((css_file_end - 1) - css_file_start);
    httpd_resp_send(req, (const char *)css_file_start, css_file_size);

    return ESP_OK;
}

static esp_err_t wildcard_get_handler(httpd_req_t *req)
{
    const char *filename = req->uri;

    if (!filename)
    {
        ESP_LOGE(TAG, "Filename is too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_OK;
    }

    if (strcmp(filename, "/app.js") == 0)
    {
        return write_app_js(req);
    }
    else if (strcmp(filename, "/app.css") == 0)
    {
        return write_app_css(req);
    }
    else
    {
        return write_index_html(req);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");

    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_get_handler,
    .user_ctx = NULL,
    .is_websocket = true};

static httpd_handle_t start_webserver(void)
{
    ESP_LOGI(TAG, "Configuring webserver...");

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.max_uri_handlers = 20;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 20480;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

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

    const httpd_uri_t wildcard_get_uri = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = wildcard_get_handler,
        .user_ctx = NULL};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &nodes_post_uri);
        httpd_register_uri_handler(server, &nodes_get_uri);
        httpd_register_uri_handler(server, &nodes_delete_uri);
        httpd_register_uri_handler(server, &nodes_put_uri);
        httpd_register_uri_handler(server, &radiators_post_uri);
        httpd_register_uri_handler(server, &radiators_get_uri);
        httpd_register_uri_handler(server, &radiators_delete_uri);
        httpd_register_uri_handler(server, &rooms_post_uri);
        httpd_register_uri_handler(server, &rooms_get_uri);
        httpd_register_uri_handler(server, &room_get_uri);
        httpd_register_uri_handler(server, &room_put_uri);
        httpd_register_uri_handler(server, &rooms_delete_uri);
        httpd_register_uri_handler(server, &sensors_get_uri);
        httpd_register_uri_handler(server, &reset_post_uri);

        httpd_register_uri_handler(server, &wildcard_get_uri);

        ESP_LOGI(TAG, "WebService is up and running!");

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");

    return NULL;
}

#pragma endregion

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
            event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP)
        {
            if (server == NULL)
            {
                server = start_webserver();
            }
        }
        else if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                 event->Platform.ESPSystemEvent.Id == IP_EVENT_GOT_IP6)
        {
            chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t ctx)
                                                          { subscribe_all_temperature_measurements(&g_node_manager); }, 0);
        }
        break;
    default:
        break;
    }
}

extern "C" void app_main()
{
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

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif // CONFIG_ENABLE_CHIP_SHELL

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    ESP_LOGI(TAG, "Setup controller client and commissioner...");

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_matter::controller::matter_controller_client::get_instance().init(112233, 1, 5580);
    esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    ESP_LOGI(TAG, "Getting fabric table...");

    auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();

    auto fabricTable = controller_instance.get_commissioner()->GetFabricTable();

    uint8_t fabricCount = fabricTable->FabricCount();

    ESP_LOGI(TAG, "There are %u fabrics", fabricCount);
}
